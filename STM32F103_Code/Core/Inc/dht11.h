/*
 * dht11.h
 *
 *  Created on: Jun 24, 2026
 *      Author: ASUS
 */

#ifndef INC_DHT11_H_
#define INC_DHT11_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* -------------------------------------------------------------------------
 * Filter configuration — chỉnh tại đây nếu cần
 * -------------------------------------------------------------------------- */
#define DHT11_MA_SIZE       5       /* Số mẫu moving average [3..10]          */
#define DHT11_SPIKE_TEMP    5.0f    /* Ngưỡng spike nhiệt độ (°C)             */
#define DHT11_SPIKE_HUM    10.0f    /* Ngưỡng spike độ ẩm (%RH)              */

/* -------------------------------------------------------------------------
 * Sensor instance descriptor
 * -------------------------------------------------------------------------- */
typedef struct {
    /* Hardware */
    GPIO_TypeDef *port;
    uint16_t      pin;
    const char   *label;

    /* Filtered output — chỉ dùng 2 trường này trong main.c */
    float   temperature;            /* °C  đã lọc                             */
    float   humidity;               /* %RH đã lọc                             */
    uint8_t valid;                  /* 1 = có ít nhất 1 mẫu hợp lệ           */

    /* --- Internal state (không truy cập trực tiếp từ ngoài) --- */

    /* Moving average — buffer nhiệt độ */
    float   _tempBuf[DHT11_MA_SIZE];
    uint8_t _tempIdx;
    uint8_t _tempCount;

    /* Moving average — buffer độ ẩm */
    float   _humBuf[DHT11_MA_SIZE];
    uint8_t _humIdx;
    uint8_t _humCount;

    /* Spike filter */
    float   _lastValidTemp;
    float   _lastValidHum;
    uint8_t _initialized;           /* 0 = chưa có mẫu nào, bỏ qua spike     */
} DHT11_Sensor_t;

/* -------------------------------------------------------------------------
 * Pre-defined instances (defined in dht11.c)
 * -------------------------------------------------------------------------- */
extern DHT11_Sensor_t DHT11_INDOOR;
extern DHT11_Sensor_t DHT11_OUTDOOR;

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * @brief  Khởi tạo sensor, reset toàn bộ bộ lọc.
 *         Gọi sau HAL_TIM_Base_Start(&htim1).
 */
void DHT11_Init(DHT11_Sensor_t *sensor);

/**
 * @brief  Đọc sensor, áp dụng spike filter rồi moving average.
 *         Cập nhật sensor->temperature và sensor->humidity khi thành công.
 *
 * @retval 1  Đọc thành công, qua bộ lọc, giá trị đã được cập nhật.
 * @retval 0  Lỗi giao tiếp hoặc spike filter loại bỏ — giá trị cũ giữ nguyên.
 */
uint8_t DHT11_Read(DHT11_Sensor_t *sensor);

/**
 * @brief  Reset toàn bộ bộ lọc (buffer + trạng thái spike).
 *         Dùng khi cảm biến bị ngắt rồi cắm lại.
 */
void DHT11_ResetFilter(DHT11_Sensor_t *sensor);

#ifdef __cplusplus
}
#endif

#endif /* INC_DHT11_H_ */
