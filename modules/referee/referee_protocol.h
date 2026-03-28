/**
 * @file referee_protocol.h
 * @author kidneygood (you@domain.com)
 * @version 0.1
 * @date 2022-12-02
 *
 * @copyright Copyright (c) HNU YueLu EC 2022 all rights reserved
 *
 */

#ifndef referee_protocol_H
#define referee_protocol_H

#include "stdint.h"

/****************************宏定义部分****************************/

#define REFEREE_SOF 0xA5 // 起始字节,协议固定为0xA5
#define Robot_Red 0
#define Robot_Blue 1
#define Communicate_Data_LEN 5  // 自定义交互数据长度，该长度决定了我方发送和他方接收，自定义交互数据协议更改时只需要更改此宏定义即可

#pragma pack(1)

/****************************通信协议格式****************************/

/* 通信协议格式偏移，枚举类型,代替#define声明 */
typedef enum
{
	FRAME_HEADER_Offset = 0,
	CMD_ID_Offset = 5,
	DATA_Offset = 7,
} JudgeFrameOffset_e;//帧头偏移

/* 通信协议长度 */
typedef enum
{
	LEN_HEADER = 5, // 帧头长
	LEN_CMDID = 2,	// 命令码长度
	LEN_TAIL = 2,	// 帧尾CRC16

	LEN_CRC8 = 4, // 帧头CRC8校验长度=帧头+数据长+包序号 1+2+1
} JudgeFrameLength_e;

/****************************帧头****************************/
/****************************帧头****************************/

/* 帧头偏移 */
typedef enum
{
	SOF = 0,		 // 起始位
	DATA_LENGTH = 1, // 帧内数据长度,根据这个来获取数据长度
	SEQ = 3,		 // 包序号
	CRC8 = 4		 // CRC8
} FrameHeaderOffset_e;

/* 帧头定义 */
typedef struct
{
	uint8_t SOF;
	uint16_t DataLength;
	uint8_t Seq;
	uint8_t CRC8;
} xFrameHeader;

/****************************cmd_id命令码说明****************************/
/****************************cmd_id命令码说明****************************/

/* 命令码ID,用来判断接收的是什么数据 */
typedef enum
{													    // information				 lenth	| 协议原命名
    // ==================== 裁判系统链路 REF ====================
    REF_ID_game_state = 0x0001,				            // 比赛状态数据				11	| game_state
    REF_ID_game_result = 0x0002,			            // 比赛结果数据				1	| game_result
    REF_ID_game_robot_survivors = 0x0003,		        // 比赛机器人血量数据		16	| game_robot_survivors
    REF_ID_event_data = 0x0101,				            // 场地事件数据				4	| event_data
    REF_ID_referee_warning = 0x0104,                    // 裁判警告数据			    3	| referee_warning_t
    REF_ID_dart_info = 0x0105,                          // 飞镖发射相关数据			3	| dart_info_t
    REF_ID_robot_status = 0x0201,			            // 机器人性能体系数据		13	| robot_status_t
    REF_ID_power_heat_data = 0x0202,		            // 实时底盘功率和枪口热量数据14	| power_heat_data
    REF_ID_game_robot_pos = 0x0203,			        // 机器人位置数据			16	| game_robot_pos
    REF_ID_buff = 0x0204,					            // 机器人增益数据			8	| buff_t
    REF_ID_robot_hurt = 0x0206,				            // 伤害状态数据				1	| robot_hurt
    REF_ID_shoot_data = 0x0207,				            // 实时射击数据				7	| shoot_data
    REF_ID_projectile_allowance = 0x0208,               // 允许发弹量				6	| projectile_allowance
    REF_ID_rfid_status = 0x0209,                        // 机器人 RFID 模块状态		5	| rfid_status_t
    REF_ID_dart_client_cmd = 0x020A,                    // 飞镖选手端指令数据		6	| darts_client_cmd
    REF_ID_ground_robot_position = 0x020B,              // 地面机器人位置数据		40	| ground_robot_position_t
    REF_ID_radar_mark_data = 0x020C,                    // 雷达标记进度数据			2	| radar_mark_data_t
    REF_ID_sentry_info = 0x020D,                        // 哨兵自主决策信息同步		6	| sentry_info_t
    REF_ID_radar_info = 0x020E,                         // 雷达自主决策信息同步		1	| radar_info_t

    // ==================== 图传链路 VID ====================
    VID_ID_student_interactive = 0x0301,		        // 机器人间交互数据			118	| student_interactive
    VID_ID_custom_robot_data = 0x0302,		            // 自定义控制器与机器人交互	30	| custom_robot_data_t
    VID_ID_map_command = 0x0303,			            // 选手端小地图交互数据		15	| map_command_t
    VID_ID_map_robot_data = 0x0305,		                // 选手端小地图接收雷达数据	48	| map_robot_data_t
    VID_ID_custom_client_data = 0x0306,		            // 自定义控制器与选手端交互	8	| custom_client_data_t
    VID_ID_map_data = 0x0307,				            // 选手端小地图接收哨兵数据	103| map_data_t
    VID_ID_user_map_info_from_robot = 0x0308,	        // 选手端小地图接收机器人数据34	| user_map_info_from_robot_t
    VID_ID_robot_custom_data = 0x0309,                 // 自定义控制器接收机器人数据30 | robot_custom_data_t
    VID_ID_robot_to_client_data = 0x030A,              // 机器人发自定义客户端数据	300| robot_to_client_data_t
    VID_ID_client_to_robot_cmd = 0x030B,               // 自定义客户端发机器人指令	30 | client_to_robot_cmd_t

    // ==================== 雷达无线链路 RAD ====================
    RAD_ID_radar_opponent_pos = 0x0A01,		            // 雷达接收对方机器人坐标	24	| radar_opponent_pos_t
    RAD_ID_radar_opponent_HP = 0x0A02,		            // 雷达接收对方机器人血量	12	| radar_opponent_HP_t
    RAD_ID_radar_opponent_ammo = 0x0A03,		        // 雷达接收对方剩余发弹量	10	| radar_opponent_ammo_t
    RAD_ID_radar_opponent_global = 0x0A04,	            // 雷达接收对方宏观状态		8	| radar_opponent_global_t
    RAD_ID_radar_opponent_buff = 0x0A05,		        // 雷达接收对方增益效果		36	| radar_opponent_buff_t
    RAD_ID_radar_opponent_key = 0x0A06,		            // 雷达接收对方干扰波密钥	6	| radar_opponent_key_t

} CmdID_e;

/* 命令码数据段长,根据官方协议来定义长度，还有自定义数据长度 */
typedef enum
{
    // ==================== 裁判系统链路 REF ====================
    LEN_REF_game_state = 11,                                 // 0x0001  原命名：game_state
    LEN_REF_game_result = 1,                                 // 0x0002  原命名：game_result
    LEN_REF_game_robot_survivors = 16,                       // 0x0003  原命名：game_robot_survivors
    LEN_REF_event_data = 4,                                  // 0x0101  原命名：event_data
    LEN_REF_referee_warning = 3,                             // 0x0104  原命名：referee_warning_t
    LEN_REF_dart_info = 3,                                   // 0x0105  原命名：dart_info_t
    LEN_REF_robot_status = 13,                               // 0x0201  原命名：robot_status_t
    LEN_REF_power_heat_data = 14,                            // 0x0202  原命名：power_heat_data
    LEN_REF_game_robot_pos = 16,                             // 0x0203  原命名：game_robot_pos
    LEN_REF_buff = 8,                                        // 0x0204  原命名：buff_t
    LEN_REF_robot_hurt = 1,                                  // 0x0206  原命名：robot_hurt
    LEN_REF_shoot_data = 7,                                  // 0x0207  原命名：shoot_data
    LEN_REF_projectile_allowance = 6,                        // 0x0208  原命名：projectile_allowance
    LEN_REF_rfid_status = 5,                                 // 0x0209  原命名：rfid_status_t
    LEN_REF_dart_client_cmd = 6,                             // 0x020A  原命名：darts_client_cmd
    LEN_REF_ground_robot_position = 40,                      // 0x020B  原命名：ground_robot_position_t
    LEN_REF_radar_mark_data = 2,                             // 0x020C  原命名：radar_mark_data_t
    LEN_REF_sentry_info = 6,                                 // 0x020D  原命名：sentry_info_t
    LEN_REF_radar_info = 1,                                  // 0x020E  原命名：radar_info_t

    // ==================== 图传链路 VID ====================
    aLEN_VID_student_interactive = 118,                       // 0x0301  原命名：student_interactive
    LEN_VID_custom_robot_data = 30,                          // 0x0302  原命名：custom_robot_data_t
    LEN_VID_map_command = 15,                                // 0x0303  原命名：map_command_t
    LEN_VID_map_robot_data = 48,                             // 0x0305  原命名：map_robot_data_t
    LEN_VID_custom_client_data = 8,                          // 0x0306  原命名：custom_client_data_t
    LEN_VID_map_data = 103,                                  // 0x0307  原命名：map_data_t
    LEN_VID_user_map_info_from_robot = 34,                   // 0x0308  原命名：user_map_info_from_robot_t
    LEN_VID_robot_custom_data = 30,                          // 0x0309  原命名：robot_custom_data_t
    LEN_VID_robot_to_client_data = 300,                      // 0x030A  原命名：robot_to_client_data_t
    LEN_VID_client_to_robot_cmd = 30,                       // 0x030B  原命名：client_to_robot_cmd_t

    // ==================== 雷达无线链路 RAD ====================
    LEN_RAD_radar_opponent_pos = 24,		                 // 0x0A01  原命名：radar_opponent_pos_t
    LEN_RAD_radar_opponent_HP = 12,		                     // 0x0A02  原命名：radar_opponent_HP_t
    LEN_RAD_radar_opponent_ammo = 10,		                 // 0x0A03  原命名：radar_opponent_ammo_t
    LEN_RAD_radar_opponent_global = 8,	                     // 0x0A04  原命名：radar_opponent_global_t
    LEN_RAD_radar_opponent_buff = 36,		                 // 0x0A05  原命名：radar_opponent_buff_t
    LEN_RAD_radar_opponent_key = 6,		                     // 0x0A06  原命名：radar_opponent_key_t

} JudgeDataLength_e;
/*
              */
/****************************接收数据的详细说明****************************/
/****************************接收数据的详细说明****************************/
// 协议通用宏定义：紧凑结构体（避免编译器字节对齐） memcpy方式赋值如果不取消字节填充 就会存在空字节从而导致失帧
// =========================================================
// 【1. 常规链路结构体】（裁判系统服务器/主控模块转发，电源管理模块User串口收发）
// =========================================================
// 比赛状态数据（0x0001）- 固定1Hz，服务器→全体机器人
typedef struct {
	uint8_t game_type:4;        // bit0-3：比赛类型（超级对抗赛/单项赛等）
	uint8_t game_progress:4;    // bit4-7：比赛阶段（未开始/准备/比赛中等）
	uint16_t stage_remain_time; // 当前阶段剩余时间（秒）
	uint64_t SyncTimeStamp;     // UNIX时间（连接NTP服务器后生效）
} game_status_t;
// 比赛结果数据（0x0002）- 比赛结束触发，服务器→全体机器人
typedef struct {
	uint8_t winner;  // 结果：0=平局 1=红方胜利 2=蓝方胜利
} game_result_t;
// 机器人血量数据（0x0003）- 固定3Hz，服务器→全体机器人
typedef struct {
	uint16_t ally_1_robot_HP;   // 己方1号英雄血量
	uint16_t ally_2_robot_HP;   // 己方2号工程血量
	uint16_t ally_3_robot_HP;   // 己方3号步兵血量
	uint16_t ally_4_robot_HP;   // 己方4号步兵血量
	uint16_t reserved;          // 保留位
	uint16_t ally_7_robot_HP;   // 己方7号哨兵血量
	uint16_t ally_outpost_HP;   // 己方前哨站血量
	uint16_t ally_base_HP;      // 己方基地血量
} game_robot_HP_t;

// 场地事件数据（0x0101）- 固定1Hz，服务器→己方全体机器人
typedef struct {
	uint32_t event_data;        // 场地事件综合bit域（补给区占领/能量机关状态等）
} event_data_t;

// 裁判警告数据（0x0104）- 触发+1Hz，服务器→被判罚方全体机器人
typedef struct {
	uint8_t level;              // 判罚等级（1=双方黄牌 2=黄牌 3=红牌 4=判负）
	uint8_t offending_robot_id; // 违规机器人ID（红1=1/蓝1=101）
	uint8_t count;              // 对应等级违规次数
} referee_warning_t;
// 飞镖发射相关数据（0x0105）- 固定1Hz，服务器→己方全体机器人
typedef struct {
	uint8_t dart_remaining_time; // 飞镖发射剩余时间（秒）
	uint16_t dart_info;          // 飞镖状态bit域（击中目标/选定目标等）
} dart_info_t;

// 机器人性能体系数据（0x0201）- 固定10Hz，主控模块→对应机器人
typedef struct {
	uint8_t robot_id;                   // 本机器人ID
	uint8_t robot_level;                // 机器人等级
	uint16_t current_HP;                // 当前血量
	uint16_t maximum_HP;                // 血量上限
	uint16_t shooter_barrel_cooling_value; // 射击热量冷却值（每秒）
	uint16_t shooter_barrel_heat_limit; // 射击热量上限
	uint16_t chassis_power_limit;       // 底盘功率上限
	uint8_t power_management_gimbal_output:1;  // gimbal口24V输出（1=开启）
	uint8_t power_management_chassis_output:1; // chassis口24V输出（1=开启）
	uint8_t power_management_shooter_output:1; // shooter口24V输出（1=开启）
} robot_status_t;

// 底盘缓冲能量和射击热量数据（0x0202）- 固定10Hz，主控模块→对应机器人
typedef struct {
	uint16_t reserved1;         // 保留位
	uint16_t reserved2;         // 保留位
	float reserved3;            // 保留位
	uint16_t buffer_energy;     // 底盘缓冲能量（J）
	uint16_t shooter_17mm_barrel_heat; // 17mm发射机构热量
	uint16_t shooter_42mm_barrel_heat; // 42mm发射机构热量
} power_heat_data_t;

// 机器人位置数据（0x0203）- 固定1Hz，主控模块→对应机器人
typedef struct {
	float x;        // x坐标（m）
	float y;        // y坐标（m）
	float angle;    // 测速模块朝向（度，正北为0）
} robot_pos_t;

// 机器人增益和底盘能量数据（0x0204）- 固定3Hz，服务器→对应机器人
typedef struct {
	uint8_t recovery_buff;      // 回血增益（百分比）
	uint16_t cooling_buff;      // 热量冷却增益（x/s）
	uint8_t defence_buff;       // 防御增益（百分比）
	uint8_t vulnerability_buff; // 负防御增益（百分比）
	uint16_t attack_buff;       // 攻击增益（百分比）
	uint8_t remaining_energy;   // 剩余能量比例bit域
} buff_t;

// 伤害状态数据（0x0206）- 伤害触发，主控模块→对应机器人
typedef struct {
	uint8_t armor_id:4;         // 装甲/测速模块ID（非对应原因扣血为0）
	uint8_t HP_deduction_reason:4; // 扣血类型（0=弹丸攻击 1=模块离线 5=撞击）
} hurt_data_t;


// 实时射击数据（0x0207）- 弹丸发射触发，主控模块→对应机器人
typedef struct {
	uint8_t bullet_type;        // 弹丸类型（bit1=17mm bit2=42mm）
	uint8_t shooter_number;     // 发射机构ID（1=17mm 3=42mm）
	uint8_t launching_frequency;// 射速（Hz）
	float initial_speed;        // 初速度（m/s）
} shoot_data_t;

// 允许发弹量（0x0208）- 固定10Hz，服务器→己方英雄/步兵/哨兵/空中机器人
typedef struct {
	uint16_t projectile_allowance_17mm; // 17mm允许发弹量
	uint16_t projectile_allowance_42mm; // 42mm允许发弹量
	uint16_t remaining_gold_coin;       // 剩余金币数
	uint16_t projectile_allowance_fortress;//堡垒增益点提供的储备17mm弹丸允许发弹量;该值与机器人是否实际占领堡垒无关
} projectile_allowance_t;

// 机器人RFID模块状态（0x0209）- 固定3Hz，服务器→己方带RFID模块机器人
typedef struct {
	uint32_t rfid_status;       // 己方增益点RFID检测bit域（32位）
	uint8_t rfid_status_2;      // 对方隧道类增益点RFID检测bit域（8位）
} rfid_status_t;

// 飞镖选手端指令数据（0x020A）- 固定3Hz，服务器→己方飞镖机器人
typedef struct {
	uint8_t dart_launch_opening_status; // 发射站状态（0=开启 1=关闭 2=开关中）
	uint8_t reserved;                   // 保留位
	uint16_t target_change_time;        // 切换目标时比赛剩余时间（秒）
	uint16_t latest_launch_cmd_time;    // 最后一次发射指令时间（秒）
} dart_client_cmd_t;

// 地面机器人位置数据（0x020B）- 固定1Hz，服务器→己方哨兵机器人
typedef struct {
    float hero_x;       // 己方英雄x坐标（m）
    float hero_y;       // 己方英雄y坐标（m）
    float engineer_x;   // 己方工程x坐标（m）
    float engineer_y;   // 己方工程y坐标（m）
    float standard_3_x; // 己方3号步兵x坐标（m）
    float standard_3_y; // 己方3号步兵y坐标（m）
    float standard_4_x; // 己方4号步兵x坐标（m）
    float standard_4_y; // 己方4号步兵y坐标（m）
    float reserved1;    // 保留位
    float reserved2;    // 保留位
} ground_robot_position_t;

// 雷达标记进度数据（0x020C）- 固定1Hz，服务器→己方雷达机器人
typedef struct {
    uint16_t mark_progress;     // 标记进度&易伤状态bit域（16位）
} radar_mark_data_t;

// 哨兵自主决策信息同步（0x020D）- 固定1Hz，服务器→己方哨兵机器人
typedef struct {
    uint32_t sentry_info;       // 哨兵核心信息bit域（32位）
    uint16_t sentry_info_2;     // 哨兵扩展信息bit域（16位）
} sentry_info_t;

// 雷达自主决策信息同步（0x020E）- 固定1Hz，服务器→己方雷达机器人
typedef struct {
    uint8_t radar_info;         // 雷达核心信息bit域（8位）
} radar_info_t;

// 机器人交互数据（0x0301）- 触发式（上限30Hz），机器人间/机器人→选手端
typedef struct {
    uint16_t data_cmd_id;       // 子内容ID（0x0100/0x0101等）
    uint16_t sender_id;         // 发送者ID
    uint16_t receiver_id;       // 接收者ID（仅限己方）
    uint8_t user_data[112];     // 内容数据段（最大112字节）
} robot_interaction_data_t;

// 选手端小地图交互数据（0x0303）- 触发式，选手端→服务器→己方机器人
typedef struct {
    float target_position_x;    // 目标x坐标（m，发送ID时为0）
    float target_position_y;    // 目标y坐标（m，发送ID时为0）
    uint8_t cmd_keyboard;       // 按下的键盘键值（无则为0）
    uint8_t target_robot_id;    // 对方机器人ID（发送坐标时为0）
    uint16_t cmd_source;        // 信息来源ID
} map_command_t;

// 选手端小地图接收雷达数据（0x0305）- 上限5Hz，雷达→服务器→己方选手端
typedef struct {
    uint16_t opponent_hero_position_x; // 对方英雄x坐标（cm）
    uint16_t opponent_hero_position_y; // 对方英雄y坐标（cm）
    uint16_t opponent_engineer_position_x; // 对方工程x坐标（cm）
    uint16_t opponent_engineer_position_y; // 对方工程y坐标（cm）
    uint16_t opponent_infantry_3_position_x; // 对方3号步兵x坐标（cm）
    uint16_t opponent_infantry_3_position_y; // 对方3号步兵y坐标（cm）
    uint16_t opponent_infantry_4_position_x; // 对方4号步兵x坐标（cm）
    uint16_t opponent_infantry_4_position_y; // 对方4号步兵y坐标（cm）
    uint16_t opponent_aerial_position_x; // 对方空中机器人x坐标（cm）
    uint16_t opponent_aerial_position_y; // 对方空中机器人y坐标（cm）
    uint16_t opponent_sentry_position_x; // 对方哨兵x坐标（cm）
    uint16_t opponent_sentry_position_y; // 对方哨兵y坐标（cm）
    uint16_t ally_hero_position_x; // 己方英雄x坐标（cm）
    uint16_t ally_hero_position_y; // 己方英雄y坐标（cm）
    uint16_t ally_engineer_position_x; // 己方工程x坐标（cm）
    uint16_t ally_engineer_position_y; // 己方工程y坐标（cm）
    uint16_t ally_infantry_3_position_x; // 己方3号步兵x坐标（cm）
    uint16_t ally_infantry_3_position_y; // 己方3号步兵y坐标（cm）
    uint16_t ally_infantry_4_position_x; // 己方4号步兵x坐标（cm）
    uint16_t ally_infantry_4_position_y; // 己方4号步兵y坐标（cm）
    uint16_t ally_aerial_position_x; // 己方空中机器人x坐标（cm）
    uint16_t ally_aerial_position_y; // 己方空中机器人y坐标（cm）
    uint16_t ally_sentry_position_x; // 己方哨兵x坐标（cm）
    uint16_t ally_sentry_position_y; // 己方哨兵y坐标（cm）
} map_robot_data_t;

// 选手端小地图接收路径数据（0x0307）- 上限1Hz，哨兵/半自动机器人→对应选手端
typedef struct {
    uint8_t intention;          // 意图（1=攻击 2=防御 3=移动）
    uint16_t start_position_x;  // 路径起点x坐标（dm）
    uint16_t start_position_y;  // 路径起点y坐标（dm）
    int8_t delta_x[49];         // x轴增量数组（49个点位）
    int8_t delta_y[49];         // y轴增量数组（49个点位）
    uint16_t sender_id;         // 发送者ID
} map_data_t;

// 选手端小地图接收机器人数据（0x0308）- 上限3Hz，己方机器人→己方选手端
typedef struct {
    uint16_t sender_id;         // 发送者ID（需校验）
    uint16_t receiver_id;       // 接收者ID（仅限己方选手端）
    uint8_t user_data[30];      // 自定义消息（utf-16编码，支持中文）
} custom_info_t;




// =========================================================
// 【2. 图传链路结构体】（裁判系统选手端/图传模块转发，图传发送端串口收发）
// =========================================================
// 自定义控制器→机器人交互数据（0x0302）- 触发式（上限30Hz）
typedef struct {
	uint8_t data[30];           // 自定义数据（30字节）
} custom_robot_data_t;

// 机器人→自定义控制器交互数据（0x0309）- 上限10Hz
typedef struct {
	uint8_t data[30];           // 自定义数据（30字节）
} robot_custom_data_t;

// 机器人→自定义客户端数据（0x0310）- 上限50Hz
typedef struct {
	uint8_t data[300];          // 自定义数据（300字节）
} robot_custom_data_2_t;

// 自定义客户端→机器人指令（0x0311）- 上限75Hz
typedef struct {
	uint8_t data[30];           // 自定义指令（30字节）
} robot_custom_cmd_t;

// 自定义控制器→选手端交互数据（0x0306）- 触发式（上限30Hz，非链路数据但依赖图传链路）
typedef struct {
	uint16_t key_value;         // 键盘键值（bit0-7=按键1，bit8-15=按键2）
	uint16_t x_position:12;     // 鼠标X轴像素位置（1920×1080分辨率）
	uint16_t mouse_left:4;      // 鼠标左键状态（1=按下）
	uint16_t y_position:12;     // 鼠标Y轴像素位置
	uint16_t mouse_right:4;     // 鼠标右键状态（1=按下）
	uint16_t reserved;          // 保留位
} custom_client_data_t;

/****************************机器人交互数据****************************/
/****************************机器人交互数据****************************/
/* 发送的内容数据段最大为 112 检测是否超出大小限制?实际上图形段不会超，数据段最多30个，也不会超//0x0200~0x02FF */
/* 交互数据头结构 */
/* ID: 0x0301  Byte:  6+ Communicate_Data_LEN    机器人间交互数据 */
typedef struct
{
	uint16_t data_cmd_id; // 由于存在多个内容 ID，但整个cmd_id 上行频率最大为 10Hz，请合理安排带宽。注意交互部分的上行频率
	uint16_t sender_ID;
	uint16_t receiver_ID;
    //uint8_t user_data[Communicate_Data_LEN]; 自定义数据
} ext_robot_interaction_data_t;
/* 机器人id */
typedef enum
{
	// 红方机器人ID
	RobotID_RHero = 1,
	RobotID_REngineer = 2,
	RobotID_RStandard1 = 3,
	RobotID_RStandard2 = 4,
	RobotID_RStandard3 = 5,
	RobotID_RAerial = 6,
	RobotID_RSentry = 7,
    RobotID_Rdart =8,
	RobotID_RRadar = 9,
	// 蓝方机器人ID
	RobotID_BHero = 101,
	RobotID_BEngineer = 102,
	RobotID_BStandard1 = 103,
	RobotID_BStandard2 = 104,
	RobotID_BStandard3 = 105,
	RobotID_BAerial = 106,
	RobotID_BSentry = 107,
    RobotID_Bdart =108,
	RobotID_BRadar = 109,
} Robot_ID_e;

/* 交互数据ID */
typedef enum
{
	UI_Data_ID_Del = 0x100,
	UI_Data_ID_Draw1 = 0x101,
	UI_Data_ID_Draw2 = 0x102,
	UI_Data_ID_Draw5 = 0x103,
	UI_Data_ID_Draw7 = 0x104,
	UI_Data_ID_DrawChar = 0x110,
    UI_Data_ID_sentry=0x120,
    UI_Data_ID_radar=0x121,
	/* 自定义交互数据部分 */
	Communicate_Data_ID = 0x0200,//0x0200~0x02FF

} Interactive_Data_ID_e;
/* 交互数据长度 */
typedef enum
{
	Interactive_Data_LEN_Head = 6,
	UI_Operate_LEN_Del = 2,
	UI_Operate_LEN_PerDraw = 15,
	UI_Operate_LEN_DrawChar = 15 + 30,
    UI_Operate_LEN_Decision_sentry=4,
    UI_Operate_LEN_Decision_radar=1,
	/* 自定义交互数据部分 */
	 //Communicate_Data_LEN = 5,//自定义0x0200~0x02FF

} Interactive_Data_Length_e;

/****************************自定义交互数据****************************/
/*
	学生机器人间通信 cmd_id 0x0301，内容 ID:0x0200~0x02FF
	自定义交互数据 机器人间通信：0x0301。
	发送频率：上限 10Hz
*/
// 自定义交互数据协议，可更改，更改后需要修改最上方宏定义数据长度的值
typedef struct
{
	uint8_t data[Communicate_Data_LEN]; // 数据段,n需要小于113
} robot_interactive_data_t;

// 机器人交互信息_发送
typedef struct
{
	xFrameHeader FrameHeader;
	uint16_t CmdID;
	ext_robot_interaction_data_t datahead;
	robot_interactive_data_t Data; // 数据段
	uint16_t frametail;
} Communicate_SendData_t;
// 机器人交互信息_接收
typedef struct
{
	ext_robot_interaction_data_t datahead;
	robot_interactive_data_t Data; // 数据段
} Communicate_ReceiveData_t;

/****************************UI交互数据****************************/



/* 图形数据 */
typedef struct
{
	uint8_t graphic_name[3];
	uint32_t operate_tpye : 3;
	uint32_t figure_tpye : 3;
	uint32_t layer : 4;
	uint32_t color : 4;
	uint32_t details_a : 9;
	uint32_t details_b : 9;
	uint32_t width : 10;
	uint32_t start_x : 11;
	uint32_t start_y : 11;
    uint32_t details_c: 10;
    uint32_t details_d: 11;
    uint32_t details_e: 11;
} interaction_figure_t;

typedef struct
{
	interaction_figure_t Graph_Control;
	uint8_t show_Data[30];
} String_Data_t; // 打印字符串数据

/* 删除操作 */
typedef enum
{
	UI_Data_Del_NoOperate = 0,
	UI_Data_Del_Layer = 1,
	UI_Data_Del_ALL = 2, // 删除全部图层，后面的参数已经不重要了。
} UI_Delete_Operate_e;

/* 图形配置参数__图形操作 */
typedef enum
{
	UI_Graph_ADD = 1,
	UI_Graph_Change = 2,
	UI_Graph_Del = 3,
} UI_Graph_Operate_e;

/* 图形配置参数__图形类型 */
typedef enum
{
	UI_Graph_Line = 0,		// 直线
	UI_Graph_Rectangle = 1, // 矩形
	UI_Graph_Circle = 2,	// 整圆
	UI_Graph_Ellipse = 3,	// 椭圆
	UI_Graph_Arc = 4,		// 圆弧
	UI_Graph_Float = 5,		// 浮点型
	UI_Graph_Int = 6,		// 整形
	UI_Graph_Char = 7,		// 字符型

} UI_Graph_Type_e;

/* 图形配置参数__图形颜色 */
typedef enum
{
	UI_Color_Main = 0, // 红蓝主色
	UI_Color_Yellow = 1,
	UI_Color_Green = 2,
	UI_Color_Orange = 3,
	UI_Color_Purplish_red = 4, // 紫红色
	UI_Color_Pink = 5,
	UI_Color_Cyan = 6, // 青色
	UI_Color_Black = 7,
	UI_Color_White = 8,

} UI_Graph_Color_e;

typedef  struct//哨兵自主决策指令：0x0120
{
    uint32_t sentry_cmd;
} sentry_cmd_t;

typedef struct//雷达自主决策指令：0x0121
{
    uint8_t radar_cmd;
} radar_cmd_t;

#pragma pack()

#endif
