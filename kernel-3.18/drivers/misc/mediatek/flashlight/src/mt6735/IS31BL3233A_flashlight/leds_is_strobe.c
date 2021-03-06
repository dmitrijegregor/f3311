#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/time.h>
#include "kd_flashlight.h"
#include <asm/io.h>
#include <asm/uaccess.h>
#include "kd_camera_typedef.h"
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#ifdef FLASHLIGHT_TIMING_FIX
#include <linux/kthread.h>
#endif
#include <mt-plat/mt_gpio.h>
#include <mt-plat/mt_gpio_core.h>
#include <mach/gpio_const.h>



/******************************************************************************
 * Debug configuration
******************************************************************************/
/* availible parameter */
/* ANDROID_LOG_ASSERT */
/* ANDROID_LOG_ERROR */
/* ANDROID_LOG_WARNING */
/* ANDROID_LOG_INFO */
/* ANDROID_LOG_DEBUG */
/* ANDROID_LOG_VERBOSE */

#define TAG_NAME "[leds_is_strobe.c]"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
#define PK_DBG_FUNC(fmt, arg...)    pr_debug(TAG_NAME "%s: " fmt, __func__ , ##arg)

/*#define DEBUG_LEDS_STROBE*/
#ifdef DEBUG_LEDS_STROBE
#define PK_DBG PK_DBG_FUNC
#else
#define PK_DBG(a, ...)
#endif

extern unsigned int g_flash_ctrl;

/******************************************************************************
 * local variables
******************************************************************************/

static DEFINE_SPINLOCK(g_strobeSMPLock);	/* cotta-- SMP proection */
static DEFINE_SPINLOCK(g_strobe);

static u32 strobe_Res;
static u32 strobe_Timeus;
static BOOL g_strobe_On;

static int g_led_state = 0;
int g_duty=-1;
#ifdef FLASHLIGHT_TIMING_FIX
struct task_struct *flashlight_thread_handle = NULL;
static unsigned long flags;
extern int g_light_test;
int g_idle_cpu = -1;
#else
static struct hrtimer g_timeOutTimer;
static struct work_struct workTimeOut;
#endif
static int g_timeOutTimeMs=0;

static DEFINE_MUTEX(g_strobeSem);

#define FLASH_GPIO_ENF GPIO43
#define FLASH_GPIO_ENM GPIO42

/*****************************************************************************
Functions
*****************************************************************************/
#ifdef FLASHLIGHT_TIMING_FIX
int flashlight_kthread(void *x);
#else
static void work_timeOutFunc(struct work_struct *data);
#endif
int FL_preOn(void)
{
	PK_DBG("[IS31BL3233A]g_led_state=%d, g_duty=%d\n", g_led_state, g_duty);
	return 0;
}

int FL_Enable(void)
{
#ifdef FLASHLIGHT_TIMING_FIX
	int i = 0;

	if (g_duty > 0)
		g_led_state = 2;
	else
		g_led_state = 1;

	PK_DBG("[IS31BL3233A]FL_Enable g_led_state=%d, g_timeOutTimeMs=%d\n", g_led_state, g_timeOutTimeMs);

	if (g_led_state == 2) {
		for (i=0; i<8; i++) {
			if (idle_cpu(i)) {
				g_idle_cpu = i;
			}
		}
		wake_up_process(flashlight_thread_handle);
	} else if (g_led_state == 1) {
		mt_set_gpio_out(FLASH_GPIO_ENM, GPIO_OUT_ONE);
	}
#else
	ktime_t ktime;

	if (g_duty > 0)
		g_led_state = 2;
	else
		g_led_state = 1;

	PK_DBG("[IS31BL3233A]FL_Enable g_led_state=%d, g_timeOutTimeMs=%d\n", g_led_state, g_timeOutTimeMs);

	ktime = ktime_set(0, g_timeOutTimeMs * 1000);
	hrtimer_start(&g_timeOutTimer, ktime, HRTIMER_MODE_REL);

	PK_DBG("[IS31BL3233A]FL_Enable g_led_state=%d, g_duty=%d\n", g_led_state, g_duty);
#endif
	return 0;
}

int FL_Disable(void)
{
	if (g_led_state == 1) {
		mt_set_gpio_out(FLASH_GPIO_ENM, GPIO_OUT_ZERO);		
	}
	g_led_state = 0;
	g_duty = -1;
#ifndef FLASHLIGHT_TIMING_FIX
	if (g_timeOutTimeMs != 0) {
		hrtimer_cancel(&g_timeOutTimer);
		g_timeOutTimeMs = 0;
	}
#endif
	PK_DBG("[IS31BL3233A]FL_Disable g_led_state=%d, g_duty=%d\n", g_led_state, g_duty);

	return 0;
}

int FL_dim_duty(kal_uint32 duty)
{
	PK_DBG("[IS31BL3233A]duty=%u, g_duty=%d\n", duty, g_duty);

	if (duty == 0) {
		g_duty = duty;
	} else if (duty > 0) {
		g_duty = (duty > 6) ? 6 : duty;
	}
	PK_DBG("[IS31BL3233A]g_duty=%d, g_led_state=%d\n", g_duty, g_led_state);

	return 0;
}


int FL_Init(void)
{
#ifdef FLASHLIGHT_TIMING_FIX
	int err;
#endif
	if(mt_set_gpio_mode(FLASH_GPIO_ENF,GPIO_MODE_00)){printk("[IS31BL3233A _flashlight] set gpio enf mode failed!! \n");}
	if(mt_set_gpio_dir(FLASH_GPIO_ENF,GPIO_DIR_OUT)){printk("[IS31BL3233A _flashlight] set gpio enf dir failed!! \n");}
	if(mt_set_gpio_out(FLASH_GPIO_ENF,GPIO_OUT_ZERO)){printk("[IS31BL3233A _flashlight] set gpio enf failed!! \n");}

	if(mt_set_gpio_mode(FLASH_GPIO_ENM,GPIO_MODE_00)){printk("[IS31BL3233A _flashlight] set gpio enm mode failed!! \n");}
	if(mt_set_gpio_dir(FLASH_GPIO_ENM,GPIO_DIR_OUT)){printk("[IS31BL3233A _flashlight] set gpio enm dir failed!! \n");}
	if(mt_set_gpio_out(FLASH_GPIO_ENM,GPIO_OUT_ZERO)){printk("[IS31BL3233A _flashlight] set gpio enm failed!! \n");}
#ifdef FLASHLIGHT_TIMING_FIX
	if (flashlight_thread_handle == NULL)
		flashlight_thread_handle = kthread_create(flashlight_kthread, (void *)NULL, "flashlight_thread");
	if (IS_ERR(flashlight_thread_handle)) {
		PK_DBG("[IS31BL3233A]FL_Enable Unable to start flashlight kernel thread./n");
		err = PTR_ERR(flashlight_thread_handle);
		flashlight_thread_handle = NULL;
		return err;
	}
#endif
	PK_DBG("[IS31BL3233A]FL_Init line=%d\n",__LINE__);

	return 0;
}


int FL_Uninit(void)
{
	mt_set_gpio_out(FLASH_GPIO_ENF, GPIO_OUT_ZERO);
	mt_set_gpio_out(FLASH_GPIO_ENM, GPIO_OUT_ZERO);
#ifdef FLASHLIGHT_TIMING_FIX
	if (flashlight_thread_handle) {
		kthread_stop(flashlight_thread_handle);
		flashlight_thread_handle = NULL;
	}
#endif
	PK_DBG("[IS31BL3233A]FL_Uninit line=%d\n", __LINE__);

	return 0;
}

int FL_hasLowPowerDetect(void)
{
	return 0;
}

/*****************************************************************************
User interface
*****************************************************************************/
#ifdef FLASHLIGHT_TIMING_FIX
int flashlight_kthread(void *x)
{
	unsigned int cycle = 0, hightime = 0, lowtime = 0;
	int i = 0, ret = 0, old_idle_cpu = -1;

	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if(kthread_should_stop())
			break;

		if (g_led_state == 2) {
			if (g_light_test) {
				cycle = 500 * 1000 / 200;
				hightime = (40 + g_duty * 10) << 1;
				lowtime = 200 - hightime;
				g_led_state = 0;
				g_light_test = 0;
			} else {
				if (g_idle_cpu != -1 && g_idle_cpu != old_idle_cpu) {
					ret = set_cpus_allowed(current, cpumask_of_cpu(g_idle_cpu));
					//PK_DBG("[IS31BL3233A]%s: set_cpus_allowed() g_idle_cpu=%d, return %d\n", __func__, g_idle_cpu, ret);
					if (ret != 0) {
						for (i=0; i<4; i++) {
							if (idle_cpu(i)) {
								g_idle_cpu = i;
								ret = set_cpus_allowed(current, cpumask_of_cpu(g_idle_cpu));
								//PK_DBG("[IS31BL3233A]%s: set_cpus_allowed() g_idle_cpu=%d, return %d\n", __func__, g_idle_cpu, ret);
								if (ret == 0) {
									old_idle_cpu = g_idle_cpu;
									break;
								}
							}
						}
					} else {
						old_idle_cpu = g_idle_cpu;
					}
				}

				cycle = 250 * 1000 / 200;
				hightime = (40 + g_duty * 10) << 1;
				lowtime = 200 - hightime;
			}
			spin_lock_irqsave(&g_strobe, flags);
			for (i = 0; i < cycle; i++) {
				mt_set_gpio_out(FLASH_GPIO_ENF, GPIO_OUT_ONE);
				udelay(hightime);
				mt_set_gpio_out(FLASH_GPIO_ENF, GPIO_OUT_ZERO);
				udelay(lowtime);
			}
			spin_unlock_irqrestore(&g_strobe, flags);
			//PK_DBG("[IS31BL3233A]%s: flash\n", __func__);
		//} else if (g_led_state == 1 && torch_set == 0) {
		//	mt_set_gpio_out(FLASH_GPIO_ENM, GPIO_OUT_ONE);
		//	torch_set = 1;
		//	PK_DBG("[IS31BL3233A]%s: torch\n", __func__);
		} else if (g_led_state == 0) {
			g_idle_cpu = -1;
			schedule_timeout(HZ);
		}
	}
	return 0;
}
#else
static void work_timeOutFunc(struct work_struct *data)
{
	unsigned int cycle = 0, hightime = 0, lowtime = 0;
	int i = 0;
	if (g_led_state == 2) {
		cycle = 500 * 1000 / 200;		
		hightime = (40 + g_duty * 10) << 1;
		lowtime = 200 - hightime;
		spin_lock_irq(&g_strobe);

		for (i = 0; i < cycle && g_led_state == 2; i++) {
			mt_set_gpio_out(FLASH_GPIO_ENF, GPIO_OUT_ONE);
			udelay(hightime);
			mt_set_gpio_out(FLASH_GPIO_ENF, GPIO_OUT_ZERO);
			udelay(lowtime);
		}
		spin_unlock_irq(&g_strobe);
	} else if (g_led_state == 1) {
		mt_set_gpio_out(FLASH_GPIO_ENM, GPIO_OUT_ONE);
	}
}

enum hrtimer_restart ledTimeOutCallback(struct hrtimer *timer)
{
	schedule_work(&workTimeOut);
	return HRTIMER_NORESTART;
}

void timerInit(void)
{
	static int init_flag;

	if (init_flag == 0) {
		init_flag = 1;
		INIT_WORK(&workTimeOut, work_timeOutFunc);
		g_timeOutTimeMs = 1000;
		hrtimer_init(&g_timeOutTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		g_timeOutTimer.function = ledTimeOutCallback;
	}
}
#endif

static int constant_flashlight_ioctl(unsigned int cmd, unsigned long arg)
{
	int temp;
	int i4RetValue = 0;
	int ior_shift;
	int iow_shift;
	int iowr_shift;

	ior_shift = cmd - (_IOR(FLASHLIGHT_MAGIC, 0, int));
	iow_shift = cmd - (_IOW(FLASHLIGHT_MAGIC, 0, int));
	iowr_shift = cmd - (_IOWR(FLASHLIGHT_MAGIC, 0, int));
	PK_DBG("[IS31BL3233A]constant_flashlight_ioctl() line=%d ior_shift=%d, iow_shift=%d iowr_shift=%d arg=%d\n",
	     __LINE__, ior_shift, iow_shift, iowr_shift, (int)arg);
	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		PK_DBG("[IS31BL3233A]FLASH_IOC_SET_TIME_OUT_TIME_MS: %d\n", (int)arg);
		g_timeOutTimeMs = arg;
		break;

	case FLASH_IOC_SET_DUTY:
		PK_DBG("[IS31BL3233A]FLASH_IOC_SET_DUTY: %d\n", (int)arg);
		FL_dim_duty(arg);
		break;

	case FLASH_IOC_SET_STEP:
		PK_DBG("[IS31BL3233A]FLASH_IOC_SET_STEP: %d\n", (int)arg);
		break;

 	case FLASH_IOC_PRE_ON:
 	 	PK_DBG("[IS31BL3233A]FLASH_IOC_PRE_ON: %d\n", (int)arg);
 	 	FL_preOn();
 	 	break;

 	case FLASH_IOC_GET_PRE_ON_TIME_MS_DUTY:
		PK_DBG("[IS31BL3233A]FLASH_IOC_GET_PRE_ON_TIME_MS_DUTY: %d\n",(int)arg);
 	 	i4RetValue= g_duty;
		
		break;

 	case FLASH_IOC_GET_PRE_ON_TIME_MS:
 	 	PK_DBG("[IS31BL3233A]FLASH_IOC_GET_PRE_ON_TIME_MS: %d\n",(int)arg);
 	 	i4RetValue= g_timeOutTimeMs;		
		break;

	case FLASH_IOC_SET_ONOFF:
		PK_DBG("[IS31BL3233A]FLASH_IOC_SET_ONOFF: %d\n", (int)arg);
		if (!g_flash_ctrl) {
			if (arg == 1) {
				FL_Enable();
			} else {
				if (g_led_state) {
					FL_Disable();
				}
			}
		}
		break;

 	case FLASH_IOC_HAS_LOW_POWER_DETECT:
   		PK_DBG("[IS31BL3233A]FLASH_IOC_HAS_LOW_POWER_DETECT");
   		temp=FL_hasLowPowerDetect();
   		if (copy_to_user((void __user *) arg , (void*)&temp , 4)) {
 	 	 	PK_DBG("[IS31BL3233A]ioctl copy to user failed\n");
 	 	 	return -1;
		}
		break;
	default:
		PK_DBG("[IS31BL3233A]No such command\n");
		i4RetValue = -EPERM;
		break;
	}

	return i4RetValue;
}




static int constant_flashlight_open(void *pArg)
{
	int i4RetValue = 0;
	PK_DBG("[IS31BL3233A]constant_flashlight_open line=%d\n", __LINE__);

	if (0 == strobe_Res) {
		FL_Init();
#ifndef FLASHLIGHT_TIMING_FIX
		timerInit();
#endif
	}
	PK_DBG("[IS31BL3233A]constant_flashlight_open line=%d\n", __LINE__);
	spin_lock_irq(&g_strobeSMPLock);


	if (strobe_Res) {
		PK_DBG("[IS31BL3233A]busy!\n");
		i4RetValue = -EBUSY;
	} else {
		strobe_Res += 1;
	}

	spin_unlock_irq(&g_strobeSMPLock);
	PK_DBG("[IS31BL3233A]constant_flashlight_open line=%d\n", __LINE__);

	return i4RetValue;

}


static int constant_flashlight_release(void *pArg)
{
	PK_DBG("[IS31BL3233A]constant_flashlight_release\n");

	if (strobe_Res) {
		spin_lock_irq(&g_strobeSMPLock);

		strobe_Res = 0;
		strobe_Timeus = 0;

		/* LED On Status */
		g_strobe_On = FALSE;

		spin_unlock_irq(&g_strobeSMPLock);

		FL_Uninit();
	}

	PK_DBG("[IS31BL3233A]Done\n");

	return 0;

}


FLASHLIGHT_FUNCTION_STRUCT constantFlashlightFunc = {
	constant_flashlight_open,
	constant_flashlight_release,
	constant_flashlight_ioctl
};


MUINT32 constantFlashlightInit(PFLASHLIGHT_FUNCTION_STRUCT *pfFunc)
{
	if (pfFunc != NULL)
		*pfFunc = &constantFlashlightFunc;
	return 0;
}



/* LED flash control for high current capture mode*/
ssize_t strobe_VDIrq(void)
{

	return 0;
}
EXPORT_SYMBOL(strobe_VDIrq);
