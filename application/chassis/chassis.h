#ifndef CHASSIS_H
#define CHASSIS_H

#include <stdint.h>

/* 观察器接口，不改变现有板间/上位机协议。轮序LF、RF、LB、RB。 */
typedef struct
{
    uint32_t tick_ms;
    uint32_t feedback_fault_mask;
    uint32_t command_invalid;
    float dt_s;
    float input_vx, input_vy, ramp_vx, ramp_vy, ramp_wz;
    float wheel_ref_rpm[4];       /* 逻辑方向，已限幅 */
    float wheel_actual_rpm[4];    /* 已消除电机反向配置 */
    float wheel_scale;
    float translation_scale, spin_scale;
    uint32_t referee_status_fresh, referee_power_fresh;
    float power_target_w, power_budget_w, referee_measured_w;
    uint32_t buffer_j;
    float body_vx_mps, body_vy_mps, body_wz_radps;
} Chassis_Debug_s;

extern volatile Chassis_Debug_s chassis_debug;

/**
 * @brief 底盘应用初始化,请在开启rtos之前调用(目前会被RobotInit()调用)
 * 
 */
void ChassisInit();

/**
 * @brief 底盘应用任务,放入实时系统以一定频率运行
 * 
 */
void ChassisTask();

#endif // CHASSIS_H
