#ifndef _DRIVERS_ATA_H
#define _DRIVERS_ATA_H

#include <stdint.h>

#define ATA_PORT_DATA    0x1F0
#define ATA_PORT_ERROR   0x1F1
#define ATA_PORT_COUNT   0x1F2
#define ATA_PORT_LBA_LO  0x1F3
#define ATA_PORT_LBA_MID 0x1F4
#define ATA_PORT_LBA_HI  0x1F5
#define ATA_PORT_DRIVE   0x1F6
#define ATA_PORT_CMD     0x1F7
#define ATA_PORT_STATUS  0x1F7

#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08

void ataInit(void);
int  ataReadSector(uint32_t lba, uint8_t* buf);
int  ataWriteSector(uint32_t lba, const uint8_t* buf);

#endif