#ifndef RADIOPROTOCOL_H_
#define RADIOPROTOCOL_H_

#include "stdint.h"
#include <time.h>
#include "easylink/EasyLink.h"

#define RADIO_CONCENTRATOR_ADDRESS     0x00
#define RADIO_EASYLINK_MODULATION     EasyLink_Phy_Custom

#define RADIO_PACKET_TYPE_ACK_PACKET             0
#define RADIO_PACKET_TYPE_ADC_SENSOR_PACKET      1
#define RADIO_PACKET_TYPE_DM_SENSOR_PACKET       2

struct PacketHeader {
    uint8_t sourceAddress;
    uint8_t packetType;
    uint8_t thirdAddress;
    uint32_t clock;
};

struct AdcSensorPacket {
    struct PacketHeader header;
    uint16_t adcValue;
};

struct DualModeSensorPacket {
    struct PacketHeader header;
    uint16_t adcValue;
    uint16_t batt;
    uint32_t time100MiliSec;
    uint8_t button;
    bool concLedToggle;
    uint8_t humidity1;
    uint8_t humidity2;
    uint8_t humidity3;
    uint8_t humidity4;
    uint8_t humidity5;
    uint8_t humidity6;
    uint32_t temperature;
};

struct AckPacket {
    struct PacketHeader header;
};

#endif /* RADIOPROTOCOL_H_ */
