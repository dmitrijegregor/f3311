/*
 * @file  GT24C64A.h
 * @brief Header file of GT24C64A EEPROM driver
 */

#ifndef __GT24C64A_H
#define __GT24C64A_H

#define GT24C64A_DEV_MAJOR_NUMBER 226
#define GT24C64A_DRVNAME           "CY33R1_CAM_CAL_DRV"

/* I2C */
#define GT24C64A_DEVICE_ID  0xA0
#define GT24C64A_I2C_BUSNUM 1

extern int iReadRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u8 *a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
extern int iWriteRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId);
extern int iReadReg(u16 a_u2Addr , u8 *a_puBuff , u16 i2cId);

#endif /* __GT24C64A_H */

