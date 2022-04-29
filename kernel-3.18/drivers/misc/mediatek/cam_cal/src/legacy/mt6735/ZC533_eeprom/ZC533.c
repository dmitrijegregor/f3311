/*
 * @file  ZC533.c
 * @brief Driver of ZC533's eFlash, reference the code of GT24C16.
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>

#include "kd_camera_hw.h"
#include "cam_cal.h"
#include "cam_cal_define.h"

#include "ZC533.h"
/*#include <asm/system.h>  // for SMP */
#include <linux/dma-mapping.h>
#ifdef CONFIG_COMPAT
/* 64 bit */
#include <linux/fs.h>
#include <linux/compat.h>
#endif

//#define ZC533_GETDLT_DEBUG
#define ZC533_DEBUG

#ifdef ZC533_DEBUG
#define ZC533DB pr_debug
#else
#define ZC533DB(x, ...)
#endif

static DEFINE_SPINLOCK(ZC533Lock); /* for SMP */
static struct i2c_board_info ZC533_kd_dev __initdata = {I2C_BOARD_INFO(ZC533_DRVNAME, ZC533_DEVICE_ID >> 1)};

static dev_t        g_devno = MKDEV(ZC533_DEV_MAJOR_NUMBER, 0);
static struct cdev *g_pZC533_CharDrv;

static struct class *g_pZC533_class;
static atomic_t      g_ZC533atomic;

/*
 * iReadDataFromZC533
 *   Reads data from the device
 * @param [in] ui4_offset
 *     Start address.
 * @param [in] ui4_length
 *     How many bytes to be read.
 * @param [in, out] pinputdata
 *     The buffer.
 * @return 
 *     How many bytes are read.
 */
int iReadDataFromZC533(unsigned int ui4_offset, unsigned int ui4_length, unsigned char *pinputdata)
{
	char puSendCmd[2];/* = {(char)(ui4_offset & 0xFF) }; */
	unsigned short SampleOffset = (unsigned short)((ui4_offset) & (0x0000FFFF));
	short          loop[2];
	short          loopCount;
	short          MaxLoop = sizeof(loop)/sizeof(loop[0]);
	u8            *pBuff;
	u32            u4IncOffset = 0;
	int            i4RetValue = 0;
    
	pBuff = pinputdata;

	loop[0] = ((ui4_length >> 3) << 3); // read times of 8 bytes in the first loop
	loop[1] = ui4_length - loop[0];     // read the remaining bytes 

	ZC533DB("[ZC533] loop[0]=%d loop[1]=%d\n", (loop[0]) , (loop[1]));

	puSendCmd[0] = (char)(((SampleOffset + u4IncOffset) >> 8) & 0xFF);
	puSendCmd[1] = (char)((SampleOffset + u4IncOffset) & 0xFF);

	for (loopCount = 0; loopCount < MaxLoop; loopCount++) {
		do {
			if (8 <= loop[loopCount]) {

				ZC533DB("[ZC533]1 loopCount=%d loop[loopCount]=%d puSendCmd[0]=%x puSendCmd[1]=%x\n", loopCount , loop[loopCount], puSendCmd[0], puSendCmd[1]);
				i4RetValue = iReadRegI2C(puSendCmd , 2, (u8*)pBuff, 8 , ZC533_DEVICE_ID);
				if (i4RetValue != 0) {
					ZC533DB("[ZC533] I2C iReadData failed!!\n");
					return -1;
				}
				u4IncOffset += 8;
				loop[loopCount] -= 8;
				puSendCmd[0] = (char)(((SampleOffset + u4IncOffset) >> 8) & 0xFF);
				puSendCmd[1] = (char)((SampleOffset + u4IncOffset) & 0xFF);
				pBuff = pinputdata + u4IncOffset;
			} else if (0 < loop[loopCount]) {
				ZC533DB("[ZC533]2 loopCount=%d loop[loopCount]=%d puSendCmd[0]=%x puSendCmd[1]=%x\n", loopCount , loop[loopCount], puSendCmd[0], puSendCmd[1]);
				i4RetValue = iReadRegI2C(puSendCmd , 2, (u8*)pBuff, loop[loopCount], ZC533_DEVICE_ID);
				if (i4RetValue != 0) {
					ZC533DB("[ZC533] I2C iReadData failed!!\n");
					return -1;
				}
				u4IncOffset += loop[loopCount];
				loop[loopCount] -= loop[loopCount];
				puSendCmd[0] = (char)(((SampleOffset + u4IncOffset) >> 8) & 0xFF);
				puSendCmd[1] = (char)((SampleOffset + u4IncOffset) & 0xFF);
				pBuff = pinputdata + u4IncOffset;
			}
		} while (loop[loopCount] > 0);
	}
	return (int)u4IncOffset;
}

#ifdef CONFIG_COMPAT
static int compat_put_cal_info_struct(
            COMPAT_stCAM_CAL_INFO_STRUCT __user *data32,
            stCAM_CAL_INFO_STRUCT __user *data)
{
    compat_uptr_t p;
    compat_uint_t i;
    int err;

    err = get_user(i, &data->u4Offset);
    err |= put_user(i, &data32->u4Offset);
    err |= get_user(i, &data->u4Length);
    err |= put_user(i, &data32->u4Length);
    /* Assume pointer is not change */
#if 1
    err |= get_user(p, (compat_uptr_t *)&data->pu1Params);
    err |= put_user(p, &data32->pu1Params);
#endif
    return err;
}
static int compat_get_cal_info_struct(
            COMPAT_stCAM_CAL_INFO_STRUCT __user *data32,
            stCAM_CAL_INFO_STRUCT __user *data)
{
    compat_uptr_t p;
    compat_uint_t i;
    int err;

    err = get_user(i, &data32->u4Offset);
    err |= put_user(i, &data->u4Offset);
    err |= get_user(i, &data32->u4Length);
    err |= put_user(i, &data->u4Length);
    err |= get_user(p, &data32->pu1Params);
    err |= put_user(compat_ptr(p), &data->pu1Params);

    return err;
}

static long ZC533_ioctl_Compat(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long ret;
    COMPAT_stCAM_CAL_INFO_STRUCT __user *data32;
    stCAM_CAL_INFO_STRUCT __user *data;
    int err;
	ZC533DB("[ZC533] ZC533_ioctl_Compat,%p %p %x ioc size %d (%lu)\n",filp->f_op ,filp->f_op->unlocked_ioctl,cmd,_IOC_SIZE(cmd),COMPAT_CAM_CALIOC_G_READ);

    if (!filp->f_op || !filp->f_op->unlocked_ioctl)
        return -ENOTTY;

    ZC533DB("[ZC533] Handling cmd\n");
    switch (cmd) {

    case COMPAT_CAM_CALIOC_G_READ:
    {
        data32 = compat_ptr(arg);
        ZC533DB("[ZC533] Handling COMPAT_CAM_CALIOC_G_READ \n");
        data = compat_alloc_user_space(sizeof(*data));
        if (data == NULL) {
            ZC533DB("[ZC533] Failed to allocate user space!\n");
            return -EFAULT;
        }
        err = compat_get_cal_info_struct(data32, data);
        if (err) {
            ZC533DB("[ZC533] Failed to get_cal_info!\n");
            return err;
        }

        ret = filp->f_op->unlocked_ioctl(filp, CAM_CALIOC_G_READ,(unsigned long)data);
        err = compat_put_cal_info_struct(data32, data);


        if(err != 0)
            ZC533DB("[ZC533] compat_put_acdk_sensor_getinfo_struct failed\n");
        return ret;
    }
    default:
        return -ENOIOCTLCMD;
    }
}
#endif

/*
 * ZC533_ioctl
 *   Device I/O control handler.
 */

#define NEW_UNLOCK_IOCTL
#ifndef NEW_UNLOCK_IOCTL
static int ZC533_ioctl(struct inode *a_pstInode,
			struct file *a_pstFile,
			unsigned int a_u4Command,
			unsigned long a_u4Param)
#else
static long ZC533_ioctl(
	struct file *file,
	unsigned int a_u4Command,
	unsigned long a_u4Param
)
#endif
{
	int i4RetValue = 0;
	u8 *pBuff = NULL;
	u8 *pWorkingBuff = NULL;
	stCAM_CAL_INFO_STRUCT *ptempbuf;
	u8 readTryagain = 0;

#ifdef ZC533_GETDLT_DEBUG
	struct timeval ktv1, ktv2;
	unsigned long TimeIntervalUS;
#endif

	if (_IOC_NONE != _IOC_DIR(a_u4Command)) {
		pBuff = kmalloc(sizeof(stCAM_CAL_INFO_STRUCT), GFP_KERNEL);

		if (NULL == pBuff) {
			ZC533DB("[ZC533] ioctl allocate mem failed\n");
			return -ENOMEM;
		}

		if (_IOC_WRITE & _IOC_DIR(a_u4Command)) {
			if (copy_from_user((u8 *) pBuff , (u8 *) a_u4Param, sizeof(stCAM_CAL_INFO_STRUCT))) {
				/* get input structure address */
				kfree(pBuff);
				ZC533DB("[ZC533] ioctl copy from user failed\n");
				return -EFAULT;
			}
		}
	}

	ptempbuf = (stCAM_CAL_INFO_STRUCT *)pBuff;
	pWorkingBuff = kmalloc(ptempbuf->u4Length, GFP_KERNEL);
	if (NULL == pWorkingBuff) {
		kfree(pBuff);
		ZC533DB("[ZC533] ioctl allocate mem failed\n");
		return -ENOMEM;
	}
	ZC533DB("[ZC533] init Working buffer address 0x%p  command is 0x%8x\n", pWorkingBuff, (u32)a_u4Command);


	if (copy_from_user((u8 *)pWorkingBuff , (u8 *)ptempbuf->pu1Params, ptempbuf->u4Length)) {
		kfree(pBuff);
		kfree(pWorkingBuff);
		ZC533DB("[ZC533] ioctl copy from user failed\n");
		return -EFAULT;
	}

	switch (a_u4Command) {
	case CAM_CALIOC_S_WRITE:
		ZC533DB("[ZC533] Write CMD\n");
#ifdef ZC533_GETDLT_DEBUG
		do_gettimeofday(&ktv1);
#endif
		//i4RetValue = iWriteData((u16)ptempbuf->u4Offset, ptempbuf->u4Length, pWorkingBuff);
#ifdef ZC533_GETDLT_DEBUG
		do_gettimeofday(&ktv2);
		if (ktv2.tv_sec > ktv1.tv_sec)
			TimeIntervalUS = ktv1.tv_usec + 1000000 - ktv2.tv_usec;
		else
			TimeIntervalUS = ktv2.tv_usec - ktv1.tv_usec;

		ZC533DB("Write data %d bytes take %lu us\n", ptempbuf->u4Length, TimeIntervalUS);
#endif
		break;
	case CAM_CALIOC_G_READ:
		ZC533DB("[ZC533] Read CMD\n");
#ifdef ZC533_GETDLT_DEBUG
		do_gettimeofday(&ktv1);
#endif
		ZC533DB("[ZC533] offset %x\n", ptempbuf->u4Offset);
		ZC533DB("[ZC533] length %x\n", ptempbuf->u4Length);
		ZC533DB("[ZC533] Before read Working buffer address 0x%p\n", pWorkingBuff);

        readTryagain = 3;
        while (0 < readTryagain) {
            i4RetValue =  iReadDataFromZC533((u16)ptempbuf->u4Offset, ptempbuf->u4Length, pWorkingBuff);
            ZC533DB("[ZC533] error (%d) Read retry (%d)\n", i4RetValue, readTryagain);
            if (i4RetValue <= 0)
                readTryagain--;
            else
                readTryagain = 0;
        }

		ZC533DB("[ZC533] After read Working buffer data  0x%4x\n", *pWorkingBuff);

#ifdef ZC533_GETDLT_DEBUG
		do_gettimeofday(&ktv2);
		if (ktv2.tv_sec > ktv1.tv_sec)
			TimeIntervalUS = ktv1.tv_usec + 1000000 - ktv2.tv_usec;
		else
			TimeIntervalUS = ktv2.tv_usec - ktv1.tv_usec;

		ZC533DB("Read data %d bytes take %lu us\n", ptempbuf->u4Length, TimeIntervalUS);
#endif

		break;
	default:
		ZC533DB("[ZC533] No CMD\n");
		i4RetValue = -EPERM;
		break;
	}

	if (_IOC_READ & _IOC_DIR(a_u4Command)) {
		/* copy data to user space buffer, keep other input paremeter unchange. */
		ZC533DB("[ZC533] to user length %d\n", ptempbuf->u4Length);
		ZC533DB("[ZC533] to user  Working buffer address 0x%p\n", pWorkingBuff);
		if (copy_to_user((u8 __user *) ptempbuf->pu1Params , (u8 *)pWorkingBuff , ptempbuf->u4Length)) {
			kfree(pBuff);
			kfree(pWorkingBuff);
			ZC533DB("[ZC533] ioctl copy to user failed\n");
			return -EFAULT;
		}
	}

	kfree(pBuff);
	kfree(pWorkingBuff);
	return i4RetValue;
}


static u32 g_u4Opened;
/* #define */
/* Main jobs: */
/* 1.check for device-specified errors, device not ready. */
/* 2.Initialize the device if it is opened for the first time. */
static int ZC533_Open(struct inode *a_pstInode, struct file *a_pstFile)
{
	ZC533DB("[ZC533] ZC533_Open\n");
	spin_lock(&ZC533Lock);
	if (g_u4Opened) {
		spin_unlock(&ZC533Lock);
		return -EBUSY;
	} /*else {*//*LukeHu--150720=For check patch*/
	if (!g_u4Opened) {/*LukeHu--150720=For check patch*/
		g_u4Opened = 1;
		atomic_set(&g_ZC533atomic, 0);
	}
	spin_unlock(&ZC533Lock);

	return 0;
}

/* Main jobs: */
/* 1.Deallocate anything that "open" allocated in private_data. */
/* 2.Shut down the device on last close. */
/* 3.Only called once on last time. */
/* Q1 : Try release multiple times. */
static int ZC533_Release(struct inode *a_pstInode, struct file *a_pstFile)
{
	spin_lock(&ZC533Lock);

	g_u4Opened = 0;

	atomic_set(&g_ZC533atomic, 0);

	spin_unlock(&ZC533Lock);

	return 0;
}

static const struct file_operations g_stEEPROM_fops = {
	.owner = THIS_MODULE,
	.open = ZC533_Open,
	.release = ZC533_Release,
	/* .ioctl = ZC533_ioctl */
#ifdef CONFIG_COMPAT
    .compat_ioctl = ZC533_ioctl_Compat,
#endif
	.unlocked_ioctl = ZC533_ioctl
};

#define EEPROM_DYNAMIC_ALLOCATE_DEVNO 1
static inline int RegisterEEPROMCharDrv(void)
{
	struct device *EEPROM_device = NULL;

#if EEPROM_DYNAMIC_ALLOCATE_DEVNO
	if (alloc_chrdev_region(&g_devno, 0, 1, ZC533_DRVNAME)) {
		ZC533DB("[ZC533] Allocate device no failed\n");

		return -EAGAIN;
	}
#else
	if (register_chrdev_region(g_devno , 1 , ZC533_DRVNAME)) {
		ZC533DB("[ZC533] Register device no failed\n");

		return -EAGAIN;
	}
#endif

	/* Allocate driver */
	g_pZC533_CharDrv = cdev_alloc();

	if (NULL == g_pZC533_CharDrv) {
		unregister_chrdev_region(g_devno, 1);

		ZC533DB("[ZC533] Allocate mem for kobject failed\n");

		return -ENOMEM;
	}

	/* Attatch file operation. */
	cdev_init(g_pZC533_CharDrv, &g_stEEPROM_fops);

	g_pZC533_CharDrv->owner = THIS_MODULE;

	/* Add to system */
	if (cdev_add(g_pZC533_CharDrv, g_devno, 1)) {
		ZC533DB("[ZC533] Attatch file operation failed\n");

		unregister_chrdev_region(g_devno, 1);

		return -EAGAIN;
	}

	g_pZC533_class = class_create(THIS_MODULE, "ZC533_CAM_CALdrv");
	if (IS_ERR(g_pZC533_class)) {
		int ret = PTR_ERR(g_pZC533_class);
		ZC533DB("Unable to create class, err = %d\n", ret);
		return ret;
	}
	EEPROM_device = device_create(g_pZC533_class, NULL, g_devno, NULL, ZC533_DRVNAME);

	return 0;
}

static inline void UnregisterEEPROMCharDrv(void)
{
	/* Release char driver */
	cdev_del(g_pZC533_CharDrv);

	unregister_chrdev_region(g_devno, 1);

	device_destroy(g_pZC533_class, g_devno);
	class_destroy(g_pZC533_class);
}

/* platform structure */
static struct platform_driver g_stZC533_Driver = {
/*	.probe      = EEPROM_probe,
	.remove = EEPROM_remove,*/
	.driver     = {
		.name   = ZC533_DRVNAME,
		.owner  = THIS_MODULE,
	}
};


static struct platform_device g_ZC533_Device = {
	.name = ZC533_DRVNAME,
	.id = 0,
	.dev = {
	}
};

static int __init ZC533_init(void)
{
	i2c_register_board_info(ZC533_I2C_BUSNUM, &ZC533_kd_dev, 1);
	ZC533DB("ZC533_init\n");

	if (platform_device_register(&g_ZC533_Device)) {
		ZC533DB("failed to register ZC533 driver, 2nd time\n");
		return -ENODEV;
	}
    
    ZC533DB("ZC533_init step1\n");
	if (platform_driver_register(&g_stZC533_Driver)) {
		ZC533DB("failed to register ZC533 driver\n");
		return -ENODEV;
	}
	ZC533DB("ZC533_init step2\n");

	RegisterEEPROMCharDrv();

	return 0;
}

static void __exit ZC533_exit(void)
{
	platform_device_unregister(&g_ZC533_Device);

	platform_driver_unregister(&g_stZC533_Driver);

	UnregisterEEPROMCharDrv();
}

module_init(ZC533_init);
module_exit(ZC533_exit);

MODULE_DESCRIPTION("ZC533 driver");
MODULE_AUTHOR("Sean Lin <Sean.Lin@Mediatek.com>");
MODULE_LICENSE("GPL");


