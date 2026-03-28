#ifndef __CRC16_H
#define __CRC16_H

#include "main.h"

#define CRC_START_16 0xFFFF
#define CRC16_MODBUS 0
#define CRC16_CCITT_FALSE 1

uint16_t crc_16(const uint8_t* input_str, uint16_t num_bytes,uint8_t type);

#endif
