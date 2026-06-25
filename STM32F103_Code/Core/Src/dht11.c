/*
 * dht11.c
 *
 *  Created on: Jun 24, 2026
 *      Author: ASUS
 */
#include "dht11.h"
#include "tim.h"
#include <string.h>

/* =========================================================================
 * Pre-defined sensor instances
 * ========================================================================= */
DHT11_Sensor_t DHT11_INDOOR = {
    .port  = GPIOB,
    .pin   = GPIO_PIN_8,
    .label = "Indoor"
};

DHT11_Sensor_t DHT11_OUTDOOR = {
    .port  = GPIOB,
    .pin   = GPIO_PIN_9,
    .label = "Outdoor"
};

/* =========================================================================
 * Low-level helpers
 * ========================================================================= */

static void microDelay(uint16_t us)
{
    __HAL_TIM_SET_COUNTER(&htim1, 0);
    while (__HAL_TIM_GET_COUNTER(&htim1) < us);
}

static void setOutput(DHT11_Sensor_t *s)
{
    GPIO_InitTypeDef cfg = {0};
    cfg.Pin   = s->pin;
    cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    cfg.Speed = GPIO_SPEED_FREQ_LOW;
    cfg.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(s->port, &cfg);
}

static void setInput(DHT11_Sensor_t *s)
{
    GPIO_InitTypeDef cfg = {0};
    cfg.Pin  = s->pin;
    cfg.Mode = GPIO_MODE_INPUT;
    cfg.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(s->port, &cfg);
}

static uint8_t sendStart(DHT11_Sensor_t *s)
{
    uint32_t pMs, cMs;

    setOutput(s);
    HAL_GPIO_WritePin(s->port, s->pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(s->port, s->pin, GPIO_PIN_SET);
    microDelay(30);
    setInput(s);

    microDelay(40);
    if (HAL_GPIO_ReadPin(s->port, s->pin) != GPIO_PIN_RESET) return 0;

    microDelay(80);
    if (HAL_GPIO_ReadPin(s->port, s->pin) != GPIO_PIN_SET)   return 0;

    pMs = HAL_GetTick(); cMs = HAL_GetTick();
    while (HAL_GPIO_ReadPin(s->port, s->pin) && (pMs + 2 > cMs))
        cMs = HAL_GetTick();

    return 1;
}

static uint8_t readByte(DHT11_Sensor_t *s)
{
    uint8_t value = 0;
    uint32_t pMs, cMs;

    for (int i = 7; i >= 0; i--)
    {
        pMs = HAL_GetTick(); cMs = HAL_GetTick();
        while (!HAL_GPIO_ReadPin(s->port, s->pin) && (pMs + 2 > cMs))
            cMs = HAL_GetTick();

        microDelay(40);

        if (HAL_GPIO_ReadPin(s->port, s->pin))
            value |= (1 << i);

        pMs = HAL_GetTick(); cMs = HAL_GetTick();
        while (HAL_GPIO_ReadPin(s->port, s->pin) && (pMs + 2 > cMs))
            cMs = HAL_GetTick();
    }
    return value;
}

/* =========================================================================
 * Filter helpers
 * ========================================================================= */

static float localFabs(float x) { return (x < 0.0f) ? -x : x; }

/* Thêm newVal vào buffer vòng, trả về trung bình hiện tại */
static float maUpdate(float *buf, uint8_t *idx, uint8_t *count, float newVal)
{
    buf[*idx] = newVal;
    *idx = (*idx + 1) % DHT11_MA_SIZE;
    if (*count < DHT11_MA_SIZE) (*count)++;

    float sum = 0.0f;
    for (uint8_t i = 0; i < *count; i++) sum += buf[i];
    return sum / (float)(*count);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void DHT11_Init(DHT11_Sensor_t *sensor)
{
    memset(sensor->_tempBuf, 0, sizeof(sensor->_tempBuf));
    memset(sensor->_humBuf,  0, sizeof(sensor->_humBuf));
    sensor->_tempIdx       = 0;
    sensor->_tempCount     = 0;
    sensor->_humIdx        = 0;
    sensor->_humCount      = 0;
    sensor->_lastValidTemp = 0.0f;
    sensor->_lastValidHum  = 0.0f;
    sensor->_initialized   = 0;
    sensor->temperature    = 0.0f;
    sensor->humidity       = 0.0f;
    sensor->valid          = 0;

    setOutput(sensor);
    HAL_GPIO_WritePin(sensor->port, sensor->pin, GPIO_PIN_SET);
}

void DHT11_ResetFilter(DHT11_Sensor_t *sensor)
{
    memset(sensor->_tempBuf, 0, sizeof(sensor->_tempBuf));
    memset(sensor->_humBuf,  0, sizeof(sensor->_humBuf));
    sensor->_tempIdx     = 0;
    sensor->_tempCount   = 0;
    sensor->_humIdx      = 0;
    sensor->_humCount    = 0;
    sensor->_initialized = 0;
    sensor->valid        = 0;
}

uint8_t DHT11_Read(DHT11_Sensor_t *sensor)
{
    uint8_t rhi, rhd, tci, tcd, sum;

    /* ── Bước 1: Đọc raw ───────────────────────────────────────── */
    if (!sendStart(sensor)) return 0;

    rhi = readByte(sensor);
    rhd = readByte(sensor);
    tci = readByte(sensor);
    tcd = readByte(sensor);
    sum = readByte(sensor);

    if ((uint8_t)(rhi + rhd + tci + tcd) != sum) return 0;

    float rawTemp = (float)tci + (float)tcd / 10.0f;
    float rawHum  = (float)rhi + (float)rhd / 10.0f;

    /* ── Bước 2: Spike filter ──────────────────────────────────── */
    if (sensor->_initialized)
    {
        if (localFabs(rawTemp - sensor->_lastValidTemp) > DHT11_SPIKE_TEMP ||
            localFabs(rawHum  - sensor->_lastValidHum)  > DHT11_SPIKE_HUM)
        {
            /* Giá trị bất thường — giữ nguyên output đã lọc, báo lỗi */
            return 0;
        }
    }
    sensor->_lastValidTemp = rawTemp;
    sensor->_lastValidHum  = rawHum;
    sensor->_initialized   = 1;

    /* ── Bước 3: Moving average ────────────────────────────────── */
    sensor->temperature = maUpdate(sensor->_tempBuf,
                                   &sensor->_tempIdx,
                                   &sensor->_tempCount,
                                   rawTemp);

    sensor->humidity    = maUpdate(sensor->_humBuf,
                                   &sensor->_humIdx,
                                   &sensor->_humCount,
                                   rawHum);

    sensor->valid = 1;
    return 1;
}

