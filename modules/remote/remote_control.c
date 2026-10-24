#include "remote_control.h"
#include "string.h"
#include "bsp_usart.h"
#include "daemon.h"
#include "bsp_log.h"

#define REMOTE_CONTROL_FRAME_SIZE 21u
#define VT03_FRAME_SIZE 21u
#define VT03_CRC_DATA_SIZE 19u

static RC_ctrl_t rc_ctrl[2];
static uint8_t rc_init_flag;
static USARTInstance* rc_usart_instance;
static DaemonInstance* rc_daemon_instance;

/* Ozone watch variables, intentionally kept global and volatile. */
volatile uint32_t vt03_valid_frame_count;
volatile uint32_t vt03_crc_error_count;
volatile uint16_t vt03_received_crc;
volatile uint16_t vt03_calculated_crc;
volatile uint8_t vt03_raw_frame[VT03_FRAME_SIZE];
volatile int16_t vt03_channel_value[4];

static uint16_t VT03ReadBits(const uint8_t* data, uint16_t bit_offset, uint8_t bit_count)
{
    uint16_t value = 0;
    for (uint8_t i = 0; i < bit_count; ++i)
    {
        uint16_t bit = bit_offset + i;
        value |= (uint16_t)(((data[bit >> 3] >> (bit & 0x07U)) & 1U) << i);
    }
    return value;
}

/* DJI example: reflected polynomial 0x8408, init 0xFFFF, no final XOR. */
static uint16_t VT03CRC16(const uint8_t* data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    for (uint16_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0x8408U) : (uint16_t)(crc >> 1);
    }
    return crc;
}

static void VT03UpdateKeyState(uint16_t key_now)
{
    uint16_t key_last = rc_ctrl[LAST].key[KEY_PRESS].keys;
    rc_ctrl[TEMP].key[KEY_PRESS].keys = key_now;
    if (rc_ctrl[TEMP].key[KEY_PRESS].ctrl)
        rc_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL] = rc_ctrl[TEMP].key[KEY_PRESS];
    else
        memset(&rc_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL], 0, sizeof(Key_t));
    if (rc_ctrl[TEMP].key[KEY_PRESS].shift)
        rc_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT] = rc_ctrl[TEMP].key[KEY_PRESS];
    else
        memset(&rc_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT], 0, sizeof(Key_t));

    for (uint8_t i = 0; i < 16; ++i)
    {
        uint16_t mask = (uint16_t)(1U << i);
        if (i == 4U || i == 5U)
            continue;
        if ((key_now & mask) && !(key_last & mask) &&
            !(rc_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL].keys & mask) &&
            !(rc_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT].keys & mask))
            ++rc_ctrl[TEMP].key_count[KEY_PRESS][i];
        if ((rc_ctrl[TEMP].key[KEY_PRESS_WITH_CTRL].keys & mask) &&
            !(rc_ctrl[LAST].key[KEY_PRESS_WITH_CTRL].keys & mask))
            ++rc_ctrl[TEMP].key_count[KEY_PRESS_WITH_CTRL][i];
        if ((rc_ctrl[TEMP].key[KEY_PRESS_WITH_SHIFT].keys & mask) &&
            !(rc_ctrl[LAST].key[KEY_PRESS_WITH_SHIFT].keys & mask))
            ++rc_ctrl[TEMP].key_count[KEY_PRESS_WITH_SHIFT][i];
    }
}

static uint8_t VT03DecodeFrame(const uint8_t* frame)
{
    uint16_t channel_raw[4];
    if (frame[0] != 0xA9U || frame[1] != 0x53U)
        return 0;

    /* Save header-matched candidates, including CRC failures, for diagnosis. */
    for (uint8_t i = 0; i < VT03_FRAME_SIZE; ++i)
        vt03_raw_frame[i] = frame[i];
    uint16_t received_crc = (uint16_t)frame[19] | ((uint16_t)frame[20] << 8);
    uint16_t calculated_crc = VT03CRC16(frame, VT03_CRC_DATA_SIZE);
    vt03_received_crc = received_crc;
    vt03_calculated_crc = calculated_crc;
    if (received_crc != calculated_crc)
    {
        ++vt03_crc_error_count;
        return 0;
    }

    for (uint8_t i = 0; i < 4; ++i)
    {
        channel_raw[i] = VT03ReadBits(frame, (uint16_t)(16U + 11U * i), 11U);
        if (channel_raw[i] < RC_CH_VALUE_MIN || channel_raw[i] > RC_CH_VALUE_MAX)
            return 0;
    }

    if (VT03ReadBits(frame, 60U, 2U) > 2U)
        return 0;

    memcpy(&rc_ctrl[TEMP], &rc_ctrl[LAST], sizeof(RC_ctrl_t));
    rc_ctrl[TEMP].rc.rocker_r_ = (int16_t)channel_raw[0] - (int16_t)RC_CH_VALUE_OFFSET;
    rc_ctrl[TEMP].rc.rocker_r1 = (int16_t)channel_raw[1] - (int16_t)RC_CH_VALUE_OFFSET;
    rc_ctrl[TEMP].rc.rocker_l1 = (int16_t)channel_raw[2] - (int16_t)RC_CH_VALUE_OFFSET;
    rc_ctrl[TEMP].rc.rocker_l_ = (int16_t)channel_raw[3] - (int16_t)RC_CH_VALUE_OFFSET;

    rc_ctrl[TEMP].rc.mode_switch = (uint8_t)VT03ReadBits(frame, 60U, 2U);
    rc_ctrl[TEMP].rc.pause = (uint8_t)VT03ReadBits(frame, 62U, 1U);
    rc_ctrl[TEMP].rc.custom_button_left = (uint8_t)VT03ReadBits(frame, 63U, 1U);
    rc_ctrl[TEMP].rc.custom_button_right = (uint8_t)VT03ReadBits(frame, 64U, 1U);
    rc_ctrl[TEMP].rc.dial = (int16_t)VT03ReadBits(frame, 65U, 11U) - (int16_t)RC_CH_VALUE_OFFSET;
    rc_ctrl[TEMP].rc.trigger_button = (uint8_t)VT03ReadBits(frame, 76U, 1U);

    /* Map VT03's C/N/S switch to the existing command-layer switch convention. */
    if (rc_ctrl[TEMP].rc.mode_switch == 0U || rc_ctrl[TEMP].rc.pause)
        rc_ctrl[TEMP].rc.switch_right = RC_SW_UP;   // C / pause: emergency stop
    else if (rc_ctrl[TEMP].rc.mode_switch == 1U)
        rc_ctrl[TEMP].rc.switch_right = RC_SW_MID;  // N: sticks
    else if (rc_ctrl[TEMP].rc.mode_switch == 2U)
        rc_ctrl[TEMP].rc.switch_right = RC_SW_DOWN; // S: keyboard/mouse
    else
        return 0;
    rc_ctrl[TEMP].rc.switch_left = RC_SW_UP;

    rc_ctrl[TEMP].mouse.x = (int16_t)VT03ReadBits(frame, 80U, 16U);
    rc_ctrl[TEMP].mouse.y = (int16_t)VT03ReadBits(frame, 96U, 16U);
    rc_ctrl[TEMP].mouse.press_l = (uint8_t)VT03ReadBits(frame, 128U, 2U);
    rc_ctrl[TEMP].mouse.press_r = (uint8_t)VT03ReadBits(frame, 130U, 2U);
    VT03UpdateKeyState(VT03ReadBits(frame, 136U, 16U));

    for (uint8_t i = 0; i < 4; ++i)
        vt03_channel_value[i] = (int16_t)channel_raw[i] - (int16_t)RC_CH_VALUE_OFFSET;
    memcpy(&rc_ctrl[LAST], &rc_ctrl[TEMP], sizeof(RC_ctrl_t));
    ++vt03_valid_frame_count;
    return 1;
}

static void RemoteControlRxCallback(void)
{
    if (VT03DecodeFrame(rc_usart_instance->recv_buff))
        DaemonReload(rc_daemon_instance);
}

static void RCLostCallback(void* id)
{
    (void)id;
    memset(rc_ctrl, 0, sizeof(rc_ctrl));
    USARTServiceInit(rc_usart_instance);
    LOGWARNING("[rc] VT03 remote control lost");
}

RC_ctrl_t* RemoteControlInit(UART_HandleTypeDef* rc_usart_handle)
{
    USART_Init_Config_s conf = {
        .module_callback = RemoteControlRxCallback,
        .usart_handle = rc_usart_handle,
        .recv_buff_size = REMOTE_CONTROL_FRAME_SIZE,
    };
    rc_usart_instance = USARTRegister(&conf);

    Daemon_Init_Config_s daemon_conf = {
        .reload_count = 10,
        .callback = RCLostCallback,
        .owner_id = rc_usart_instance,
    };
    rc_daemon_instance = DaemonRegister(&daemon_conf);
    rc_init_flag = 1;
    return rc_ctrl;
}

uint8_t RemoteControlIsOnline(void)
{
    return (rc_init_flag && vt03_valid_frame_count) ? DaemonIsOnline(rc_daemon_instance) : 0U;
}
