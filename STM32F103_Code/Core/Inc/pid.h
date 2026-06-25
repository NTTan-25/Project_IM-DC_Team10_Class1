/*
 * pid.h
 *
 *  Created on: Jun 24, 2026
 *      Author: ASUS
 */

#ifndef INC_PID_H_
#define INC_PID_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* -------------------------------------------------------------------------
 * PID instance
 * -------------------------------------------------------------------------- */
typedef struct {
    /* Tuning parameters */
    float Kp;               /* Proportional gain                */
    float Ki;               /* Integral gain                    */
    float Kd;               /* Derivative gain                  */

    /* Setpoint */
    float setpoint;         /* Desired temperature (°C)         */

    /* Output limits */
    float outMin;           /* Minimum output (default  0.0)    */
    float outMax;           /* Maximum output (default 999.0)   */

    /* Internal state */
    float integral;         /* Accumulated integral term        */
    float prevError;        /* Error from previous cycle        */
    float prevInput;        /* Input from previous cycle (for derivative-on-measurement) */

    /* Output */
    float output;           /* Last computed PWM value [0..999] */
} PID_t;

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * @brief  Initialise a PID instance.
 *
 * @param  pid       Pointer to PID_t struct.
 * @param  Kp        Proportional gain.
 * @param  Ki        Integral gain.
 * @param  Kd        Derivative gain.
 * @param  setpoint  Target temperature (°C).
 * @param  outMin    Minimum PWM output (typically 0).
 * @param  outMax    Maximum PWM output (typically 999).
 */
void PID_Init(PID_t *pid,
              float Kp, float Ki, float Kd,
              float setpoint,
              float outMin, float outMax);

/**
 * @brief  Update setpoint at runtime.
 *
 * @param  pid       Pointer to PID_t struct.
 * @param  setpoint  New target temperature (°C).
 */
void PID_SetSetpoint(PID_t *pid, float setpoint);

/**
 * @brief  Update tuning parameters at runtime.
 *
 * @param  pid  Pointer to PID_t struct.
 * @param  Kp   New proportional gain.
 * @param  Ki   New integral gain.
 * @param  Kd   New derivative gain.
 */
void PID_SetTunings(PID_t *pid, float Kp, float Ki, float Kd);

/**
 * @brief  Reset integral accumulator and internal state.
 *         Call when switching modes or after a long pause.
 *
 * @param  pid  Pointer to PID_t struct.
 */
void PID_Reset(PID_t *pid);

/**
 * @brief  Compute one PID cycle.
 *         Call periodically (every sampleTime ms, typically 1000 ms).
 *
 * @param  pid        Pointer to PID_t struct.
 * @param  input      Current measured temperature (°C).
 * @param  sampleTime Elapsed time since last call (seconds).
 *
 * @retval Computed PWM duty cycle clamped to [outMin .. outMax].
 */
float PID_Compute(PID_t *pid, float input, float sampleTime);

/**
 * @brief  Convert PID float output to TIM ARR-compatible uint32_t.
 *
 * @param  pid  Pointer to PID_t struct.
 * @retval PWM compare value as uint32_t.
 */
uint32_t PID_GetPWM(PID_t *pid);

#ifdef __cplusplus
}
#endif

#endif /* INC_PID_H_ */
