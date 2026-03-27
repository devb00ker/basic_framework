#include "remote_control.h"
#include "string.h"
#include "bsp_usart.h"
#include "memory.h"
#include "stdlib.h"
#include "daemon.h"
#include "bsp_log.h"
/******************************************/
#include "crc16.h"

#define REMOTE_CONTROL_FRAME_SIZE 18u // 遥控器接收的buffer大小

// 遥控器数据
static RC_ctrl_t rc_ctrl[2]; //[0]:当前数据TEMP,[1]:上一次的数据LAST.用于按键持续按下和切换的判断
static uint8_t rc_init_flag = 0; // 遥控器初始化标志位

// 遥控器拥有的串口实例,因为遥控器是单例,所以这里只有一个,就不封装了
static USARTInstance* rc_usart_instance;
static DaemonInstance* rc_daemon_instance;

/**
 *  judiciary_variability-------------------------------------------------------------------
 */
#define REMOTE_REFEREE_CONTROL_FRAME_SIZE 21u // 遥控器接收的buffer大小
#define FRAME_HEADER1       0xA9        // 帧头第1字节
#define FRAME_HEADER2       0x53        // 帧头第2字节
#define CRC_LEN             2           // CRC16 占2字节
#define DATA_BEFORE_CRC_LEN (REMOTE_REFEREE_CONTROL_FRAME_SIZE - CRC_LEN)  // 除了最后CRC之外的长度：19字节
// 裁判系统遥控器数据
static RC_Referee_ctrl_t rc_referee_ctrl[2]; //[0]:当前数据TEMP,[1]:上一次的数据LAST.用于按键持续按下和切换的判断
static uint8_t rc_referee_init_flag = 0; // 遥控器初始化标志位


static USARTInstance* rc_referee_usart_instance;
static DaemonInstance* rc_referee_daemon_instance;

/*--------------------------------------------------------------------------------------------*/

/**
 * @brief 矫正遥控器摇杆的值,超过660或者小于-660的值都认为是无效值,置0
 *
 */
static void RectifyRCjoystick()
{
    for (uint8_t i = 0; i < 5; ++i)
        if (abs(*(&rc_ctrl[TEMP].rc.rocker_l_ + i)) > 660)
            *(&rc_ctrl[TEMP].rc.rocker_l_ + i) = 0;
}

/**
 * @brief 遥控器数据解析
 *
 * @param sbus_buf 接收buffer
 */
static void sbus_to_rc(const uint8_t* sbus_buf)
{
    //=====================================================
    // 1. 摇杆通道数据解析 + 中心点偏移校正
    //=====================================================
    // 右水平摇杆 & 右竖直摇杆
    rc_ctrl[TEMP].rc.rocker_r_ = ((sbus_buf[0] | (sbus_buf[1] << 8)) & 0x07ff) - RC_CH_VALUE_OFFSET;
    rc_ctrl[TEMP].rc.rocker_r1 = (((sbus_buf[1] >> 3) | (sbus_buf[2] << 5)) & 0x07ff) - RC_CH_VALUE_OFFSET;

    // 左水平摇杆 & 左竖直摇杆
    rc_ctrl[TEMP].rc.rocker_l_ = (((sbus_buf[2] >> 6) | (sbus_buf[3] << 2) | (sbus_buf[4] << 10)) & 0x07ff) -
        RC_CH_VALUE_OFFSET;
    rc_ctrl[TEMP].rc.rocker_l1 = (((sbus_buf[4] >> 1) | (sbus_buf[5] << 7)) & 0x07ff) - RC_CH_VALUE_OFFSET;

    // 侧边拨动旋钮解析
    rc_ctrl[TEMP].rc.dial = ((sbus_buf[16] | (sbus_buf[17] << 8)) & 0x07FF) - RC_CH_VALUE_OFFSET;

    // 摇杆非线性/死区校正统一处理
    RectifyRCjoystick();

    //=====================================================
    // 2. 物理拨码开关解析 (左右两路开关量)
    //=====================================================
    rc_ctrl[TEMP].rc.switch_right = ((sbus_buf[5] >> 4) & 0x0003);       // 右侧2位开关
    rc_ctrl[TEMP].rc.switch_left  = ((sbus_buf[5] >> 4) & 0x000C) >> 2;  // 左侧2位开关

    //=====================================================
    // 3. 鼠标坐标轴 + 鼠标按键解析
    //=====================================================
    rc_ctrl[TEMP].mouse.x      = (sbus_buf[6] | (sbus_buf[7] << 8));   // 鼠标X左右轴
    rc_ctrl[TEMP].mouse.y      = (sbus_buf[8] | (sbus_buf[9] << 8));   // 鼠标Y前后轴
    rc_ctrl[TEMP].mouse.press_l= sbus_buf[12];                         // 鼠标左键按压标志
    rc_ctrl[TEMP].mouse.press_r= sbus_buf[13];                         // 鼠标右键按压标志

    //=====================================================
    // 4. 位域按键整体赋值 (16位原始按键打包)
    // 利用Union直接整体映射,无需逐位判断,节省开销
    //=====================================================
    *(uint16_t*)&rc_ctrl[TEMP].key[KEY_PRESS] = (uint16_t)(sbus_buf[14] | (sbus_buf[15] << 8));

    //=====================================================
    // 5. Ctrl组合按键状态筛选 & 清空逻辑
    //=====================================================
    if (rc_ctrl[TEMP].key[KEY_PRESS].ctrl)
        rc_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL] = rc_ctrl[TEMP].key[KEY_PRESS];
    else
        // 无Ctrl按下:整组Key_t内存清零,防止残留旧状态
        memset(&rc_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL], 0, sizeof(Key_t));

    //=====================================================
    // 6. Shift组合按键状态筛选 & 清空逻辑
    //=====================================================
    if (rc_ctrl[TEMP].key[KEY_PRESS].shift)
        rc_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT] = rc_ctrl[TEMP].key[KEY_PRESS];
    else
        // 无Shift按下:整组Key_t内存清零
        memset(&rc_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT], 0, sizeof(Key_t));

    //=====================================================
    // 7. 提取当前/上一帧所有按键组原始16位数值
    // 统一缓存,减少重复寻址,提升执行效率
    //=====================================================
    uint16_t key_now        = rc_ctrl[TEMP].key[KEY_PRESS].keys;
    uint16_t key_last       = rc_ctrl[LAST].key[KEY_PRESS].keys;
    uint16_t key_with_ctrl  = rc_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL].keys;
    uint16_t key_last_ctrl  = rc_ctrl[LAST].key[KEY_PRESS_WITH_CTRL].keys;
    uint16_t key_with_shift = rc_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT].keys;
    uint16_t key_last_shift = rc_ctrl[LAST].key[KEY_PRESS_WITH_SHIFT].keys;

    //=====================================================
    // 8. 逐位遍历16个按键 + 上升沿判定 + key_count计数
    // key_count[3][16]: [模式组][按键序号] 记录单次有效按下次数
    // 跳过bit4(shift) bit5(ctrl)本身,只统计功能按键
    // personal doc : 模仿上升沿 遍历键盘每一个按键 判断是否是第一次按下，触发一次 计数一次 未找到计数器清零
    //=====================================================
    for (uint16_t i = 0, j = 0x1; i < 16; j <<= 1, i++)
    {
        if (i == 4 || i == 5)
            continue;

        // 普通按键上升沿计数(无Ctrl/Shift叠加)
        if ((key_now & j) && !(key_last & j) && !(key_with_ctrl & j) && !(key_with_shift & j))
            rc_ctrl[TEMP].key_count[KEY_PRESS][i]++;

        // Ctrl组合键上升沿独立计数
        if ((key_with_ctrl & j) && !(key_last_ctrl & j))
            rc_ctrl[TEMP].key_count[KEY_PRESS_WITH_CTRL][i]++;

        // Shift组合键上升沿独立计数
        if ((key_with_shift & j) && !(key_last_shift & j))
            rc_ctrl[TEMP].key_count[KEY_PRESS_WITH_SHIFT][i]++;
    }

    //=====================================================
    // 9. 帧备份:当前帧存入历史帧,供下一帧边沿对比
    //=====================================================
    memcpy(&rc_ctrl[LAST], &rc_ctrl[TEMP], sizeof(RC_ctrl_t));
}
/**
 * @brief 对sbus_to_rc的简单封装,用于注册到bsp_usart的回调函数中
 *
 */
static void RemoteControlRxCallback()
{
    DaemonReload(rc_daemon_instance); // 先喂狗
    sbus_to_rc(rc_usart_instance->recv_buff); // 进行协议解析
}

/**
 * @brief 遥控器离线的回调函数,注册到守护进程中,串口掉线时调用
 *
 */
static void RCLostCallback(void* id)
{
    memset(rc_ctrl, 0, sizeof(rc_ctrl)); // 清空遥控器数据
    USARTServiceInit(rc_usart_instance); // 尝试重新启动接收
    LOGWARNING("[rc] remote control lost");
}

RC_ctrl_t* RemoteControlInit(UART_HandleTypeDef* rc_usart_handle)
{
    USART_Init_Config_s conf;
    conf.module_callback = RemoteControlRxCallback;
    conf.usart_handle = rc_usart_handle;
    conf.recv_buff_size = REMOTE_CONTROL_FRAME_SIZE;
    rc_usart_instance = USARTRegister(&conf);

    // 进行守护进程的注册,用于定时检查遥控器是否正常工作
    Daemon_Init_Config_s daemon_conf = {
        .reload_count = 10, // 100ms未收到数据视为离线,遥控器的接收频率实际上是1000/14Hz(大约70Hz)
        .callback = RCLostCallback,
        .owner_id = (void *)rc_daemon_instance, // 只有1个遥控器,不需要owner_id
    };
    rc_daemon_instance = DaemonRegister(&daemon_conf);

    rc_init_flag = 1;
    return rc_ctrl;
}

uint8_t RemoteControlIsOnline()
{
    if (rc_init_flag)
        return DaemonIsOnline(rc_daemon_instance);
    return 0;
}
/*****************************************referee_system******************************************/
//发送端每间隔14ms通过UART接口输出一帧21字节的数据   attention the Data frame endianness issue 大小端 小端模式
static void uart_to_rc(const uint8_t* uart_buf)
{
    // 右水平摇杆 & 右竖直摇杆
    rc_referee_ctrl[TEMP].rc.rocker_r_ = ((uart_buf[0] | (uart_buf[1] << 8)) & 0x07ffU) - RC_CH_VALUE_OFFSET;
    rc_referee_ctrl[TEMP].rc.rocker_r1 = (((uart_buf[1] >> 3) | (uart_buf[2] << 5)) & 0x07ffU) - RC_CH_VALUE_OFFSET;

    // 左水平摇杆 & 左竖直摇杆
    rc_referee_ctrl[TEMP].rc.rocker_l_ = (((uart_buf[2] >> 6) | (uart_buf[3] << 2) | (uart_buf[4] << 10)) & 0x07ffU) -
        RC_CH_VALUE_OFFSET;
    rc_referee_ctrl[TEMP].rc.rocker_l1 = (((uart_buf[4] >> 1) | (uart_buf[5] << 7)) & 0x07ffU) - RC_CH_VALUE_OFFSET;
    // 挡位切换开关 (位60~61，2位)
    rc_referee_ctrl[TEMP].rc.switch_gear = (uint8_t)((uart_buf[5] >> 4) & 0x03U);

    // 暂停按键 (位62，1位)
    rc_referee_ctrl[TEMP].rc.pause_press = (uint8_t)((uart_buf[5] >> 6) & 0x01U);

    // 自定义按键左 (位63，1位)
    rc_referee_ctrl[TEMP].rc.custom_button_left = (uint8_t)((uart_buf[5] >> 7) & 0x01U);

    // 自定义按键右 (位64，1位)
    rc_referee_ctrl[TEMP].rc.custom_button_right = (uint8_t)(uart_buf[6] & 0x01U);

    // 拨轮 (位65~75，11位)
    uint16_t raw_dial = (uint16_t)((uart_buf[6] >> 1) | (uart_buf[7] << 7)) & 0x07FFU;
    rc_referee_ctrl[TEMP].rc.dial = (int16_t)(raw_dial - RC_CH_VALUE_OFFSET);  // 匹配int16_t类型

    // 扳机键 (位76，1位)
    rc_referee_ctrl[TEMP].rc.trigger_button = (uint8_t)((uart_buf[7] >> 4) & 0x01U);

    // 摇杆非线性/死区校正统一处理
    for (uint8_t i = 0; i < 4; ++i)                             //rocker_r_       0
        if (abs(*(&rc_ctrl[TEMP].rc.rocker_l_ + i)) > 660)      //rocker_r1         1
            *(&rc_ctrl[TEMP].rc.rocker_l_ + i) = 0;             //rocker_l_             2
    if (abs(*(&rc_ctrl[TEMP].rc.dial )) > 660)          //rocker_l1             3
        *(&rc_ctrl[TEMP].rc.dial) = 0;                 //dial  8

    // ==============================================
    // 3. 鼠标数据解析（16位有符号，小端）
    // ==============================================
    // 鼠标X轴 (位80~95)
    rc_referee_ctrl[TEMP].mouse.x = (int16_t)(uart_buf[8] | (uart_buf[9] << 8));

    // 鼠标Y轴 (位96~111)
    rc_referee_ctrl[TEMP].mouse.y = (int16_t)(uart_buf[10] | (uart_buf[11] << 8));

    // 鼠标Z轴(滚轮) (位112~127)
    rc_referee_ctrl[TEMP].mouse.z = (int16_t)(uart_buf[12] | (uart_buf[13] << 8));

    // 鼠标按键 (位128~133)
    rc_referee_ctrl[TEMP].mouse.press_l = (uint8_t)(uart_buf[14] & 0x03U);    // 左键
    rc_referee_ctrl[TEMP].mouse.press_r = (uint8_t)((uart_buf[14] >> 2) & 0x03U); // 右键
    rc_referee_ctrl[TEMP].mouse.press_mid = (uint8_t)((uart_buf[14] >> 4) & 0x03U); // 中键

    // ==============================================
    // 4. 键盘按键解析（位域Union映射）
    // ==============================================
    // 键盘按键 (位136~151，16位小端)
    *(uint16_t*)&rc_referee_ctrl[TEMP].key[KEY_PRESS] = (uint16_t)(uart_buf[15] | (uart_buf[16] << 8));

    // ==============================================
    // 5. Ctrl/Shift组合按键筛选
    // ==============================================
    if (rc_referee_ctrl[TEMP].key[KEY_PRESS].ctrl)
        rc_referee_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL] = rc_referee_ctrl[TEMP].key[KEY_PRESS];
    else
        memset(&rc_referee_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL], 0, sizeof(Key_t));

    if (rc_referee_ctrl[TEMP].key[KEY_PRESS].shift)
        rc_referee_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT] = rc_referee_ctrl[TEMP].key[KEY_PRESS];
    else
        memset(&rc_referee_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT], 0, sizeof(Key_t));

    // ==============================================
    // 6. 按键上升沿计数
    // ==============================================
    uint16_t key_now        = rc_referee_ctrl[TEMP].key[KEY_PRESS].keys;
    uint16_t key_last       = rc_referee_ctrl[LAST].key[KEY_PRESS].keys;
    uint16_t key_with_ctrl  = rc_referee_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL].keys;
    uint16_t key_last_ctrl  = rc_referee_ctrl[LAST].key[KEY_PRESS_WITH_CTRL].keys;
    uint16_t key_with_shift = rc_referee_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT].keys;
    uint16_t key_last_shift = rc_referee_ctrl[LAST].key[KEY_PRESS_WITH_SHIFT].keys;

    for (uint16_t i = 0, j = 0x1; i < 16; j <<= 1, i++)
    {
        if (i == 4 || i == 5)
            continue;

        // 普通按键上升沿
        if ((key_now & j) && !(key_last & j) && !(key_with_ctrl & j) && !(key_with_shift & j))
            rc_referee_ctrl[TEMP].key_count[KEY_PRESS][i]++;

        // Ctrl组合键上升沿
        if ((key_with_ctrl & j) && !(key_last_ctrl & j))
            rc_referee_ctrl[TEMP].key_count[KEY_PRESS_WITH_CTRL][i]++;

        // Shift组合键上升沿
        if ((key_with_shift & j) && !(key_last_shift & j))
            rc_referee_ctrl[TEMP].key_count[KEY_PRESS_WITH_SHIFT][i]++;
    }


    // ==============================================
    // 8. 帧备份：当前帧存入历史帧
    // ==============================================
    memcpy(&rc_referee_ctrl[LAST], &rc_referee_ctrl[TEMP], sizeof(RC_Referee_ctrl_t));
}
static void RemoteControl_Referee_RxCallback()
{
    const uint8_t* buf=rc_referee_usart_instance->recv_buff;
    //Verify frame header
    if (buf[0] != FRAME_HEADER1 || buf[1] != FRAME_HEADER2)
        return;
    //Verify CRC
    uint16_t recv_crc = buf[19] | ((uint16_t)buf[20] << 8);

    uint16_t calc_crc = crc_16(buf, DATA_BEFORE_CRC_LEN);
    //CRC-16/CCITT-FALSE P(x)=x16+x12+x5+1 初始值为0xFFFF，无输入输出反转，无异或
    if (recv_crc == calc_crc)
    {
        DaemonReload(rc_referee_daemon_instance);        // 喂狗
        uart_to_rc(buf+2);                               // 协议解析
    }
}
static void RC_Referee_LostCallback(void* id)
{
    memset(rc_referee_ctrl, 0, sizeof(rc_referee_ctrl)); // 清空遥控器数据
    USARTServiceInit(rc_referee_usart_instance); // 尝试重新启动接收
    LOGWARNING("[rc] remote control lost");
}
RC_Referee_ctrl_t* RemoteControl_Referee_Init(UART_HandleTypeDef* rc_usart_handle)
{
    USART_Init_Config_s conf;
    conf.module_callback = RemoteControl_Referee_RxCallback;
    conf.usart_handle = rc_usart_handle;
    conf.recv_buff_size = REMOTE_REFEREE_CONTROL_FRAME_SIZE;
    rc_usart_instance = USARTRegister(&conf);

    // 进行守护进程的注册,用于定时检查遥控器是否正常工作
    Daemon_Init_Config_s daemon_conf = {
        .reload_count = 10, // 100ms未收到数据视为离线,遥控器的接收频率实际上是1000/14Hz(大约70Hz)
        .callback = RC_Referee_LostCallback,
        .owner_id =(void *)rc_referee_daemon_instance , // 只有1个遥控器,不需要owner_id
    };
    rc_referee_daemon_instance = DaemonRegister(&daemon_conf);

    rc_referee_init_flag = 1;
    return rc_referee_ctrl;
}
uint8_t RemoteControl_Referee_IsOnline()
{
    if (rc_referee_init_flag)
        return DaemonIsOnline(rc_referee_daemon_instance);
    return 0;
}
