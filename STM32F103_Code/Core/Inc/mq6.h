/*
 * mq6.h
 *
 *  Created on: Jun 24, 2026
 *      Author: ASUS
 */

#ifndef INC_MQ6_H_
#define INC_MQ6_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "adc.h"

/* -------------------------------------------------------------------------
 * Cấu hình phần cứng
 * -------------------------------------------------------------------------- */
#define MQ6_RL_KOHM           10.0f    /* Điện trở tải RL (kΩ)                 */
#define MQ6_VCC               3.3f    /* Điện áp cấp (V)                       */
#define MQ6_ADC_RESOLUTION    4095.0f  /* 12-bit ADC                            */
#define MQ6_CALIBRATE_SAMPLES 64       /* Số mẫu lấy trung bình khi tính R0    */
#define MQ6_ALERT_PPM         300.0f  /* Ngưỡng cảnh báo gas (ppm)            */

/* -------------------------------------------------------------------------
 * Cấu hình bộ lọc — chỉnh tại đây
 * -------------------------------------------------------------------------- */

/* EMA: α ∈ (0, 1)
 *   α lớn (0.5–0.8) → phản hồi nhanh, ít lọc nhiễu
 *   α nhỏ (0.05–0.2) → mượt hơn, phản hồi chậm hơn
 *   Khuyến nghị cho MQ6: 0.1–0.2                                            */
#define MQ6_EMA_ALPHA         0.15f

/* Outlier filter: bỏ qua mẫu nếu lệch quá giá trị này so với EMA hiện tại
 *   Đơn vị ppm. Tăng nếu hay bị loại nhầm khi gas tăng đột ngột thật.      */
#define MQ6_OUTLIER_PPM       500.0f

/* -------------------------------------------------------------------------
 * MQ6 instance
 * -------------------------------------------------------------------------- */
typedef struct {
    float   R0;           /* Điện trở baseline trong không khí sạch (kΩ)      */
    float   ppm;          /* Nồng độ gas đã lọc (output cuối)                 */
    float   emaValue;     /* Trạng thái nội bộ EMA                            */
    uint8_t calibrated;   /* 1 = đã calibrate R0                              */
    uint8_t emaReady;     /* 1 = EMA đã có mẫu đầu tiên                      */
} MQ6_t;

/* Pre-defined instance (defined in mq6.c) */
extern MQ6_t MQ6;

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * @brief  Khởi tạo, warm-up 20s và calibrate R0.
 *         Gọi một lần sau MX_ADC1_Init().
 *         !! Đảm bảo không có gas khi gọi !!
 */
void MQ6_Init(void);

/**
 * @brief  Đọc ADC, tính ppm raw, áp dụng outlier filter + EMA.
 *         Cập nhật MQ6.ppm.
 *
 * @retval ppm  Nồng độ gas đã lọc (ppm). -1.0f nếu chưa calibrate.
 */
float MQ6_Read(void);

/**
 * @brief  Trả về ppm đã lọc từ lần đọc gần nhất.
 */
float MQ6_GetPPM(void);

/**
 * @brief  Kiểm tra nồng độ gas có vượt MQ6_ALERT_PPM không.
 * @retval 1 = cảnh báo, 0 = bình thường
 */
uint8_t MQ6_IsAlert(void);

/**
 * @brief  Reset EMA về trạng thái chưa có mẫu.
 *         Dùng khi cảm biến bị ngắt rồi cắm lại.
 */
void MQ6_ResetFilter(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_MQ6_H_ */
