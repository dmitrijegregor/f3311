/*
 * @file  GT24C64A.c
 * @brief Driver of GT24C64A EEPROM, reference the code of GT24C16.
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

#include "GT24c64a.h"
/*#include <asm/system.h>  // for SMP */
#include <linux/dma-mapping.h>
#ifdef CONFIG_COMPAT
/* 64 bit */
#include <linux/fs.h>
#include <linux/compat.h>
#endif

//#define GT24C64A_GETDLT_DEBUG
#define GT24C64A_DEBUG

#ifdef GT24C64A_DEBUG
#define GT24C64ADB pr_debug
#else
#define GT24C64ADB(x, ...)
#endif

static DEFINE_SPINLOCK(GT24C64ALock); /* for SMP */
static struct i2c_board_info GT24C64A_kd_dev __initdata = {I2C_BOARD_INFO(GT24C64A_DRVNAME, GT24C64A_DEVICE_ID >> 1)};

static dev_t        g_devno = MKDEV(GT24C64A_DEV_MAJOR_NUMBER, 0);
static struct cdev *g_pGT24C64A_CharDrv;

static struct class *GT24C64A_class;
static atomic_t      g_GT24C64Aatomic;

/*
 * iReadDataFromGT24c64a
 *   Reads data from GT24C64A
 * @param [in] ui4_offset
 *     Start address.
 * @param [in] ui4_length
 *     How many bytes to be read.
 * @param [in, out] pinputdata
 *     The buffer.
 * @return 
 *     How many bytes are read.
 */
int iReadDataFromGT24c64a(unsigned int ui4_offset, unsigned int ui4_length, unsigned char *pinputdata)
{
	char puSendCmd[2];/* = {(char)(ui4_offset & 0xFF) }; */
	unsigned short SampleOffset = (unsigned short)((ui4_offset) & (0x0000FFFF));
	unsigned short EEPROM_Address[2] = {0xA0, 0xA0};
	short          loop[2];
	short          loopCount;
    short          MaxLoop = sizeof(loop)/sizeof(loop[0]);
	u8            *pBuff;
	u32            u4IncOffset = 0;
	int            i4RetValue = 0;
    
	pBuff = pinputdata;

	loop[0] = ((ui4_length >> 3) << 3); // read times of 8 bytes in the first loop
	loop[1] = ui4_length - loop[0];     // read the remaining bytes 

	GT24C64ADB("[GT24C64A] loop[0]=%d loop[1]=%d\n", (loop[0]) , (loop[1]));

	puSendCmd[0] = (char)(((SampleOffset + u4IncOffset) >> 8) & 0xFF);
	puSendCmd[1] = (char)((SampleOffset + u4IncOffset) & 0xFF);

	for (loopCount = 0; loopCount < MaxLoop; loopCount++) {
		do {
			if (8 <= loop[loopCount]) {

				GT24C64ADB("[GT24C64A]1 loopCount=%d loop[loopCount]=%d puSendCmd[0]=%x puSendCmd[1]=%x, EEPROM(%x)\n", loopCount , loop[loopCount], puSendCmd[0], puSendCmd[1], EEPROM_Address[loopCount]);
				i4RetValue = iReadRegI2C(puSendCmd , 2, (u8*)pBuff, 8 ,EEPROM_Address[loopCount]);
				if (i4RetValue != 0) {
					GT24C64ADB("[GT24C64A] I2C iReadData failed!!\n");
					return -1;
				}
				u4IncOffset += 8;
				loop[loopCount] -= 8;
				puSendCmd[0] = (char)(((SampleOffset + u4IncOffset) >> 8) & 0xFF);
				puSendCmd[1] = (char)((SampleOffset + u4IncOffset) & 0xFF);
				pBuff = pinputdata + u4IncOffset;
			} else if (0 < loop[loopCount]) {
				GT24C64ADB("[GT24C64A]2 loopCount=%d loop[loopCount]=%d puSendCmd[0]=%x puSendCmd[1]=%x\n", loopCount , loop[loopCount], puSendCmd[0], puSendCmd[1]);
				i4RetValue = iReadRegI2C(puSendCmd , 2, (u8*)pBuff, loop[loopCount], EEPROM_Address[loopCount]);
				if (i4RetValue != 0) {
					GT24C64ADB("[GT24C64A] I2C iReadData failed!!\n");
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

static long GT24c64a_Ioctl_Compat(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long ret;
    COMPAT_stCAM_CAL_INFO_STRUCT __user *data32;
    stCAM_CAL_INFO_STRUCT __user *data;
    int err;
    GT24C64ADB("[GT24C64A] COMPAT_CAM_CALIOC_G_READ\n");
	GT24C64ADB("[GT24C64A] GT24c64a_Ioctl_Compat,%p %p %x ioc size %d\n",filp->f_op ,filp->f_op->unlocked_ioctl,cmd,_IOC_SIZE(cmd) );

    if (!filp->f_op || !filp->f_op->unlocked_ioctl)
        return -ENOTTY;

    switch (cmd) {

    case COMPAT_CAM_CALIOC_G_READ:
    {
        data32 = compat_ptr(arg);
        data = compat_alloc_user_space(sizeof(*data));
        if (data == NULL)
            return -EFAULT;

        err = compat_get_cal_info_struct(data32, data);
        if (err)
            return err;

        ret = filp->f_op->unlocked_ioctl(filp, CAM_CALIOC_G_READ,(unsigned long)data);
        err = compat_put_cal_info_struct(data32, data);


        if(err != 0)
            GT24C64ADB("[GT24C64A] compat_put_acdk_sensor_getinfo_struct failed\n");
        return ret;
    }
    default:
        return -ENOIOCTLCMD;
    }
}
#endif

/*
 * GT24C64A_ioctl
 *   Device I/O control handler.
 */

#define NEW_UNLOCK_IOCTL
#ifndef NEW_UNLOCK_IOCTL
static int GT24C64A_ioctl(struct inode *a_pstInode,
			struct file *a_pstFile,
			unsigned int a_u4Command,
			unsigned long a_u4Param)
#else
static long GT24C64A_ioctl(
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

#ifdef GT24C64A_GETDLT_DEBUG
	struct timeval ktv1, ktv2;
	unsigned long TimeIntervalUS;
#endif

	if (_IOC_NONE != _IOC_DIR(a_u4Command)) {
		pBuff = kmalloc(sizeof(stCAM_CAL_INFO_STRUCT), GFP_KERNEL);

		if (NULL == pBuff) {
			GT24C64ADB("[GT24C64A] ioctl allocate mem failed\n");
			return -ENOMEM;
		}

		if (_IOC_WRITE & _IOC_DIR(a_u4Command)) {
			if (copy_from_user((u8 *) pBuff , (u8 *) a_u4Param, sizeof(stCAM_CAL_INFO_STRUCT))) {
				/* get input structure address */
				kfree(pBuff);
				GT24C64ADB("[GT24C64A] ioctl copy from user failed\n");
				return -EFAULT;
			}
		}
	}

	ptempbuf = (stCAM_CAL_INFO_STRUCT *)pBuff;
	pWorkingBuff = kmalloc(ptempbuf->u4Length, GFP_KERNEL);
	if (NULL == pWorkingBuff) {
		kfree(pBuff);
		GT24C64ADB("[GT24C64A] ioctl allocate mem failed\n");
		return -ENOMEM;
	}
	GT24C64ADB("[GT24C64A] init Working buffer address 0x%p  command is 0x%8x\n", pWorkingBuff, (u32)a_u4Command);


	if (copy_from_user((u8 *)pWorkingBuff , (u8 *)ptempbuf->pu1Params, ptempbuf->u4Length)) {
		kfree(pBuff);
		kfree(pWorkingBuff);
		GT24C64ADB("[GT24C64A] ioctl copy from user failed\n");
		return -EFAULT;
	}

	switch (a_u4Command) {
	case CAM_CALIOC_S_WRITE:
		GT24C64ADB("[GT24C64A] Write CMD\n");
#ifdef GT24C64A_GETDLT_DEBUG
		do_gettimeofday(&ktv1);
#endif
		//i4RetValue = iWriteData((u16)ptempbuf->u4Offset, ptempbuf->u4Length, pWorkingBuff);
#ifdef GT24C64A_GETDLT_DEBUG
		do_gettimeofday(&ktv2);
		if (ktv2.tv_sec > ktv1.tv_sec)
			TimeIntervalUS = ktv1.tv_usec + 1000000 - ktv2.tv_usec;
		else
			TimeIntervalUS = ktv2.tv_usec - ktv1.tv_usec;

		GT24C64ADB("Write data %d bytes take %lu us\n", ptempbuf->u4Length, TimeIntervalUS);
#endif
		break;
	case CAM_CALIOC_G_READ:
		GT24C64ADB("[GT24C64A] Read CMD\n");
#ifdef GT24C64A_GETDLT_DEBUG
		do_gettimeofday(&ktv1);
#endif
		GT24C64ADB("[GT24C64A] offset %x\n", ptempbuf->u4Offset);
		GT24C64ADB("[GT24C64A] length %x\n", ptempbuf->u4Length);
		GT24C64ADB("[GT24C64A] Before read Working buffer address 0x%p\n", pWorkingBuff);

        readTryagain = 3;
        while (0 < readTryagain) {
            i4RetValue =  iReadDataFromGT24c64a((u16)ptempbuf->u4Offset, ptempbuf->u4Length, pWorkingBuff);
            GT24C64ADB("[GT24C64A] error (%d) Read retry (%d)\n", i4RetValue, readTryagain);
            if (i4RetValue <= 0)
                readTryagain--;
            else
                readTryagain = 0;
        }

		GT24C64ADB("[GT24C64A] After read Working buffer data  0x%4x\n", *pWorkingBuff);

#ifdef GT24C64A_GETDLT_DEBUG
		do_gettimeofday(&ktv2);
		if (ktv2.tv_sec > ktv1.tv_sec)
			TimeIntervalUS = ktv1.tv_usec + 1000000 - ktv2.tv_usec;
		else
			TimeIntervalUS = ktv2.tv_usec - ktv1.tv_usec;

		GT24C64ADB("Read data %d bytes take %lu us\n", ptempbuf->u4Length, TimeIntervalUS);
#endif

		break;
	default:
		GT24C64ADB("[GT24C64A] No CMD\n");
		i4RetValue = -EPERM;
		break;
	}

	if (_IOC_READ & _IOC_DIR(a_u4Command)) {
		/* copy data to user space buffer, keep other input paremeter unchange. */
		GT24C64ADB("[GT24C64A] to user length %d\n", ptempbuf->u4Length);
		GT24C64ADB("[GT24C64A] to user  Working buffer address 0x%p\n", pWorkingBuff);
		if (copy_to_user((u8 __user *) ptempbuf->pu1Params , (u8 *)pWorkingBuff , ptempbuf->u4Length)) {
			kfree(pBuff);
			kfree(pWorkingBuff);
			GT24C64ADB("[GT24C64A] ioctl copy to user failed\n");
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
static int GT24C64A_Open(struct inode *a_pstInode, struct file *a_pstFile)
{
	GT24C64ADB("[GT24C64A] GT24C64A_Open\n");
	spin_lock(&GT24C64ALock);
	if (g_u4Opened) {
		spin_unlock(&GT24C64ALock);
		return -EBUSY;
	} /*else {*//*LukeHu--150720=For check patch*/
	if (!g_u4Opened) {/*LukeHu--150720=For check patch*/
		g_u4Opened = 1;
		atomic_set(&g_GT24C64Aatomic, 0);
	}
	spin_unlock(&GT24C64ALock);

	return 0;
}

/* Main jobs: */
/* 1.Deallocate anything that "open" allocated in private_data. */
/* 2.Shut down the device on last close. */
/* 3.Only called once on last time. */
/* Q1 : Try release multiple times. */
static int GT24C64A_Release(struct inode *a_pstInode, struct file *a_pstFile)
{
	spin_lock(&GT24C64ALock);

	g_u4Opened = 0;

	atomic_set(&g_GT24C64Aatomic, 0);

	spin_unlock(&GT24C64ALock);

	return 0;
}

static const struct file_operations g_stEEPROM_fops = {
	.owner = THIS_MODULE,
	.open = GT24C64A_Open,
	.release = GT24C64A_Release,
	/* .ioctl = GT24C64A_ioctl */
#ifdef CONFIG_COMPAT
    .compat_ioctl = GT24c64a_Ioctl_Compat,
#endif
	.unlocked_ioctl = GT24C64A_ioctl
};

#define EEPROM_DYNAMIC_ALLOCATE_DEVNO 1
static inline int RegisterEEPROMCharDrv(void)
{
	struct device *EEPROM_device = NULL;

#if EEPROM_DYNAMIC_ALLOCATE_DEVNO
	if (alloc_chrdev_region(&g_devno, 0, 1, GT24C64A_DRVNAME)) {
		GT24C64ADB("[GT24C64A] Allocate device no failed\n");

		return -EAGAIN;
	}
#else
	if (register_chrdev_region(g_devno , 1 , GT24C64A_DRVNAME)) {
		GT24C64ADB("[GT24C64A] Register device no failed\n");

		return -EAGAIN;
	}
#endif

	/* Allocate driver */
	g_pGT24C64A_CharDrv = cdev_alloc();

	if (NULL == g_pGT24C64A_CharDrv) {
		unregister_chrdev_region(g_devno, 1);

		GT24C64ADB("[GT24C64A] Allocate mem for kobject failed\n");

		return -ENOMEM;
	}

	/* Attatch file operation. */
	cdev_init(g_pGT24C64A_CharDrv, &g_stEEPROM_fops);

	g_pGT24C64A_CharDrv->owner = THIS_MODULE;

	/* Add to system */
	if (cdev_add(g_pGT24C64A_CharDrv, g_devno, 1)) {
		GT24C64ADB("[GT24C64A] Attatch file operation failed\n");

		unregister_chrdev_region(g_devno, 1);

		return -EAGAIN;
	}

	GT24C64A_class = class_create(THIS_MODULE, "GT24c64a_CAM_CALdrv");
	if (IS_ERR(GT24C64A_class)) {
		int ret = PTR_ERR(GT24C64A_class);
		GT24C64ADB("Unable to create class, err = %d\n", ret);
		return ret;
	}
	EEPROM_device = device_create(GT24C64A_class, NULL, g_devno, NULL, GT24C64A_DRVNAME);

	return 0;
}

static inline void UnregisterEEPROMCharDrv(void)
{
	/* Release char driver */
	cdev_del(g_pGT24C64A_CharDrv);

	unregister_chrdev_region(g_devno, 1);

	device_destroy(GT24C64A_class, g_devno);
	class_destroy(GT24C64A_class);
}

/* platform structure */
static struct platform_driver g_stGT24C64A_Driver = {
/*	.probe      = EEPROM_probe,
	.remove = EEPROM_remove,*/
	.driver     = {
		.name   = GT24C64A_DRVNAME,
		.owner  = THIS_MODULE,
	}
};


static struct platform_device g_GT24C64A_Device = {
	.name = GT24C64A_DRVNAME,
	.id = 0,
	.dev = {
	}
};

static int __init GT24C64A_init(void)
{
	i2c_register_board_info(GT24C64A_I2C_BUSNUM, &GT24C64A_kd_dev, 1);
	GT24C64ADB("GT24C64A_init\n");

	if (platform_device_register(&g_GT24C64A_Device)) {
		GT24C64ADB("failed to register GT24C64A driver, 2nd time\n");
		return -ENODEV;
	}
    
    GT24C64ADB("GT24C64A_init step1\n");
	if (platform_driver_register(&g_stGT24C64A_Driver)) {
		GT24C64ADB("failed to register GT24C64A driver\n");
		return -ENODEV;
	}
	GT24C64ADB("GT24C64A_init step2\n");

	RegisterEEPROMCharDrv();

	return 0;
}

static void __exit GT24C64A_exit(void)
{
	platform_device_unregister(&g_GT24C64A_Device);

	platform_driver_unregister(&g_stGT24C64A_Driver);

	UnregisterEEPROMCharDrv();
}

module_init(GT24C64A_init);
module_exit(GT24C64A_exit);

MODULE_DESCRIPTION("GT24C64A driver");
MODULE_AUTHOR("Sean Lin <Sean.Lin@Mediatek.com>");
MODULE_LICENSE("GPL");


