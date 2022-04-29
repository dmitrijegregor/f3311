/*
 * @file  ZC533.h
 * @brief Header file of ZC533's eFlash driver
 */

#ifndef _ZC533_H
#define _ZC533_H

#define ZC533_DEV_MAJOR_NUMBER 226
#define ZC533_DRVNAME           "CY33R2_CAM_CAL_DRV"

/* I2C */
#define ZC533_DEVICE_ID  0xA0
#define ZC533_I2C_BUSNUM 1

extern int iReadRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u8 *a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
extern int iWriteRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId);
extern int iReadReg(u16 a_u2Addr , u8 *a_puBuff , u16 i2cId);

#endif /* _ZC533_H */

