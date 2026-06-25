/*
 * pid.c
 *
 *  Created on: Jun 24, 2026
 *      Author: ASUS
 */
#include "pid.h"

/* -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static float clamp(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void PID_Init(PID_t *pid,
              float Kp, float Ki, float Kd,
              float setpoint,
              float outMin, float outMax)
{
    pid->Kp        = Kp;
    pid->Ki        = Ki;
    pid->Kd        = Kd;
    pid->setpoint  = setpoint;
    pid->outMin    = outMin;
    pid->outMax    = outMax;
    pid->integral  = (outMin + outMax) * 0.5f;  // FIX: bắt đầu ở giữa
    pid->prevError = 0.0f;
    pid->prevInput = 0.0f;
    pid->output    = (outMin + outMax) * 0.5f;  // FIX: output ban đầu = giữa
}

void PID_SetSetpoint(PID_t *pid, float setpoint)
{
    pid->setpoint = setpoint;
}

void PID_SetTunings(PID_t *pid, float Kp, float Ki, float Kd)
{
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
}

void PID_Reset(PID_t *pid)
{
    pid->integral  = 0.0f;
    pid->prevError = 0.0f;
    pid->prevInput = 0.0f;
    pid->output    = 0.0f;
}

float PID_Compute(PID_t *pid, float input, float sampleTime)
{
    if (sampleTime <= 0.0f) return pid->output;

    // Cooling: error âm = quá nóng = cần output lớn
    // Đảo dấu để pTerm tăng khi nhiệt độ vượt setpoint
    float error = input - pid->setpoint;   // FIX: đổi chiều

    float pTerm = pid->Kp * error;

    pid->integral += pid->Ki * error * sampleTime;
    pid->integral  = clamp(pid->integral, pid->outMin, pid->outMax);

    float dInput  = (input - pid->prevInput) / sampleTime;
    float dTerm   = pid->Kd * dInput;     // FIX: bỏ dấu âm (đã đổi chiều error)

    pid->output   = clamp(pTerm + pid->integral + dTerm,
                          pid->outMin, pid->outMax);

    pid->prevError = error;
    pid->prevInput = input;

    return pid->output;
}

uint32_t PID_GetPWM(PID_t *pid)
{
    return (uint32_t)(pid->output + 0.5f);   /* round to nearest integer */
}



