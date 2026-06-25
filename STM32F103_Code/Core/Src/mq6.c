/*
 * mq6.c
 *
 *  Created on: Jun 24, 2026
 *      Author: ASUS
 */
#include "mq6.h"
#include <math.h>

/* =========================================================================
 * Pre-defined instance
 * ========================================================================= */
MQ6_t MQ6 = {
    .R0         = 10.0f,
    .ppm        = 0.0f,
    .emaValue   = 0.0f,
    .calibrated = 0,
    .emaReady   = 0
};

/* =========================================================================
 * Hệ số đường cong LPG (datasheet MQ6)
 * ========================================================================= */
#define MQ6_CURVE_A   1000.0f
#define MQ6_CURVE_B   (-2.2f)

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static uint32_t readADC(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return val;
}

static float adcToRS(uint32_t adcVal)
{
    if (adcVal == 0) adcVal = 1;
    float vout = MQ6_VCC * (float)adcVal / MQ6_ADC_RESOLUTION;
    if (vout <= 0.0f) vout = 0.001f;
    float rs = MQ6_RL_KOHM * (MQ6_VCC - vout) / vout;
    return (rs < 0.0f) ? 0.001f : rs;
}

static float localFabs(float x) { return (x < 0.0f) ? -x : x; }

/* Chuyển RS/R0 ratio → ppm theo power-law datasheet */
static float ratioToPPM(float ratio)
{
    if (ratio <= 0.0f) ratio = 0.001f;
    float ppm = MQ6_CURVE_A * powf(ratio, MQ6_CURVE_B);
    return (ppm < 0.0f) ? 0.0f : ppm;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void MQ6_Init(void)
{
    /* Warm-up: nung nóng SnO₂ đến nhiệt độ hoạt động */
    HAL_Delay(20000);

    /* Calibrate R0: trung bình MQ6_CALIBRATE_SAMPLES mẫu RS */
    float rsSum = 0.0f;
    for (uint16_t i = 0; i < MQ6_CALIBRATE_SAMPLES; i++)
    {
        rsSum += adcToRS(readADC());
        HAL_Delay(50);
    }
    float rsAvg = rsSum / (float)MQ6_CALIBRATE_SAMPLES;

    /* Trong không khí sạch: RS/R0 ≈ 10 (datasheet MQ6) */
    MQ6.R0         = rsAvg / 10.0f;
    MQ6.calibrated = 1;
    MQ6.emaReady   = 0;   /* reset EMA sau khi calibrate */
}

float MQ6_Read(void)
{
    if (!MQ6.calibrated) return -1.0f;

    /* ── Bước 1: Đọc raw → ppm_raw ──────────────────────────── */
    float rs      = adcToRS(readADC());
    float ratio   = rs / MQ6.R0;
    float ppmRaw  = ratioToPPM(ratio);

    /* ── Bước 2: Outlier filter ──────────────────────────────── */
    if (MQ6.emaReady)
    {
        if (localFabs(ppmRaw - MQ6.emaValue) > MQ6_OUTLIER_PPM)
        {
            /* Spike bất thường — giữ nguyên output cũ */
            return MQ6.ppm;
        }
    }

    /* ── Bước 3: EMA ─────────────────────────────────────────── */
    if (!MQ6.emaReady)
    {
        /* Mẫu đầu tiên: khởi tạo EMA bằng chính giá trị đó */
        MQ6.emaValue = ppmRaw;
        MQ6.emaReady = 1;
    }
    else
    {
        MQ6.emaValue = MQ6_EMA_ALPHA * ppmRaw
                     + (1.0f - MQ6_EMA_ALPHA) * MQ6.emaValue;
    }

    MQ6.ppm = MQ6.emaValue;
    return MQ6.ppm;
}

float MQ6_GetPPM(void)
{
    return MQ6.ppm;
}

uint8_t MQ6_IsAlert(void)
{
    return (MQ6.ppm >= MQ6_ALERT_PPM) ? 1 : 0;
}

void MQ6_ResetFilter(void)
{
    MQ6.emaValue = 0.0f;
    MQ6.emaReady = 0;
    MQ6.ppm      = 0.0f;
}

