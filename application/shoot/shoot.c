#include "shoot.h"
#include "robot_def.h"

#include "dji_motor.h"
#include "message_center.h"
#include "bsp_dwt.h"
#include "general_def.h"
#include <math.h>

/* 对于双发射机构的机器人,将下面的数据封装成结构体即可,生成两份shoot应用实例 */
static DJIMotorInstance *friction_l, *friction_r, *loader; // 拨盘电机
// static servo_instance *lid; 需要增加弹舱盖+

static Publisher_t* shoot_pub;
static Shoot_Ctrl_Cmd_s shoot_cmd_recv; // 来自cmd的发射控制信息
static Subscriber_t* shoot_sub;
static Shoot_Upload_Data_s shoot_feedback_data; // 来自cmd的发射控制信息

// dwt定时,计算冷却用
static float hibernate_time = 0, dead_time = 0;

/* 拨弹盘卡弹保护参数：M2006反馈的实际电流和编码器速度用于判断堵转。 */
#define LOADER_JAM_CURRENT_THRESHOLD      1800.0f  // 正常约520，卡弹判定留出约3倍余量
#define LOADER_JAM_SPEED_THRESHOLD        1200.0f
#define LOADER_JAM_DETECT_TIME_MS           80.0f
#define LOADER_JAM_STARTUP_IGNORE_TIME_MS 250.0f
#define LOADER_RECOVER_REVERSE_TIME_MS     120.0f
#define LOADER_RECOVER_PAUSE_TIME_MS        40.0f
#define LOADER_RECOVER_FORWARD_TIME_MS     180.0f
#define LOADER_RECOVER_COOLDOWN_TIME_MS    300.0f
#define LOADER_RECOVER_REVERSE_RATIO         0.25f
#define LOADER_RECOVER_MAX_ATTEMPTS            3U

typedef enum
{
    LOADER_RECOVER_NORMAL = 0,
    LOADER_RECOVER_REVERSE,
    LOADER_RECOVER_PAUSE,
    LOADER_RECOVER_FORWARD,
    LOADER_RECOVER_COOLDOWN,
    LOADER_RECOVER_LOCKOUT,
} Loader_Recovery_State_e;

/* 这些变量可直接在Ozone中观察，便于现场调参。 */
static Loader_Recovery_State_e loader_recovery_state = LOADER_RECOVER_NORMAL;
static uint8_t loader_recovery_attempts = 0;
static float loader_jam_start_time = 0.0f;
static float loader_recovery_start_time = 0.0f;
static float loader_recovery_cooldown_start_time = 0.0f;
static float loader_burst_start_time = 0.0f;
static float loader_jam_current = 0.0f;
static float loader_jam_speed = 0.0f;

static void LoaderRecoveryReset(void)
{
    loader_recovery_state = LOADER_RECOVER_NORMAL;
    loader_recovery_attempts = 0;
    loader_jam_start_time = 0.0f;
    loader_recovery_start_time = 0.0f;
    loader_recovery_cooldown_start_time = 0.0f;
    loader_burst_start_time = 0.0f;
    loader_jam_current = 0.0f;
    loader_jam_speed = 0.0f;
}

static void LoaderSetBurstfireRef(float forward_ref)
{
    const float now = DWT_GetTimeline_ms();
    const float reverse_ref = -forward_ref * LOADER_RECOVER_REVERSE_RATIO;
    const float current = fabsf((float)loader->measure.real_current);
    const float speed = fabsf(loader->measure.speed_aps);
    const uint8_t jam_condition = (fabsf(forward_ref) > 1000.0f) &&
                                  (current >= LOADER_JAM_CURRENT_THRESHOLD) &&
                                  (speed <= LOADER_JAM_SPEED_THRESHOLD);

    loader_jam_current = current;
    loader_jam_speed = speed;

    if (loader_burst_start_time <= 0.0f)
        loader_burst_start_time = now;

    switch (loader_recovery_state)
    {
    case LOADER_RECOVER_NORMAL:
        DJIMotorOuterLoop(loader, SPEED_LOOP);
        DJIMotorSetRef(loader, forward_ref);
        if (jam_condition && (now - loader_burst_start_time) >= LOADER_JAM_STARTUP_IGNORE_TIME_MS)
        {
            if (loader_jam_start_time <= 0.0f)
                loader_jam_start_time = now;
            else if ((now - loader_jam_start_time) >= LOADER_JAM_DETECT_TIME_MS)
            {
                loader_jam_start_time = 0.0f;
                if (loader_recovery_attempts < LOADER_RECOVER_MAX_ATTEMPTS)
                {
                    loader_recovery_attempts++;
                    loader_recovery_state = LOADER_RECOVER_REVERSE;
                    loader_recovery_start_time = now;
                }
                else
                {
                    /* 多次修复失败后停盘，避免持续大电流损坏电机或电调。 */
                    loader_recovery_state = LOADER_RECOVER_LOCKOUT;
                }
            }
        }
        else
        {
            loader_jam_start_time = 0.0f;
        }
        break;

    case LOADER_RECOVER_REVERSE:
        DJIMotorOuterLoop(loader, SPEED_LOOP);
        DJIMotorSetRef(loader, reverse_ref);
        if ((now - loader_recovery_start_time) >= LOADER_RECOVER_REVERSE_TIME_MS)
        {
            loader_recovery_state = LOADER_RECOVER_PAUSE;
            loader_recovery_start_time = now;
        }
        break;

    case LOADER_RECOVER_PAUSE:
        DJIMotorOuterLoop(loader, SPEED_LOOP);
        DJIMotorSetRef(loader, 0.0f);
        if ((now - loader_recovery_start_time) >= LOADER_RECOVER_PAUSE_TIME_MS)
        {
            loader_recovery_state = LOADER_RECOVER_FORWARD;
            loader_recovery_start_time = now;
        }
        break;

    case LOADER_RECOVER_FORWARD:
        DJIMotorOuterLoop(loader, SPEED_LOOP);
        DJIMotorSetRef(loader, forward_ref);
        if ((now - loader_recovery_start_time) >= LOADER_RECOVER_FORWARD_TIME_MS)
        {
            loader_recovery_state = LOADER_RECOVER_COOLDOWN;
            loader_recovery_cooldown_start_time = now;
        }
        break;

    case LOADER_RECOVER_COOLDOWN:
        DJIMotorOuterLoop(loader, SPEED_LOOP);
        DJIMotorSetRef(loader, forward_ref);
        if ((now - loader_recovery_cooldown_start_time) >= LOADER_RECOVER_COOLDOWN_TIME_MS)
        {
            loader_recovery_state = LOADER_RECOVER_NORMAL;
            loader_jam_start_time = 0.0f;
        }
        break;

    case LOADER_RECOVER_LOCKOUT:
    default:
        DJIMotorOuterLoop(loader, SPEED_LOOP);
        DJIMotorSetRef(loader, 0.0f);
        break;
    }
}

void ShootInit()
{
    // 左摩擦轮 预测id为对应id号在底层已处理 无需关心标识符等，现象正确，但待证实
    Motor_Init_Config_s friction_config = {
        .can_init_config = {
            .can_handle = &hcan2,
        },
        .controller_param_init_config = {
            .speed_PID = {
                .Kp = 20, // 20
                .Ki = 0, // 1
                .Kd = 0,
                .Improve = PID_Integral_Limit,
                .IntegralLimit = 10000,
                .MaxOut = 15000,
            },
            .current_PID = {
                .Kp = 0.7, // 0.7
                .Ki = 0, // 0.1
                .Kd = 0,
                .Improve = PID_Integral_Limit,
                .IntegralLimit = 10000,
                .MaxOut = 15000,
            },
        },
        .controller_setting_init_config = {
            .angle_feedback_source = MOTOR_FEED,
            .speed_feedback_source = MOTOR_FEED,

            .outer_loop_type = SPEED_LOOP,
            .close_loop_type = SPEED_LOOP | CURRENT_LOOP,
            .motor_reverse_flag = MOTOR_DIRECTION_REVERSE,
        },
        .motor_type = M3508
    };
    friction_config.can_init_config.tx_id = 5,
        friction_l = DJIMotorInit(&friction_config);

    friction_config.can_init_config.tx_id = 6; // 右摩擦轮,改txid和方向就行
    friction_config.controller_setting_init_config.motor_reverse_flag = MOTOR_DIRECTION_NORMAL;
    friction_r = DJIMotorInit(&friction_config);

    // 拨盘电机
    Motor_Init_Config_s loader_config = {
        .can_init_config = {
            .can_handle = &hcan2,
            .tx_id = 7,
        },
        .controller_param_init_config = {
            .angle_PID = {
                // 如果启用位置环来控制发弹,需要较大的I值保证输出力矩的线性度否则出现接近拨出的力矩大幅下降
                .Kp = 12, // 10
                .Ki = 0,
                .Kd = 0,
                .MaxOut = 200,
            },
            .speed_PID = {
                .Kp = 10, // 10
                .Ki = 0, // 1
                .Kd = 0,
                .Improve = PID_Integral_Limit,
                .IntegralLimit = 5000,
                .MaxOut = 5000,
            },
            .current_PID = {
                .Kp = 0.7, // 0.7
                .Ki = 0, // 0.1
                .Kd = 0,
                .Improve = PID_Integral_Limit,
                .IntegralLimit = 5000,
                .MaxOut = 5000,
            },
        },
        .controller_setting_init_config = {
            .angle_feedback_source = MOTOR_FEED, .speed_feedback_source = MOTOR_FEED,
            .outer_loop_type = SPEED_LOOP, // 初始化成SPEED_LOOP,让拨盘停在原地,防止拨盘上电时乱转
            .close_loop_type = CURRENT_LOOP | SPEED_LOOP,
            .motor_reverse_flag = MOTOR_DIRECTION_REVERSE, // 注意方向设置为拨盘的拨出的击发方向
        },
        .motor_type = M2006 // 英雄使用m3508
    };
    loader = DJIMotorInit(&loader_config);

    shoot_pub = PubRegister("shoot_feed", sizeof(Shoot_Upload_Data_s));
    shoot_sub = SubRegister("shoot_cmd", sizeof(Shoot_Ctrl_Cmd_s));
}

/* 机器人发射机构控制核心任务 */
void ShootTask()
{
    // 从cmd获取控制数据
    SubGetMessage(shoot_sub, &shoot_cmd_recv);

    // 对shoot mode等于SHOOT_STOP的情况特殊处理,直接停止所有电机(紧急停止)
    if (shoot_cmd_recv.shoot_mode == SHOOT_OFF)
    {
        LoaderRecoveryReset();
        DJIMotorStop(friction_l);
        DJIMotorStop(friction_r);
        DJIMotorStop(loader);
    }
    else // 恢复运行
    {
        DJIMotorEnable(friction_l);
        DJIMotorEnable(friction_r);
        DJIMotorEnable(loader);
    }

    // 如果上一次触发单发或3发指令的时间加上不应期仍然大于当前时间(尚未休眠完毕),直接返回即可
    // 单发模式主要提供给能量机关激活使用(以及英雄的射击大部分处于单发)
    // if (hibernate_time + dead_time > DWT_GetTimeline_ms())
    //     return;

    // 若不在休眠状态,根据robotCMD传来的控制模式进行拨盘电机参考值设定和模式切换
    switch (shoot_cmd_recv.load_mode)
    {
    // 停止拨盘
    case LOAD_STOP:
        LoaderRecoveryReset();
        DJIMotorOuterLoop(loader, SPEED_LOOP); // 切换到速度环
        DJIMotorSetRef(loader, 0); // 同时设定参考值为0,这样停止的速度最快
        break;
    // 单发模式,根据鼠标按下的时间,触发一次之后需要进入不响应输入的状态(否则按下的时间内可能多次进入,导致多次发射)
    case LOAD_1_BULLET: // 激活能量机关/干扰对方用,英雄用.
        LoaderRecoveryReset();
        DJIMotorOuterLoop(loader, ANGLE_LOOP); // 切换到角度环
        DJIMotorSetRef(loader, loader->measure.total_angle + ONE_BULLET_DELTA_ANGLE); // 控制量增加一发弹丸的角度
        hibernate_time = DWT_GetTimeline_ms(); // 记录触发指令的时间
        dead_time = 150; // 完成1发弹丸发射的时间
        break;
    // 三连发,如果不需要后续可能删除
    case LOAD_3_BULLET:
        LoaderRecoveryReset();
        DJIMotorOuterLoop(loader, ANGLE_LOOP); // 切换到速度环
        DJIMotorSetRef(loader, loader->measure.total_angle + 3 * ONE_BULLET_DELTA_ANGLE); // 增加3发
        hibernate_time = DWT_GetTimeline_ms(); // 记录触发指令的时间
        dead_time = 300; // 完成3发弹丸发射的时间
        break;
    // 连发模式,对速度闭环,射频后续修改为可变,目前固定为1Hz
    case LOAD_BURSTFIRE:
        LoaderSetBurstfireRef(shoot_cmd_recv.shoot_rate * 360 * REDUCTION_RATIO_LOADER / 8);
    // x颗/秒换算成速度: 已知一圈的载弹量,由此计算出1s需要转的角度,注意换算角速度(DJIMotor的速度单位是angle per second)
        break;
    // 拨盘反转,对速度闭环,后续增加卡弹检测(通过裁判系统剩余热量反馈和电机电流)
    // 也有可能需要从switch-case中独立出来
    case LOAD_REVERSE:
        LoaderRecoveryReset();
        DJIMotorOuterLoop(loader, SPEED_LOOP);
        DJIMotorSetRef(loader, -1000.0f);
        break;
    default:
        while (1); // 未知模式,停止运行,检查指针越界,内存溢出等问题
    }

    // 确定是否开启摩擦轮,后续可能修改为键鼠模式下始终开启摩擦轮(上场时建议一直开启)
    if (shoot_cmd_recv.friction_mode == FRICTION_ON)
    {
        // 根据收到的弹速设置设定摩擦轮电机参考值,需实测后填入
        switch (shoot_cmd_recv.bullet_speed)
        {
        case SMALL_AMU_15:
            DJIMotorSetRef(friction_l, 27500);
            DJIMotorSetRef(friction_r, 27500);
            break;
        case SMALL_AMU_18:
            DJIMotorSetRef(friction_l, 35000);
            DJIMotorSetRef(friction_r, 35000);
            break;
        case SMALL_AMU_30:
            DJIMotorSetRef(friction_l, 25000);
            DJIMotorSetRef(friction_r, 25000);
            break;
        default: // 当前为了调试设定的默认值4000,因为还没有加入裁判系统无法读取弹速.
            DJIMotorSetRef(friction_l, 0);
            DJIMotorSetRef(friction_r, 0);
            break;
        }
    }
    else // 关闭摩擦轮
    {
        DJIMotorSetRef(friction_l, 0);
        DJIMotorSetRef(friction_r, 0);
    }

    // 开关弹舱盖
    if (shoot_cmd_recv.lid_mode == LID_CLOSE)
    {
        //...
    }
    else if (shoot_cmd_recv.lid_mode == LID_OPEN)
    {
        //...
    }

    // 反馈数据,目前暂时没有要设定的反馈数据,后续可能增加应用离线监测以及卡弹反馈
    PubPushMessage(shoot_pub, (void*)&shoot_feedback_data);
}
