/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Smart Environment Monitor & Controller
  *
  * Chức năng:
  *  1. Đo DHT11_INDOOR / DHT11_OUTDOOR → LED + Buzzer cảnh báo nhiệt độ
  *  2. PID điều khiển PWM TIM2_CH1, TIM2_CH2, TIM3_CH1 theo nhiệt độ Indoor
  *  3. MQ6 đo khí gas → PID điều khiển PWM TIM3_CH2, cảnh báo PPM > 100
  *  4. Hiển thị OLED: Temp/Hum In-Out, PPM, PWM, Setpoint, State
  *  5. Gửi dữ liệu qua UART2 → ESP32 (JSON, mỗi 2 giây)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "dht11.h"
#include "mq6.h"
#include "pid.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PD */
/* ── Ngưỡng cảnh báo ─────────────────────────────────────────────────── */
#define TEMP_ALERT_THRESHOLD    80.0f   /* °C – cảnh báo nhiệt độ         */
#define GAS_ALERT_THRESHOLD     100.0f  /* ppm – cảnh báo khí gas         */

/* ── Setpoint PID nhiệt độ (mục tiêu làm mát) ───────────────────────── */
#define TEMP_SETPOINT           30.0f   /* °C                              */

/* ── Setpoint PID gas (mục tiêu thổi khí khi gas cao) ───────────────── */
#define GAS_SETPOINT_LOW        100.0f   /* ppm – quạt thổi nhẹ            */

/* ── PWM range (TIM2/TIM3, Period = 1000) ───────────────────────────── */
#define PWM_MIN                 200.0f
#define PWM_MAX                 999.0f

/* ── PID tuning: nhiệt độ ───────────────────────────────────────────── */
#define TEMP_KP                 200.0f
#define TEMP_KI                 0.2f
#define TEMP_KD                 10.0f

/* ── PID tuning: gas ────────────────────────────────────────────────── */
#define GAS_KP                  8.0f
#define GAS_KI                  0.2f
#define GAS_KD                  1.0f

/* ── Chu kỳ vòng lặp chính ──────────────────────────────────────────── */
#define LOOP_DELAY_MS           1000
/* USER CODE END PD */

/* USER CODE BEGIN PV */
/* ── PID instances ───────────────────────────────────────────────────── */
static PID_t pidTemp;   /* điều khiển TIM2_CH1, TIM2_CH2, TIM3_CH1      */
static PID_t pidGas;    /* điều khiển TIM3_CH2                           */

/* ── Buffer dùng chung cho sprintf / UART ───────────────────────────── */
static char strBuf[128];
static char uartBuf[256];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN PFP */
static void Alert_Update(uint8_t tempAlert, uint8_t gasAlert);
static void OLED_Update(uint8_t inOk, uint8_t outOk,
                        uint32_t pwmTemp, uint32_t pwmGas,
                        float gasPPM, uint8_t tempAlert, uint8_t gasAlert);
static void UART_SendData(uint8_t inOk, uint8_t outOk,
                          uint32_t pwmTemp, uint32_t pwmGas,
                          float gasPPM, uint8_t tempAlert, uint8_t gasAlert);
/* USER CODE END PFP */

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    /* ── HAL & Clock ─────────────────────────────────────────────────── */
    HAL_Init();
    SystemClock_Config();

    /* ── Peripheral init ─────────────────────────────────────────────── */
    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_I2C1_Init();
    MX_TIM1_Init();
    MX_TIM2_Init();
    MX_TIM3_Init();
    MX_USART2_UART_Init();

    /* USER CODE BEGIN 2 */
    /* ── Timer base (dùng cho DHT11 microDelay) ──────────────────────── */
    HAL_TIM_Base_Start(&htim1);

    /* ── OLED ────────────────────────────────────────────────────────── */
    ssd1306_Init();

    /* ── DHT11 ───────────────────────────────────────────────────────── */
    DHT11_Init(&DHT11_INDOOR);
    DHT11_Init(&DHT11_OUTDOOR);

    /* ── PWM channels ────────────────────────────────────────────────── */
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);

    /* Khởi đầu tất cả PWM = 0 */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);

    /* ── PID: nhiệt độ (TIM2_CH1, TIM2_CH2, TIM3_CH1) ──────────────── */
    PID_Init(&pidTemp,
             TEMP_KP, TEMP_KI, TEMP_KD,
             TEMP_SETPOINT,
             PWM_MIN, PWM_MAX);

    /* ── PID: gas (TIM3_CH2) ─────────────────────────────────────────── */
    PID_Init(&pidGas,
             GAS_KP, GAS_KI, GAS_KD,
             GAS_SETPOINT_LOW,
             PWM_MIN, PWM_MAX);

    /* ── MQ6: warm-up 20s + calibrate (blocking, chỉ 1 lần) ─────────── */
    {
        /* Hiển thị thông báo warm-up lên OLED trong lúc chờ */
        ssd1306_Fill(Black);
        ssd1306_SetCursor(10, 20);
        ssd1306_WriteString("MQ6 Warming up", Font_7x10, White);
        ssd1306_SetCursor(20, 38);
        ssd1306_WriteString("Please wait...", Font_7x10, White);
        ssd1306_UpdateScreen();
    }
    MQ6_Init();   /* blocking ~20s + 50 samples calibrate */

    PID_Reset(&pidTemp);
    PID_Reset(&pidGas);
    /* USER CODE END 2 */

    /* ── Infinite loop ───────────────────────────────────────────────── */
    while (1)
    {
        /* ── 1. Đọc DHT11 ───────────────────────────────────────────── */
        uint8_t inOk  = DHT11_Read(&DHT11_INDOOR);
        uint8_t outOk = DHT11_Read(&DHT11_OUTDOOR);

        /* ── 2. Đọc MQ6 ─────────────────────────────────────────────── */
        float gasPPM = MQ6_Read();   /* trả -1 nếu chưa calibrate        */
        if (gasPPM < 0.0f) gasPPM = 0.0f;

        /* ── 3. PID nhiệt độ → PWM TIM2_CH1, TIM2_CH2, TIM3_CH1 ────── */
        uint32_t pwmTemp = 0;
        if (inOk)
        {
            PID_Compute(&pidTemp, DHT11_INDOOR.temperature,
                        (float)LOOP_DELAY_MS / 1000.0f);
            pwmTemp = PID_GetPWM(&pidTemp);
        }
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pwmTemp);
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, pwmTemp);
//        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, pwmTemp);

        /* ── 4. PID gas → PWM TIM3_CH2 ──────────────────────────────── */
        uint32_t pwmGas;
        if (gasPPM < GAS_ALERT_THRESHOLD)
		{
			pwmGas = 150;
			PID_Reset(&pidGas);  /* reset tích phân, PID bắt đầu sạch khi vượt ngưỡng */
		}
		else
		{
			PID_Compute(&pidGas, gasPPM, (float)LOOP_DELAY_MS / 1000.0f);
			pwmGas = PID_GetPWM(&pidGas);
		}
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pwmGas);


        /* ── 5. Đánh giá cảnh báo ────────────────────────────────────── */
        uint8_t tempAlert = (inOk && DHT11_INDOOR.temperature > TEMP_ALERT_THRESHOLD) ? 1 : 0;
        uint8_t gasAlert  = MQ6_IsAlert();   /* PPM >= MQ6_ALERT_PPM (100) */

        /* ── 6. LED & Buzzer ─────────────────────────────────────────── */
        Alert_Update(tempAlert, gasAlert);

        /* ── 7. Hiển thị OLED ───────────────────────────────────────── */
        OLED_Update(inOk, outOk, pwmTemp, pwmGas,
                    gasPPM, tempAlert, gasAlert);

        /* ── 8. Gửi UART → ESP32 ────────────────────────────────────── */
        UART_SendData(inOk, outOk, pwmTemp, pwmGas,
                      gasPPM, tempAlert, gasAlert);

        /* ── 9. Chờ ─────────────────────────────────────────────────── */
        HAL_Delay(500);
    }
}

/* =========================================================================
 * Alert_Update
 *   - Cả hai OK   → LED Xanh ON,  LED Đỏ OFF, Buzzer OFF
 *   - Bất kỳ cảnh báo → LED Xanh OFF, LED Đỏ ON, Buzzer ON
 * ========================================================================= */
static void Alert_Update(uint8_t tempAlert, uint8_t gasAlert)
{
    if (tempAlert || gasAlert)
    {
        HAL_GPIO_WritePin(LED_Red_GPIO_Port,   LED_Red_Pin,   GPIO_PIN_SET);
        HAL_GPIO_WritePin(LED_Green_GPIO_Port, LED_Green_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(Buzzer_GPIO_Port,    Buzzer_Pin,    GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(LED_Red_GPIO_Port,   LED_Red_Pin,   GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED_Green_GPIO_Port, LED_Green_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(Buzzer_GPIO_Port,    Buzzer_Pin,    GPIO_PIN_RESET);
    }
}

/* =========================================================================
 * OLED_Update  –  Layout 128×64, chia 2 vùng ngang
 *
 *  Cột trái (0..62)   : INDOOR temp/hum + PWM nhiệt + State nhiệt
 *  Cột phải (65..127) : OUTDOOR temp/hum + PPM + PWM gas
 *  Dòng kẻ dọc tại x = 63
 *  Dòng cuối (y=54)   : Setpoint nhiệt | Setpoint gas
 * ========================================================================= */
static void OLED_Update(uint8_t inOk, uint8_t outOk,
                        uint32_t pwmTemp, uint32_t pwmGas,
                        float gasPPM, uint8_t tempAlert, uint8_t gasAlert)
{
    ssd1306_Fill(Black);

    /* Đường kẻ dọc giữa */
    for (uint8_t y = 0; y < 64; y++)
        ssd1306_DrawPixel(63, y, White);

    /* ── Cột trái: INDOOR ─────────────────────────────────────────────── */
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("INDOOR", Font_7x10, White);

    if (inOk)
    {
        /* Nhiệt độ */
        sprintf(strBuf, "%d.%dC",
                (int)DHT11_INDOOR.temperature,
                (int)(DHT11_INDOOR.temperature * 10) % 10);
        ssd1306_SetCursor(0, 12);
        ssd1306_WriteString(strBuf, Font_7x10, White);

        /* Độ ẩm */
        sprintf(strBuf, "%d.%d%%",
                (int)DHT11_INDOOR.humidity,
                (int)(DHT11_INDOOR.humidity * 10) % 10);
        ssd1306_SetCursor(0, 24);
        ssd1306_WriteString(strBuf, Font_7x10, White);

        /* PWM nhiệt */
        sprintf(strBuf, "P:%lu", pwmTemp);
        ssd1306_SetCursor(0, 36);
        ssd1306_WriteString(strBuf, Font_7x10, White);

    }
    else
    {
        ssd1306_SetCursor(0, 12);
        ssd1306_WriteString("--.-C", Font_7x10, White);
        ssd1306_SetCursor(0, 24);
        ssd1306_WriteString("--.-%", Font_7x10, White);
        ssd1306_SetCursor(0, 36);
        ssd1306_WriteString("P:---", Font_7x10, White);
    }

    /* Setpoint nhiệt */
    sprintf(strBuf, "S:%.0fC", TEMP_SETPOINT);
    ssd1306_SetCursor(0, 48);
    ssd1306_WriteString(strBuf, Font_7x10, White);

    /* ── Cột phải: OUTDOOR ────────────────────────────────────────────── */
    ssd1306_SetCursor(66, 0);
    ssd1306_WriteString("OUTDR", Font_7x10, White);

    if (outOk)
    {
        sprintf(strBuf, "%d.%dC",
                (int)DHT11_OUTDOOR.temperature,
                (int)(DHT11_OUTDOOR.temperature * 10) % 10);
        ssd1306_SetCursor(66, 12);
        ssd1306_WriteString(strBuf, Font_7x10, White);

        sprintf(strBuf, "%d.%d%%",
                (int)DHT11_OUTDOOR.humidity,
                (int)(DHT11_OUTDOOR.humidity * 10) % 10);
        ssd1306_SetCursor(66, 24);
        ssd1306_WriteString(strBuf, Font_7x10, White);
    }
    else
    {
        ssd1306_SetCursor(66, 12);
        ssd1306_WriteString("--.-C", Font_7x10, White);
        ssd1306_SetCursor(66, 24);
        ssd1306_WriteString("--.-%", Font_7x10, White);
    }

    /* PPM */
    sprintf(strBuf, "%dppm", (int)gasPPM);
    ssd1306_SetCursor(66, 36);
    ssd1306_WriteString(strBuf, Font_7x10, White);

    /* State gas */
    ssd1306_SetCursor(66, 48);
    ssd1306_WriteString(gasAlert ? "!GAS!" : "SAFE", Font_7x10, White);

    ssd1306_UpdateScreen();
}

/* =========================================================================
 * UART_SendData  –  Gửi JSON tới ESP32 qua USART2
 *
 *  Format:
 *  {"ti":25.3,"hi":60.1,"to":28.0,"ho":65.0,
 *   "ppm":45,"pt":350,"pg":120,"sp_t":30,"sp_g":50,
 *   "st_t":"OK","st_g":"SAFE"}\r\n
 * ========================================================================= */
static void UART_SendData(uint8_t inOk, uint8_t outOk,
                          uint32_t pwmTemp, uint32_t pwmGas,
                          float gasPPM, uint8_t tempAlert, uint8_t gasAlert)
{
    float ti = inOk ? DHT11_INDOOR.temperature : -1.0f;
    float hi = inOk ? DHT11_INDOOR.humidity    : -1.0f;

    int len = snprintf(uartBuf, sizeof(uartBuf),
        "{"
        "\"temp\":%d.%d,"
        "\"humid\":%d.%d,"
        "\"gas\":%d"
        "}\n",
        (int)ti, (ti < 0.0f ? 0 : (int)(ti * 10) % 10),
        (int)hi, (hi < 0.0f ? 0 : (int)(hi * 10) % 10),
        (int)gasPPM
    );

    if (len > 0)
        HAL_UART_Transmit(&huart2, (uint8_t *)uartBuf, (uint16_t)len, 100);
}

/* =========================================================================
 * SystemClock_Config  –  HSE 8MHz × PLL9 = 72MHz (STM32F103)
 * ========================================================================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState            = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue      = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL         = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
        Error_Handler();

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection    = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
        Error_Handler();
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
