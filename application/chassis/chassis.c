/**
 * @file chassis.c
 * @author NeoZeng neozng1@hnu.edu.cn
 * @brief 底盘应用,负责接收robot_cmd的控制命令并根据命令进行运动学解算,得到输出
 *        注意底盘采取右手系,对于平面视图,底盘纵向运动的正前方为x正方向;横向运动的右侧为y正方向
 *
 * @version 0.1
 * @date 2022-12-04
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "chassis.h"
#include "robot_def.h"
#include "power_control.h"
#include "super_cap.h"
#include "message_center.h"
#include "referee_task.h"

#include "general_def.h"
#include "bsp_dwt.h"
#include "referee_UI.h"
#include "arm_math.h"
#include "chassis_math.h"
#include "chassis_tuning.h"
#include "power_limit_math.h"

/* 根据robot_def.h中的macro自动计算的参数 */
#define HALF_WHEEL_BASE (WHEEL_BASE / 2.0f)     // 半轴距
#define HALF_TRACK_WIDTH (TRACK_WIDTH / 2.0f)   // 半轮距
#define PERIMETER_WHEEL (RADIUS_WHEEL * 2 * PI) // 轮子周长
#define sqrt_2 1.41421356237f                   // 根号2

/* 底盘应用包含的模块和信息存储,底盘是单例模式,因此不需要为底盘建立单独的结构体 */
static Publisher_t* chassis_pub; // 用于发布底盘的数据
static Subscriber_t* chassis_sub; // 用于订阅底盘的控制命令
static Chassis_Ctrl_Cmd_s chassis_cmd_recv; // 底盘接收到的控制命令
static Chassis_Upload_Data_s chassis_feedback_data; // 底盘回传的反馈数据

static referee_info_t* referee_data; // 用于获取裁判系统的数据
static Referee_Interactive_info_t ui_data; // UI数据，将底盘中的数据传入此结构体的对应变量中，UI会自动检测是否变化，对应显示UI 将当前状态显示至UI

static DJIMotorInstance *motor_lf, *motor_rf, *motor_lb, *motor_rb; // left right forward back

/* 用于自旋变速策略的时间变量 */
// static float t;

/* 私有函数计算的中介变量,设为静态避免参数传递的开销 */
static float chassis_vx, chassis_vy; // 将云台系的速度投影到底盘
static float vt_lf, vt_rf, vt_lb, vt_rb; // 底盘速度解算后的临时输出,待进行限幅
static float sin_theta, cos_theta;
static float vx, vy, wz;//正向解算 出的底盘姿态
volatile Chassis_Debug_s chassis_debug;
static float ramp_vx, ramp_vy, ramp_wz;
static uint32_t chassis_last_tick;
static float power_budget_w = CHASSIS_DEFAULT_POWER_W;
static float last_valid_limit_w = CHASSIS_DEFAULT_POWER_W;

void ChassisInit()
{
    // 四个轮子的参数一样,改tx_id和反转标志位即可
    Motor_Init_Config_s chassis_motor_config = {
        .can_init_config.can_handle = &hcan1,
        .controller_param_init_config = {
            .speed_PID = {
                .Kp = 20, // 4.5
                .Ki = 0, // 0
                .Kd = 0, // 0
                .IntegralLimit = 3000,
                .Improve = PID_Trapezoid_Intergral | PID_Integral_Limit | PID_Derivative_On_Measurement,
                .MaxOut = 15000,
                .Output_LPF_RC = 0.3,
            },
        },
        .controller_setting_init_config = {
            .angle_feedback_source = MOTOR_FEED,
            .speed_feedback_source = MOTOR_FEED,
            .outer_loop_type = SPEED_LOOP, // 设置为开环，电机设定值由下面的功率控制设定，不走普通的pid
            .close_loop_type = SPEED_LOOP,
        },
        .motor_type = M3508,
    };
    //  @todo: 当前还没有设置电机的正反转,仍然需要手动添加reference的正负号,需要电机module的支持,待修改.
    //使用功率控制的电机需要使用PowerControlInit()函数初始化,因为电机的控制方式不同
    chassis_motor_config.can_init_config.tx_id = 1;
    chassis_motor_config.controller_setting_init_config.motor_reverse_flag = MOTOR_DIRECTION_REVERSE;
    motor_lf = PowerControlInit(&chassis_motor_config);

    chassis_motor_config.can_init_config.tx_id = 2;
    chassis_motor_config.controller_setting_init_config.motor_reverse_flag = MOTOR_DIRECTION_NORMAL;
    motor_rf = PowerControlInit(&chassis_motor_config);

    chassis_motor_config.can_init_config.tx_id = 4;
    chassis_motor_config.controller_setting_init_config.motor_reverse_flag = MOTOR_DIRECTION_NORMAL;
    motor_lb = PowerControlInit(&chassis_motor_config);

    chassis_motor_config.can_init_config.tx_id = 3;
    chassis_motor_config.controller_setting_init_config.motor_reverse_flag = MOTOR_DIRECTION_REVERSE;
    motor_rb = PowerControlInit(&chassis_motor_config);

    referee_data = UITaskInit(&huart6, &ui_data); // 裁判系统初始化,会同时初始化UI

    // 发布订阅初始化,如果为双板,则需要can comm来传递消息
    chassis_sub = SubRegister("chassis_cmd", sizeof(Chassis_Ctrl_Cmd_s));
    chassis_pub = PubRegister("chassis_feed", sizeof(Chassis_Upload_Data_s));
}

#define LF_CENTER ((HALF_TRACK_WIDTH + CENTER_GIMBAL_OFFSET_X + HALF_WHEEL_BASE - CENTER_GIMBAL_OFFSET_Y) * DEGREE_2_RAD)  //v = w x r  在 rad/s时才成立
#define RF_CENTER ((HALF_TRACK_WIDTH - CENTER_GIMBAL_OFFSET_X + HALF_WHEEL_BASE - CENTER_GIMBAL_OFFSET_Y) * DEGREE_2_RAD)
#define LB_CENTER ((HALF_TRACK_WIDTH + CENTER_GIMBAL_OFFSET_X + HALF_WHEEL_BASE + CENTER_GIMBAL_OFFSET_Y) * DEGREE_2_RAD)
#define RB_CENTER ((HALF_TRACK_WIDTH - CENTER_GIMBAL_OFFSET_X + HALF_WHEEL_BASE + CENTER_GIMBAL_OFFSET_Y) * DEGREE_2_RAD)
/**
 * @brief 保留已实测的旧控制比例，输出交给转子RPM速度环。
 *        输入是历史标度控制量，不能直接按mm/s、deg/s解释。
 */
static void Calculate()
{
    const float t[4] = {(-chassis_vx + chassis_vy) / (sqrt_2 * RADIUS_WHEEL),
                       ( chassis_vx + chassis_vy) / (sqrt_2 * RADIUS_WHEEL),
                       (-chassis_vx - chassis_vy) / (sqrt_2 * RADIUS_WHEEL),
                       ( chassis_vx - chassis_vy) / (sqrt_2 * RADIUS_WHEEL)};
    const float r[4] = {chassis_cmd_recv.wz * LF_CENTER / RADIUS_WHEEL,
                      -chassis_cmd_recv.wz * RF_CENTER / RADIUS_WHEEL,
                      -chassis_cmd_recv.wz * LB_CENTER / RADIUS_WHEEL,
                       chassis_cmd_recv.wz * RB_CENTER / RADIUS_WHEEL};
    float wheel[4], translation_scale;
    chassis_debug.spin_scale = ChassisMixWheels(t, r, CHASSIS_WHEEL_MAX_RPM,
                                CHASSIS_SPIN_TRANSLATION_SHARE, wheel, &translation_scale);
    chassis_debug.translation_scale = translation_scale;
    vt_lf = wheel[0]; vt_rf = wheel[1]; vt_lb = wheel[2]; vt_rb = wheel[3];
}

/**
 * @brief 根据裁判系统和电容剩余容量对输出进行限制并设置电机参考值
 *
 */
static void LimitChassisOutput()
{
    RefereePowerSnapshot ref;
    RefereeGetPowerSnapshot(&ref);
    uint32_t now = HAL_GetTick();
    int status_fresh = PowerDataFresh(now, ref.status_tick, ref.status_seen, CHASSIS_STATUS_TIMEOUT_MS) &&
                       ref.limit_w > 0 && ref.limit_w <= CHASSIS_MAX_ACCEPTED_POWER_W;
    int power_fresh = PowerDataFresh(now, ref.power_tick, ref.power_seen, CHASSIS_POWER_TIMEOUT_MS) &&
                      isfinite(ref.measured_w) && ref.measured_w >= 0.0f;
    if (status_fresh) last_valid_limit_w = ref.limit_w;
    float base = status_fresh ? ref.limit_w : fminf(CHASSIS_DEFAULT_POWER_W, last_valid_limit_w);
    float target = PowerBudget(base, ref.buffer_j, status_fresh && power_fresh,
                               CHASSIS_BUFFER_FULL_J, CHASSIS_BUFFER_MIN_RATIO);
    power_budget_w = PowerBudgetSlew(power_budget_w, target, CHASSIS_POWER_RISE_WPS, chassis_debug.dt_s);
    SetPowerLimit(power_budget_w);
    chassis_debug.referee_status_fresh = status_fresh;
    chassis_debug.referee_power_fresh = power_fresh;
    chassis_debug.power_target_w = target;
    chassis_debug.power_budget_w = power_budget_w;
    chassis_debug.referee_measured_w = power_fresh ? ref.measured_w : 0.0f;
    chassis_debug.buffer_j = ref.buffer_j;

    float wheel[4] = {vt_lf, vt_rf, vt_lb, vt_rb};
    chassis_debug.wheel_scale = ChassisScaleWheels(wheel, CHASSIS_WHEEL_MAX_RPM);
    vt_lf = wheel[0]; vt_rf = wheel[1]; vt_lb = wheel[2]; vt_rb = wheel[3];
    for (unsigned i = 0; i < 4; ++i)
        chassis_debug.wheel_ref_rpm[i] = wheel[i];

    // 完成功率限制后进行电机参考输入设定
    DJIMotorSetRef(motor_lf, vt_lf);
    DJIMotorSetRef(motor_rf, vt_rf);
    DJIMotorSetRef(motor_lb, vt_lb);
    DJIMotorSetRef(motor_rb, vt_rb);
}

/**
 * @brief 根据每个轮子的速度反馈,计算底盘的实际运动速度,逆运动解算
 *        对于双板的情况,考虑增加来自底盘板IMU的数据
 *
 */
static void EstimateSpeed()//aps(REDUCTION_RATIO_WHEEL)减速比 从转子速度到电机速度 -> / 360.0f(aps->rps) -> *PERIMETER_WHEEL(rps->mm/s) [v=wr][PERIMETER_WHEEL=2*pi*r]{rps*2pi->rad/s*r->v} ->*1000(mm/s->m/s) 其余为解算系数
{
    DJIMotorInstance *motors[4] = {motor_lf, motor_rf, motor_lb, motor_rb};
    float wheel[4];
    /* 先还原逻辑方向，再将转子deg/s转换为轮缘mm/s。 */
    for (unsigned i = 0; i < 4; ++i)
    {
        float sign = motors[i]->motor_settings.motor_reverse_flag == MOTOR_DIRECTION_REVERSE ? -1.0f : 1.0f;
        float speed_aps = sign * motors[i]->measure.speed_aps;
        chassis_debug.wheel_actual_rpm[i] = speed_aps / 6.0f;
        wheel[i] = speed_aps / REDUCTION_RATIO_WHEEL / 360.0f * PERIMETER_WHEEL;
    }
    /* 四轮45度安装、旋转中心位于底盘中心时的轮缘有效力臂。 */
    const float arm[4] = {LF_CENTER / (DEGREE_2_RAD * sqrt_2), RF_CENTER / (DEGREE_2_RAD * sqrt_2),
                           LB_CENTER / (DEGREE_2_RAD * sqrt_2), RB_CENTER / (DEGREE_2_RAD * sqrt_2)};
    ChassisInverse(wheel, arm, &vx, &vy, &wz);
    vx /= 1000.0f;
    vy /= 1000.0f;
    chassis_debug.body_vx_mps = vx;
    chassis_debug.body_vy_mps = vy;
    chassis_debug.body_wz_radps = wz;
    chassis_feedback_data.real_vx = vx * cos_theta - vy * sin_theta;
    chassis_feedback_data.real_vy = +vx * sin_theta + vy * cos_theta;
}

/* 机器人底盘控制核心任务 */
void ChassisTask()
{
    SubGetMessage(chassis_sub, &chassis_cmd_recv);

    uint32_t now = HAL_GetTick();
    float dt = chassis_last_tick ? (uint32_t)(now - chassis_last_tick) * 0.001f : 0.005f;
    chassis_last_tick = now;
    if (dt > 0.020f) dt = 0.020f; // 调试暂停/任务延迟后不补一次大台阶
    chassis_debug.tick_ms = now;
    chassis_debug.dt_s = dt;
    chassis_debug.feedback_fault_mask = PowerControlFeedbackFaultMask();
    chassis_debug.command_invalid = !isfinite(chassis_cmd_recv.vx) || !isfinite(chassis_cmd_recv.vy) ||
                                    !isfinite(chassis_cmd_recv.offset_angle) ||
                                    chassis_cmd_recv.chassis_mode < CHASSIS_ZERO_FORCE ||
                                    chassis_cmd_recv.chassis_mode > CHASSIS_FOLLOW_GIMBAL_YAW;
    if (chassis_debug.feedback_fault_mask || chassis_debug.command_invalid)
        chassis_cmd_recv.chassis_mode = CHASSIS_ZERO_FORCE;

    if (chassis_cmd_recv.chassis_mode == CHASSIS_ZERO_FORCE)
    {
        // 如果出现重要模块离线或遥控器设置为急停,让电机停止
        DJIMotorStop(motor_lf);
        DJIMotorStop(motor_rf);
        DJIMotorStop(motor_lb);
        DJIMotorStop(motor_rb);
        ramp_vx = ramp_vy = ramp_wz = 0.0f; // 急停绕过斜坡，恢复时从零起步
        chassis_cmd_recv.vx = chassis_cmd_recv.vy = 0.0f;
    }
    else
    {
        // 正常工作
        DJIMotorEnable(motor_lf);
        DJIMotorEnable(motor_rf);
        DJIMotorEnable(motor_lb);
        DJIMotorEnable(motor_rb);
    }

    // 根据控制模式设定旋转速度
    switch (chassis_cmd_recv.chassis_mode)
    {
    case CHASSIS_NO_FOLLOW:
    case CHASSIS_FOLLOW_GIMBAL_YAW:
        // 保持旧版实际行为：枪口方向平移，不新增自动追云台旋转。
        chassis_cmd_recv.wz = 0.0f;
        break;
    case CHASSIS_ROTATE: // 自旋,同时保持全向机动;当前wz维持定值,后续增加不规则的变速策略
        chassis_cmd_recv.wz = CHASSIS_ROTATE_WZ;
        break;
    default:
        chassis_cmd_recv.wz = 0.0f;
        break;
    }

    ChassisLimitVector(&chassis_cmd_recv.vx, &chassis_cmd_recv.vy, CHASSIS_INPUT_MAX);
    chassis_debug.input_vx = chassis_cmd_recv.vx;
    chassis_debug.input_vy = chassis_cmd_recv.vy;
    ChassisSlewVector(&ramp_vx, &ramp_vy, chassis_cmd_recv.vx, chassis_cmd_recv.vy,
                      CHASSIS_INPUT_ACCEL, CHASSIS_INPUT_DECEL, dt);
    float spin_step = CHASSIS_SPIN_SLEW * dt;
    float spin_delta = chassis_cmd_recv.wz - ramp_wz;
    ramp_wz += fminf(fmaxf(spin_delta, -spin_step), spin_step);
    chassis_cmd_recv.wz = ramp_wz;
    chassis_debug.ramp_vx = ramp_vx;
    chassis_debug.ramp_vy = ramp_vy;
    chassis_debug.ramp_wz = ramp_wz;

    // 根据云台和底盘的角度offset将控制量映射到底盘坐标系上。
    // 实测表明云台顺时针转 +offset 时，底盘“前”命令方向应为 -offset，
    // 因此这里将云台系控制量按 -offset 投影到底盘系；反馈速度投影仍使用上面的正向角度。
    // 底盘逆时针旋转为角度正方向;云台命令的方向以云台指向的方向为x,采用右手系(x指向正北时y在正东)
    float offset = isfinite(chassis_cmd_recv.offset_angle) ? chassis_cmd_recv.offset_angle : 0.0f;
    cos_theta = arm_cos_f32(offset * DEGREE_2_RAD);
    sin_theta = arm_sin_f32(offset * DEGREE_2_RAD);
    chassis_vx = ramp_vx * cos_theta + ramp_vy * sin_theta;
    chassis_vy = -ramp_vx * sin_theta + ramp_vy * cos_theta;

    // 根据控制模式进行正运动学解算,计算底盘输出
    Calculate();

    // 根据裁判系统的反馈数据和电容数据对输出限幅并设定闭环参考值
    LimitChassisOutput();

    // 根据电机的反馈速度和IMU(如果有)计算真实速度
    EstimateSpeed();

    // // 获取裁判系统数据   建议将裁判系统与底盘分离，所以此处数据应使用消息中心发送
    // // 我方颜色id小于7是红色,大于7是蓝色,注意这里发送的是对方的颜色, 0:blue , 1:red
    chassis_feedback_data.enemy_color = referee_data->GameRobotState.robot_id > 7 ? 1 : 0;
    // // 当前只做了17mm热量的数据获取,后续根据robot_def中的宏切换双枪管和英雄42mm的情况
//     chassis_feedback_data.bullet_speed = referee_data->GameRobotState.shooter_id1_17mm_speed_limit;
//     chassis_feedback_data.rest_heat = referee_data->PowerHeatData.shooter_heat0;

    // 推送反馈消息
    PubPushMessage(chassis_pub, (void*)&chassis_feedback_data);
}
