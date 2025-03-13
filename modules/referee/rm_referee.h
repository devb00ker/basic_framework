#ifndef RM_REFEREE_H
#define RM_REFEREE_H

#include "usart.h"
#include "referee_protocol.h"
#include "robot_def.h"
#include "bsp_usart.h"
#include "FreeRTOS.h"

extern uint8_t UI_Seq;//宏定义报序号 每发送一帧 +1

#pragma pack(1)
typedef struct
{
	uint8_t Robot_Color;		// 机器人颜色
	uint16_t Robot_ID;			// 本机器人ID
	uint16_t Cilent_ID;			// 本机器人对应的客户端ID
	uint16_t Receiver_Robot_ID; // 机器人车间通信时接收者的ID，必须和本机器人同颜色
} referee_id_t;

// 此结构体包含 裁判系统接收数据 以及 UI绘制 与 机器人车间通信 的相关信息 (三类数据) 裁判系统传来的数据
typedef struct
{
	referee_id_t referee_id; //机器人 和 客户端 通信标识信息

	xFrameHeader FrameHeader; // 接收到的帧头信息
	uint16_t CmdID;
    /*裁判系统接收数据*/
	ext_game_state_t GameState;							   // 0x0001
	ext_game_result_t GameResult;						   // 0x0002
	ext_game_robot_HP_t GameRobotHP;					   // 0x0003
	ext_event_data_t EventData;							   // 0x0101
	ext_supply_projectile_action_t SupplyProjectileAction; // 0x0102
    ext_robot_status_t GameRobotState;				       // 0x0201
	ext_power_heat_data_t PowerHeatData;				   // 0x0202
    ext_robot_pos_t GameRobotPos;					       // 0x0203
    ext_buff_t BuffMusk;							       // 0x0204
	ext_air_support_data_t AerialRobotEnergy;		       // 0x0205
    ext_hurt_data_t RobotHurt;							   // 0x0206
	ext_shoot_data_t ShootData;							   // 0x0207
    ext_projectile_allowance_t AllowanceProjectile;        // 0x0208
    ext_rfid_status_t RfidStatus;                          // 0x0209
    ext_dart_client_cmd_t DartClient;                      // 0x020A
    ext_ground_robot_position_t RobotPostion;              // 0x020B
    ext_radar_mark_data_t RadarMark;                       // 0x020C
    ext_sentry_info_t SentryInfo;                          // 0x020D
    ext_radar_info_t RadarInfo;                            // 0x020E
	// 自定义交互数据的接收机/器人车间通信
	Communicate_ReceiveData_t ReceiveData;

	uint8_t init_flag;

} referee_info_t;

// 模式是否切换标志位，0为未切换，1为切换，static定义默认为0
typedef struct
{
	uint32_t chassis_flag : 1;
	uint32_t gimbal_flag : 1;
	uint32_t shoot_flag : 1;
	uint32_t lid_flag : 1;
	uint32_t friction_flag : 1;
	uint32_t Power_flag : 1;
} Referee_Interactive_Flag_t;//UI

// 此结构体包含 UI绘制 与机 器人车间通信 的需要的其他   非裁判系统数据 车的状态数据 发送给裁判系统绘制UI
typedef struct
{
	Referee_Interactive_Flag_t Referee_Interactive_Flag;
	// 为UI绘制以及交互数据所用
	chassis_mode_e chassis_mode;			 // 底盘模式
	gimbal_mode_e gimbal_mode;				 // 云台模式
	shoot_mode_e shoot_mode;				 // 发射模式设置
	friction_mode_e friction_mode;			 // 摩擦轮关闭
	lid_mode_e lid_mode;					 // 弹舱盖打开
	Chassis_Power_Data_s Chassis_Power_Data; // 功率控制 待调整

	// 上一次的模式，用于flag判断
	chassis_mode_e chassis_last_mode;
	gimbal_mode_e gimbal_last_mode;
	shoot_mode_e shoot_last_mode;
	friction_mode_e friction_last_mode;
	lid_mode_e lid_last_mode;
	Chassis_Power_Data_s Chassis_last_Power_Data;

} Referee_Interactive_info_t;

#pragma pack()

/**
 * @brief 裁判系统通信初始化,该函数会初始化裁判系统串口,开启中断
 *
 * @param referee_usart_handle 串口handle,C板一般用串口6
 * @return referee_info_t* 返回裁判系统反馈的数据,包括热量/血量/状态等
 */
referee_info_t *RefereeInit(UART_HandleTypeDef *referee_usart_handle);

/**
 * @brief UI绘制和交互数的发送接口,由UI绘制任务和多机通信函数调用
 * @note 内部包含了一个实时系统的延时函数,这是因为裁判系统接收CMD数据至高位10Hz
 *
 * @param send 发送数据首地址
 * @param tx_len 发送长度
 */
void RefereeSend(uint8_t *send, uint16_t tx_len);

#endif // !REFEREE_H
