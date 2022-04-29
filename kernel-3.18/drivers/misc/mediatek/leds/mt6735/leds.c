/*
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * mt65xx leds driver
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/of.h>
/* #include <linux/leds-mt65xx.h> */
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/delay.h>
/* #include <mach/mt_pwm.h>
#include <mach/upmu_common_sw.h>
#include <mach/upmu_common.h>
#include <mach/upmu_sw.h>
#include <mach/upmu_hw.h>*/

#ifdef CONFIG_MTK_AAL_SUPPORT
#include <ddp_aal.h>
/* #include <linux/aee.h> */
#endif

#include <mt-plat/mt_pwm.h>
#include <mt-plat/upmu_common.h>
#include <mt-plat/mt_gpio.h>

#include "leds_sw.h"
#include "leds_hal.h"
#include "ddp_pwm.h"
#include "mtkfb.h"

#include <cei_hw_id/cei_hw_id.h>

/* for LED&Backlight bringup, define the dummy API */
#ifndef CONFIG_MTK_PMIC
u16 pmic_set_register_value(u32 flagname, u32 val)
{
	return 0;
}
#endif

#ifndef CONFIG_MTK_PWM
s32 pwm_set_spec_config(struct pwm_spec_config *conf)
{
	return 0;
}

void mt_pwm_disable(u32 pwm_no, u8 pmic_pad)
{
}
#endif

static DEFINE_MUTEX(leds_mutex);
static DEFINE_MUTEX(leds_pmic_mutex);

/****************************************************************************
 * variables
 ***************************************************************************/
/* struct cust_mt65xx_led* bl_setting_hal = NULL; */
static unsigned int bl_brightness_hal = 102;
static unsigned int bl_duty_hal = 21;
static unsigned int bl_div_hal = CLK_DIV1;
static unsigned int bl_frequency_hal = 32000;
/* for button led don't do ISINK disable first time */
static int button_flag_isink0;
static int button_flag_isink1;
static int button_flag_isink2;
static int button_flag_isink3;

struct wake_lock leds_suspend_lock;

char *leds_name[MT65XX_LED_TYPE_TOTAL] = {
	"red",
	"green",
	"blue",
	"jogball-backlight",
	"keyboard-backlight",
	"button-backlight",
	"lcd-backlight",
};

struct cust_mt65xx_led *pled_dtsi = NULL;
/****************************************************************************
 * DEBUG MACROS
 ***************************************************************************/
static int debug_enable_led_hal = 1;
#define LEDS_DEBUG(format, args...) do { \
	if (debug_enable_led_hal) {	\
		pr_debug("[LED]"format, ##args);\
	} \
} while (0)

/*****************PWM *************************************************/
#define PWM_DIV_NUM 8
static int time_array_hal[PWM_DIV_NUM] = {
	256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};
static unsigned int div_array_hal[PWM_DIV_NUM] = {
	1, 2, 4, 8, 16, 32, 64, 128
};

static unsigned int backlight_PWM_div_hal = CLK_DIV1;	/* this para come from cust_leds. */

/****************************************************************************
 * func:return global variables
 ***************************************************************************/

void mt_leds_wake_lock_init(void)
{
	wake_lock_init(&leds_suspend_lock, WAKE_LOCK_SUSPEND, "leds wakelock");
}

unsigned int mt_get_bl_brightness(void)
{
	return bl_brightness_hal;
}

unsigned int mt_get_bl_duty(void)
{
	return bl_duty_hal;
}

unsigned int mt_get_bl_div(void)
{
	return bl_div_hal;
}

unsigned int mt_get_bl_frequency(void)
{
	return bl_frequency_hal;
}

unsigned int *mt_get_div_array(void)
{
	return &div_array_hal[0];
}

void mt_set_bl_duty(unsigned int level)
{
	bl_duty_hal = level;
}

void mt_set_bl_div(unsigned int div)
{
	bl_div_hal = div;
}

void mt_set_bl_frequency(unsigned int freq)
{
	bl_frequency_hal = freq;
}

struct cust_mt65xx_led *get_cust_led_dtsi(void) {
	struct device_node *led_node = NULL;
	bool isSupportDTS = false;
	int i, ret;
	int mode, data;
	int pwm_config[5] = { 0 };

	// LEDS_DEBUG("get_cust_led_dtsi: get the leds info from device tree\n");
	if (pled_dtsi == NULL) {
		/* this can allocat an new struct array */
		pled_dtsi = kmalloc(MT65XX_LED_TYPE_TOTAL *
		                    sizeof(struct
		                           cust_mt65xx_led),
		                    GFP_KERNEL);
		if (pled_dtsi == NULL) {
			LEDS_DEBUG("get_cust_led_dtsi kmalloc fail\n");
			goto out;
		}

		for (i = 0; i < MT65XX_LED_TYPE_TOTAL; i++) {

			char node_name[32] = "mediatek,";

			pled_dtsi[i].name = leds_name[i];

			led_node =
			    of_find_compatible_node(NULL, NULL,
			                            strcat(node_name,
			                                   leds_name[i]));
			if (!led_node) {
				LEDS_DEBUG("Cannot find LED node from dts\n");
				pled_dtsi[i].mode = 0;
				pled_dtsi[i].data = -1;
			} else {
				isSupportDTS = true;
				ret =
				    of_property_read_u32(led_node, "led_mode",
				                         &mode);
				if (!ret) {
					pled_dtsi[i].mode = mode;
					LEDS_DEBUG
					("The %s's led mode is : %d\n",
					 pled_dtsi[i].name,
					 pled_dtsi[i].mode);
				} else {
					LEDS_DEBUG
					("led dts can not get led mode");
					pled_dtsi[i].mode = 0;
				}

				ret =
				    of_property_read_u32(led_node, "data",
				                         &data);
				if (!ret) {
					pled_dtsi[i].data = data;
					LEDS_DEBUG
					("The %s's led data is : %ld\n",
					 pled_dtsi[i].name,
					 pled_dtsi[i].data);
				} else {
					LEDS_DEBUG
					("led dts can not get led data");
					pled_dtsi[i].data = -1;
				}

				ret =
				    of_property_read_u32_array(led_node, "pwm_config", pwm_config, ARRAY_SIZE(pwm_config));
				if (!ret) {
					LEDS_DEBUG
					("The %s's pwm config data is %d %d %d %d %d\n",
					 pled_dtsi[i].name, pwm_config[0],
					 pwm_config[1], pwm_config[2],
					 pwm_config[3], pwm_config[4]);
					pled_dtsi[i].config_data.clock_source =
					    pwm_config[0];
					pled_dtsi[i].config_data.div =
					    pwm_config[1];
					pled_dtsi[i].config_data.low_duration =
					    pwm_config[2];
					pled_dtsi[i].config_data.High_duration =
					    pwm_config[3];
					pled_dtsi[i].config_data.pmic_pad =
					    pwm_config[4];

				} else
					LEDS_DEBUG
					("led dts can not get pwm config data.\n");

				switch (pled_dtsi[i].mode) {
				case MT65XX_LED_MODE_CUST_LCM:
					pled_dtsi[i].data =
					    (long)mtkfb_set_backlight_level;
					LEDS_DEBUG
					("kernel:the backlight hw mode is LCM.\n");
					break;
				case MT65XX_LED_MODE_CUST_BLS_PWM:
					pled_dtsi[i].data =
					    (long)disp_bls_set_backlight;
					LEDS_DEBUG
					("kernel:the backlight hw mode is BLS.\n");
					break;
				default:
					break;
				}
			}
		}

		if (!isSupportDTS) {
			kfree(pled_dtsi);
			pled_dtsi = NULL;
		}
	}
out:
	return pled_dtsi;
}

struct cust_mt65xx_led *mt_get_cust_led_list(void) {
	struct cust_mt65xx_led *cust_led_list = get_cust_led_dtsi();
	return cust_led_list;
}

/****************************************************************************
 * internal functions
 ***************************************************************************/
static int brightness_mapto64(int level)
{
	if (level < 30)
		return (level >> 1) + 7;
	else if (level <= 120)
		return (level >> 2) + 14;
	else if (level <= 160)
		return level / 5 + 20;
	else
		return (level >> 3) + 33;
}

static int find_time_index(int time)
{
	int index = 0;

	while (index < 8) {
		if (time < time_array_hal[index])
			return index;
		index++;
	}
	return PWM_DIV_NUM - 1;
}

int mt_led_set_pwm(int pwm_num, struct nled_setting *led)
{
	struct pwm_spec_config pwm_setting;
	int time_index = 0;

	pwm_setting.pwm_no = pwm_num;
	pwm_setting.mode = PWM_MODE_OLD;

	LEDS_DEBUG("led_set_pwm: mode=%d,pwm_no=%d\n", led->nled_mode,
	           pwm_num);
	/* We won't choose 32K to be the clock src of old mode because of system performance. */
	/* The setting here will be clock src = 26MHz, CLKSEL = 26M/1625 (i.e. 16K) */
	pwm_setting.clk_src = PWM_CLK_OLD_MODE_32K;

	switch (led->nled_mode) {
		/* Actually, the setting still can not to turn off NLED. We should disable PWM to turn off NLED. */
	case NLED_OFF:
		pwm_setting.PWM_MODE_OLD_REGS.THRESH = 0;
		pwm_setting.clk_div = CLK_DIV1;
		pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 100 / 2;
		break;

	case NLED_ON:
		pwm_setting.PWM_MODE_OLD_REGS.THRESH = 30 / 2;
		pwm_setting.clk_div = CLK_DIV1;
		pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 100 / 2;
		break;

	case NLED_BLINK:
		LEDS_DEBUG("LED blink on time = %d offtime = %d\n",
		           led->blink_on_time, led->blink_off_time);
		time_index =
		    find_time_index(led->blink_on_time + led->blink_off_time);
		LEDS_DEBUG("LED div is %d\n", time_index);
		pwm_setting.clk_div = time_index;
		pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH =
		    (led->blink_on_time +
		     led->blink_off_time) * MIN_FRE_OLD_PWM /
		    div_array_hal[time_index];
		pwm_setting.PWM_MODE_OLD_REGS.THRESH =
		    (led->blink_on_time * 100) / (led->blink_on_time +
		                                  led->blink_off_time);
	}

	pwm_setting.PWM_MODE_FIFO_REGS.IDLE_VALUE = 0;
	pwm_setting.PWM_MODE_FIFO_REGS.GUARD_VALUE = 0;
	pwm_setting.PWM_MODE_FIFO_REGS.GDURATION = 0;
	pwm_setting.PWM_MODE_FIFO_REGS.WAVE_NUM = 0;
	pwm_set_spec_config(&pwm_setting);

	return 0;
}

/************************ led breath function*****************************/
/*************************************************************************
//func is to swtich to breath mode from PWM mode of ISINK
//para: enable: 1 : breath mode; 0: PWM mode;
*************************************************************************/
#if 0
static int led_switch_breath_pmic(enum mt65xx_led_pmic pmic_type,
                                  struct nled_setting *led, int enable)
{
	/* int time_index = 0; */
	/* int duty = 0; */
	LEDS_DEBUG("led_blink_pmic: pmic_type=%d\n", pmic_type);

	if ((pmic_type != MT65XX_LED_PMIC_NLED_ISINK0
	        && pmic_type != MT65XX_LED_PMIC_NLED_ISINK1
	        && pmic_type != MT65XX_LED_PMIC_NLED_ISINK2
	        && pmic_type != MT65XX_LED_PMIC_NLED_ISINK3)
	        || led->nled_mode != NLED_BLINK) {
		return -1;
	}
	if (1 == enable) {
		switch (pmic_type) {
		case MT65XX_LED_PMIC_NLED_ISINK0:
			pmic_set_register_value(PMIC_ISINK_CH0_MODE,
			                        ISINK_BREATH_MODE);
			pmic_set_register_value(PMIC_ISINK_CH0_STEP, ISINK_3);
			pmic_set_register_value(PMIC_ISINK_BREATH0_TR1_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH0_TR2_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH0_TF1_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH0_TF2_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH0_TON_SEL,
			                        0x02);
			pmic_set_register_value(PMIC_ISINK_BREATH0_TOFF_SEL,
			                        0x03);
			pmic_set_register_value(PMIC_ISINK_DIM0_DUTY, 15);
			pmic_set_register_value(PMIC_ISINK_DIM0_FSEL, 11);
			/* pmic_set_register_value(PMIC_ISINK_CH0_EN,NLED_ON); */
			break;
		case MT65XX_LED_PMIC_NLED_ISINK1:
			pmic_set_register_value(PMIC_ISINK_CH1_MODE,
			                        ISINK_BREATH_MODE);
			pmic_set_register_value(PMIC_ISINK_CH1_STEP, ISINK_3);
			pmic_set_register_value(PMIC_ISINK_BREATH1_TR1_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH1_TR2_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH1_TF1_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH1_TF2_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH1_TON_SEL,
			                        0x02);
			pmic_set_register_value(PMIC_ISINK_BREATH1_TOFF_SEL,
			                        0x03);
			pmic_set_register_value(PMIC_ISINK_DIM1_DUTY, 15);
			pmic_set_register_value(PMIC_ISINK_DIM1_FSEL, 11);
			/* pmic_set_register_value(PMIC_ISINK_CH1_EN,NLED_ON); */
			break;
		case MT65XX_LED_PMIC_NLED_ISINK2:
			pmic_set_register_value(PMIC_ISINK_CH2_MODE,
			                        ISINK_BREATH_MODE);
			pmic_set_register_value(PMIC_ISINK_CH2_STEP, ISINK_3);
			pmic_set_register_value(PMIC_ISINK_BREATH2_TR1_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH2_TR2_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH2_TF1_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH2_TF2_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH2_TON_SEL,
			                        0x02);
			pmic_set_register_value(PMIC_ISINK_BREATH2_TOFF_SEL,
			                        0x03);
			pmic_set_register_value(PMIC_ISINK_DIM2_DUTY, 15);
			pmic_set_register_value(PMIC_ISINK_DIM2_FSEL, 11);
			/* pmic_set_register_value(PMIC_ISINK_CH2_EN,NLED_ON); */
			break;
		case MT65XX_LED_PMIC_NLED_ISINK3:
			pmic_set_register_value(PMIC_ISINK_CH3_MODE,
			                        ISINK_BREATH_MODE);
			pmic_set_register_value(PMIC_ISINK_CH3_STEP, ISINK_3);
			pmic_set_register_value(PMIC_ISINK_BREATH3_TR1_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH3_TR2_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH3_TF1_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH3_TF2_SEL,
			                        0x04);
			pmic_set_register_value(PMIC_ISINK_BREATH3_TON_SEL,
			                        0x02);
			pmic_set_register_value(PMIC_ISINK_BREATH3_TOFF_SEL,
			                        0x03);
			pmic_set_register_value(PMIC_ISINK_DIM3_DUTY, 15);
			pmic_set_register_value(PMIC_ISINK_DIM3_FSEL, 11);
			/* pmic_set_register_value(PMIC_ISINK_CH3_EN,NLED_ON); */
			break;
		default:
			break;
		}
	} else {
		switch (pmic_type) {
		case MT65XX_LED_PMIC_NLED_ISINK0:
			pmic_set_register_value(PMIC_ISINK_CH3_MODE,
			                        ISINK_PWM_MODE);
			break;
		case MT65XX_LED_PMIC_NLED_ISINK0:
			pmic_set_register_value(PMIC_ISINK_CH3_MODE,
			                        ISINK_PWM_MODE);
			break;
		case MT65XX_LED_PMIC_NLED_ISINK0:
			pmic_set_register_value(PMIC_ISINK_CH3_MODE,
			                        ISINK_PWM_MODE);
			break;
		case MT65XX_LED_PMIC_NLED_ISINK0:
			pmic_set_register_value(PMIC_ISINK_CH3_MODE,
			                        ISINK_PWM_MODE);
			break;
		default:
			break;
		}
	}
	return 0;

}
#endif

#define PMIC_PERIOD_NUM 8
/* 100 * period, ex: 0.01 Hz -> 0.01 * 100 = 1 */
int pmic_period_array[] = { 250, 500, 1000, 1250, 1666, 2000, 2500, 10000 };

/* int pmic_freqsel_array[] = {99999, 9999, 4999, 1999, 999, 499, 199, 4, 0}; */
int pmic_freqsel_array[] = { 0, 4, 199, 499, 999, 1999, 1999, 1999 };

static int find_time_index_pmic(int time_ms)
{
	int i;

	for (i = 0; i < PMIC_PERIOD_NUM; i++) {
		if (time_ms <= pmic_period_array[i])
			return i;
	}
	return PMIC_PERIOD_NUM - 1;
}
// Mike, blinking interface shall go here.
extern struct mt65xx_led_data *g_leds_data[MT65XX_LED_TYPE_TOTAL]; // May delcare at header file.
int mt_led_blink_pmic(enum mt65xx_led_pmic pmic_type, struct nled_setting *led)
{
	int time_index = 0;
	int duty = 0;

	LEDS_DEBUG("led_blink_pmic: pmic_type=%d\n", pmic_type);

	if ((pmic_type != MT65XX_LED_PMIC_NLED_ISINK0
	        && pmic_type != MT65XX_LED_PMIC_NLED_ISINK1)
	        || led->nled_mode != NLED_BLINK) {
		return -1;
	}

	LEDS_DEBUG("LED blink on time = %d offtime = %d\n",
	           led->blink_on_time, led->blink_off_time);
	time_index =
	    find_time_index_pmic(led->blink_on_time + led->blink_off_time);
	LEDS_DEBUG("LED index is %d  freqsel=%d\n", time_index,
	           pmic_freqsel_array[time_index]);
	duty =
	    32 * led->blink_on_time / (led->blink_on_time +
	                               led->blink_off_time);
	/* pmic_set_register_value(PMIC_RG_G_DRV_2M_CK_PDN(0X0); // DISABLE POWER DOWN ,Indicator no need) */
	pmic_set_register_value(PMIC_RG_DRV_32K_CK_PDN, 0x0);	/* Disable power down */
	switch (pmic_type) {
	case MT65XX_LED_PMIC_NLED_ISINK0:
		pmic_set_register_value(PMIC_RG_DRV_ISINK0_CK_PDN, 0);
		pmic_set_register_value(PMIC_RG_DRV_ISINK0_CK_CKSEL, 0);
		pmic_set_register_value(PMIC_ISINK_CH0_MODE, ISINK_PWM_MODE);
		pmic_set_register_value(PMIC_ISINK_CH0_STEP, led->blink_level);	/* ISINK_3 = 16mA */
		pmic_set_register_value(PMIC_ISINK_DIM0_DUTY, duty);
		pmic_set_register_value(PMIC_ISINK_DIM0_FSEL, led->blink_off_time+led->blink_on_time-1);
		pmic_set_register_value(PMIC_ISINK_CH0_EN, NLED_ON);
		break;
	case MT65XX_LED_PMIC_NLED_ISINK1:
		pmic_set_register_value(PMIC_RG_DRV_ISINK1_CK_PDN, 0);
		pmic_set_register_value(PMIC_RG_DRV_ISINK1_CK_CKSEL, 0);
		pmic_set_register_value(PMIC_ISINK_CH1_MODE, ISINK_PWM_MODE);
		pmic_set_register_value(PMIC_ISINK_CH1_STEP, led->blink_level);	/* 16mA */
		pmic_set_register_value(PMIC_ISINK_DIM1_DUTY, duty);
		pmic_set_register_value(PMIC_ISINK_DIM1_FSEL, led->blink_off_time+led->blink_on_time-1);
		pmic_set_register_value(PMIC_ISINK_CH1_EN, NLED_ON);
		break;
	default:
		break;
	}
	return 0;
}

int mt_backlight_set_pwm(int pwm_num, u32 level, u32 div,
                         struct PWM_config *config_data)
{
	struct pwm_spec_config pwm_setting;
	unsigned int BacklightLevelSupport =
	    Cust_GetBacklightLevelSupport_byPWM();
	pwm_setting.pwm_no = pwm_num;

	if (BacklightLevelSupport == BACKLIGHT_LEVEL_PWM_256_SUPPORT)
		pwm_setting.mode = PWM_MODE_OLD;
	else
		pwm_setting.mode = PWM_MODE_FIFO;	/* New mode fifo and periodical mode */

	pwm_setting.pmic_pad = config_data->pmic_pad;

	if (config_data->div) {
		pwm_setting.clk_div = config_data->div;
		backlight_PWM_div_hal = config_data->div;
	} else
		pwm_setting.clk_div = div;

	if (BacklightLevelSupport == BACKLIGHT_LEVEL_PWM_256_SUPPORT) {
		if (config_data->clock_source)
			pwm_setting.clk_src = PWM_CLK_OLD_MODE_BLOCK;
		else
			pwm_setting.clk_src = PWM_CLK_OLD_MODE_32K;	/* actually.
			it's block/1625 = 26M/1625 = 16KHz @ MT6571 */

		pwm_setting.PWM_MODE_OLD_REGS.IDLE_VALUE = 0;
		pwm_setting.PWM_MODE_OLD_REGS.GUARD_VALUE = 0;
		pwm_setting.PWM_MODE_OLD_REGS.GDURATION = 0;
		pwm_setting.PWM_MODE_OLD_REGS.WAVE_NUM = 0;
		pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 255;	/* 256 level */
		pwm_setting.PWM_MODE_OLD_REGS.THRESH = level;

		LEDS_DEBUG("[LEDS][%d]backlight_set_pwm:duty is %d/%d\n",
		           BacklightLevelSupport, level,
		           pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH);
		LEDS_DEBUG("[LEDS][%d]backlight_set_pwm:clk_src/div is %d%d\n",
		           BacklightLevelSupport, pwm_setting.clk_src,
		           pwm_setting.clk_div);
		if (level > 0 && level < 256) {
			pwm_set_spec_config(&pwm_setting);
			LEDS_DEBUG
			("[LEDS][%d]backlight_set_pwm: old mode: thres/data_width is %d/%d\n",
			 BacklightLevelSupport,
			 pwm_setting.PWM_MODE_OLD_REGS.THRESH,
			 pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH);
		} else {
			LEDS_DEBUG("[LEDS][%d]Error level in backlight\n",
			           BacklightLevelSupport);
			mt_pwm_disable(pwm_setting.pwm_no,
			               config_data->pmic_pad);
		}
		return 0;

	} else {
		if (config_data->clock_source) {
			pwm_setting.clk_src = PWM_CLK_NEW_MODE_BLOCK;
		} else {
			pwm_setting.clk_src =
			    PWM_CLK_NEW_MODE_BLOCK_DIV_BY_1625;
		}

		if (config_data->High_duration && config_data->low_duration) {
			pwm_setting.PWM_MODE_FIFO_REGS.HDURATION =
			    config_data->High_duration;
			pwm_setting.PWM_MODE_FIFO_REGS.LDURATION =
			    pwm_setting.PWM_MODE_FIFO_REGS.HDURATION;
		} else {
			pwm_setting.PWM_MODE_FIFO_REGS.HDURATION = 4;
			pwm_setting.PWM_MODE_FIFO_REGS.LDURATION = 4;
		}

		pwm_setting.PWM_MODE_FIFO_REGS.IDLE_VALUE = 0;
		pwm_setting.PWM_MODE_FIFO_REGS.GUARD_VALUE = 0;
		pwm_setting.PWM_MODE_FIFO_REGS.STOP_BITPOS_VALUE = 31;
		pwm_setting.PWM_MODE_FIFO_REGS.GDURATION =
		    (pwm_setting.PWM_MODE_FIFO_REGS.HDURATION + 1) * 32 - 1;
		pwm_setting.PWM_MODE_FIFO_REGS.WAVE_NUM = 0;

		LEDS_DEBUG("[LEDS]backlight_set_pwm:duty is %d\n", level);
		LEDS_DEBUG
		("[LEDS]backlight_set_pwm:clk_src/div/high/low is %d%d%d%d\n",
		 pwm_setting.clk_src, pwm_setting.clk_div,
		 pwm_setting.PWM_MODE_FIFO_REGS.HDURATION,
		 pwm_setting.PWM_MODE_FIFO_REGS.LDURATION);

		if (level > 0 && level <= 32) {
			pwm_setting.PWM_MODE_FIFO_REGS.GUARD_VALUE = 0;
			pwm_setting.PWM_MODE_FIFO_REGS.SEND_DATA0 =
			    (1 << level) - 1;
			pwm_set_spec_config(&pwm_setting);
		} else if (level > 32 && level <= 64) {
			pwm_setting.PWM_MODE_FIFO_REGS.GUARD_VALUE = 1;
			level -= 32;
			pwm_setting.PWM_MODE_FIFO_REGS.SEND_DATA0 =
			    (1 << level) - 1;
			pwm_set_spec_config(&pwm_setting);
		} else {
			LEDS_DEBUG("[LEDS]Error level in backlight\n");
			mt_pwm_disable(pwm_setting.pwm_no,
			               config_data->pmic_pad);
		}

		return 0;

	}
}

void mt_led_pwm_disable(int pwm_num)
{
	struct cust_mt65xx_led *cust_led_list = get_cust_led_dtsi();

	mt_pwm_disable(pwm_num, cust_led_list->config_data.pmic_pad);
}

void mt_backlight_set_pwm_duty(int pwm_num, u32 level, u32 div,
                               struct PWM_config *config_data)
{
	mt_backlight_set_pwm(pwm_num, level, div, config_data);
}

void mt_backlight_set_pwm_div(int pwm_num, u32 level, u32 div,
                              struct PWM_config *config_data)
{
	mt_backlight_set_pwm(pwm_num, level, div, config_data);
}

void mt_backlight_get_pwm_fsel(unsigned int bl_div, unsigned int *bl_frequency)
{

}

void mt_store_pwm_register(unsigned int addr, unsigned int value)
{

}

unsigned int mt_show_pwm_register(unsigned int addr)
{
	return 0;
}

int mt_brightness_set_pmic(enum mt65xx_led_pmic pmic_type, u32 level, u32 div)
{
	static bool first_time = true;

	LEDS_DEBUG("PMIC#%d:%d\n", pmic_type, level);
	mutex_lock(&leds_pmic_mutex);
	if (pmic_type == MT65XX_LED_PMIC_NLED_ISINK0) {
		if ((button_flag_isink0 == 0) && (first_time == true)) {
			/* button
				flag ==0, means this ISINK is not for button backlight */
			if (button_flag_isink1 == 0)
				pmic_set_register_value(PMIC_ISINK_CH1_EN, NLED_OFF);	/* sw
			workround for sync leds status */
			if (button_flag_isink2 == 0)
				pmic_set_register_value(PMIC_ISINK_CH2_EN,
				                        NLED_OFF);
			if (button_flag_isink3 == 0)
				pmic_set_register_value(PMIC_ISINK_CH3_EN,
				                        NLED_OFF);
			first_time = false;
		}
		pmic_set_register_value(PMIC_RG_DRV_32K_CK_PDN, 0x0);	/* Disable power down */
		pmic_set_register_value(PMIC_RG_DRV_ISINK0_CK_PDN, 0);
		pmic_set_register_value(PMIC_RG_DRV_ISINK0_CK_CKSEL, 0);
		pmic_set_register_value(PMIC_ISINK_CH0_MODE, ISINK_PWM_MODE);
		pmic_set_register_value(PMIC_ISINK_CH0_STEP, ISINK_3);	/* 16mA */
		pmic_set_register_value(PMIC_ISINK_DIM0_DUTY, 15);
		pmic_set_register_value(PMIC_ISINK_DIM0_FSEL, ISINK_1KHZ);	/* 1KHz */
		if (level)
			pmic_set_register_value(PMIC_ISINK_CH0_EN, NLED_ON);
		else
			pmic_set_register_value(PMIC_ISINK_CH0_EN, NLED_OFF);
		mutex_unlock(&leds_pmic_mutex);
		return 0;
	} else if (pmic_type == MT65XX_LED_PMIC_NLED_ISINK1) {
		if ((button_flag_isink1 == 0) && (first_time == true)) {
			/* button
				flag ==0, means this ISINK is not for button backlight */
			if (button_flag_isink0 == 0)
				pmic_set_register_value(PMIC_ISINK_CH0_EN, NLED_OFF);	/* sw
				workround for sync leds status */
			if (button_flag_isink2 == 0)
				pmic_set_register_value(PMIC_ISINK_CH2_EN,
				                        NLED_OFF);
			if (button_flag_isink3 == 0)
				pmic_set_register_value(PMIC_ISINK_CH3_EN,
				                        NLED_OFF);
			first_time = false;
		}
		pmic_set_register_value(PMIC_RG_DRV_32K_CK_PDN, 0x0);	/* Disable power down */
		pmic_set_register_value(PMIC_RG_DRV_ISINK1_CK_PDN, 0);
		pmic_set_register_value(PMIC_RG_DRV_ISINK1_CK_CKSEL, 0);
		pmic_set_register_value(PMIC_ISINK_CH1_MODE, ISINK_PWM_MODE);
		pmic_set_register_value(PMIC_ISINK_CH1_STEP, ISINK_3);	/* 16mA */
		pmic_set_register_value(PMIC_ISINK_DIM1_DUTY, 15);
		pmic_set_register_value(PMIC_ISINK_DIM1_FSEL, ISINK_1KHZ);	/* 1KHz */
		if (level)
			pmic_set_register_value(PMIC_ISINK_CH1_EN, NLED_ON);
		else
			pmic_set_register_value(PMIC_ISINK_CH1_EN, NLED_OFF);
		mutex_unlock(&leds_pmic_mutex);
		return 0;
	}
	mutex_unlock(&leds_pmic_mutex);
	return -1;
}

int mt_brightness_set_pmic_duty_store(u32 level, u32 div)
{
	return -1;
}
// Mike implement rgb workaround.
static int cci_rgb_workaround(const struct cust_mt65xx_led *cust, int level);
int mt_mt65xx_led_set_cust(struct cust_mt65xx_led *cust, int level)
{
	struct nled_setting led_tmp_setting = { 0, 0, 0 };
	int tmp_level = level;
	static bool button_flag;
	unsigned int BacklightLevelSupport =
	    Cust_GetBacklightLevelSupport_byPWM();

	switch (cust->mode) {

	case MT65XX_LED_MODE_PWM:
		if (strcmp(cust->name, "lcd-backlight") == 0) {
			bl_brightness_hal = level;
			if (level == 0) {
				mt_pwm_disable(cust->data,
				               cust->config_data.pmic_pad);

			} else {

				if (BacklightLevelSupport ==
				        BACKLIGHT_LEVEL_PWM_256_SUPPORT)
					level = brightness_mapping(tmp_level);
				else
					level = brightness_mapto64(tmp_level);
				mt_backlight_set_pwm(cust->data, level,
				                     bl_div_hal,
				                     &cust->config_data);
			}
			bl_duty_hal = level;

		} else {
			if (level == 0) {
				led_tmp_setting.nled_mode = NLED_OFF;
				mt_led_set_pwm(cust->data, &led_tmp_setting);
				mt_pwm_disable(cust->data,
				               cust->config_data.pmic_pad);
			} else {
				led_tmp_setting.nled_mode = NLED_ON;
				mt_led_set_pwm(cust->data, &led_tmp_setting);
			}
		}
		return 1;

	case MT65XX_LED_MODE_GPIO:
		LEDS_DEBUG("brightness_set_cust:go GPIO mode!!!!!\n");
		return ((cust_set_brightness) (cust->data)) (level);

	case MT65XX_LED_MODE_PMIC:
		/* for button baclight used SINK channel, when set button ISINK,
			don't do disable other ISINK channel */
		if ((strcmp(cust->name, "button-backlight") == 0)) {
			if (button_flag == false) {
				switch (cust->data) {
				case MT65XX_LED_PMIC_NLED_ISINK0:
					button_flag_isink0 = 1;
					break;
				case MT65XX_LED_PMIC_NLED_ISINK1:
					button_flag_isink1 = 1;
					break;
				case MT65XX_LED_PMIC_NLED_ISINK2:
					button_flag_isink2 = 1;
					break;
				case MT65XX_LED_PMIC_NLED_ISINK3:
					button_flag_isink3 = 1;
					break;
				default:
					break;
				}
				button_flag = true;
			}
		}
		return cci_rgb_workaround(cust, level);

	case MT65XX_LED_MODE_CUST_LCM:
		if (strcmp(cust->name, "lcd-backlight") == 0)
			bl_brightness_hal = level;
		LEDS_DEBUG("brightness_set_cust:backlight control by LCM\n");
		/* warning for this API revork */
		return ((cust_brightness_set) (cust->data)) (level, bl_div_hal);

	case MT65XX_LED_MODE_CUST_BLS_PWM:
		if (strcmp(cust->name, "lcd-backlight") == 0)
			bl_brightness_hal = level;
		return ((cust_set_brightness) (cust->data)) (level);

	case MT65XX_LED_MODE_NONE:
	default:
		break;
	}
	return -1;
}

void mt_mt65xx_led_work(struct work_struct *work)
{
	struct mt65xx_led_data *led_data =
	    container_of(work, struct mt65xx_led_data, work);

	LEDS_DEBUG("%s:%d\n", led_data->cust.name, led_data->level);
	mutex_lock(&leds_mutex);
	mt_mt65xx_led_set_cust(&led_data->cust, led_data->level);
	mutex_unlock(&leds_mutex);
}

void mt_mt65xx_led_set(struct led_classdev *led_cdev, enum led_brightness level)
{
	struct mt65xx_led_data *led_data =
	    container_of(led_cdev, struct mt65xx_led_data, cdev);
	/* unsigned long flags; */
	/* spin_lock_irqsave(&leds_lock, flags); */

#ifdef CONFIG_MTK_AAL_SUPPORT
	if (led_data->level != level) {
		led_data->level = level;
		if (strcmp(led_data->cust.name, "lcd-backlight") != 0) {
			LEDS_DEBUG("Set NLED directly %d at time %lu\n",
			           led_data->level, jiffies);
			schedule_work(&led_data->work);
		} else {
			if (level != 0
			        && level * CONFIG_LIGHTNESS_MAPPING_VALUE < 255) {
				level = 1;
			} else {
				level =
				    (level * CONFIG_LIGHTNESS_MAPPING_VALUE) /
				    255;
			}
			LEDS_DEBUG
			("Set Backlight directly %d at time %lu, mapping level is %d\n",
			 led_data->level, jiffies, level);
			/* mt_mt65xx_led_set_cust(&led_data->cust, led_data->level); */
			disp_aal_notify_backlight_changed((((1 <<
			                                     MT_LED_INTERNAL_LEVEL_BIT_CNT)
			                                    - 1) * level +
			                                   127) / 255);
		}
	}
#else
	/* do something only when level is changed */
	if (led_data->level != level) {
		led_data->level = level;
		if (strcmp(led_data->cust.name, "lcd-backlight") != 0) {
			LEDS_DEBUG("Set NLED directly %d at time %lu\n",
			           led_data->level, jiffies);
			schedule_work(&led_data->work);
		} else {
			if (level != 0
			        && level * CONFIG_LIGHTNESS_MAPPING_VALUE < 255) {
				level = 1;
			} else {
				level =
				    (level * CONFIG_LIGHTNESS_MAPPING_VALUE) /
				    255;
			}
			LEDS_DEBUG
			("Set Backlight directly %d at time %lu, mapping level is %d\n",
			 led_data->level, jiffies, level);
			if (MT65XX_LED_MODE_CUST_BLS_PWM == led_data->cust.mode) {
				mt_mt65xx_led_set_cust(&led_data->cust,
				                       ((((1 <<
				                           MT_LED_INTERNAL_LEVEL_BIT_CNT)
				                          - 1) * level +
				                         127) / 255));
			} else {
				mt_mt65xx_led_set_cust(&led_data->cust, level);
			}
		}
	}
	/* spin_unlock_irqrestore(&leds_lock, flags); */
#endif
	/* if(0!=aee_kernel_Powerkey_is_press()) */
	/* aee_kernel_wdt_kick_Powkey_api("mt_mt65xx_led_set",WDT_SETBY_Backlight); */
}
// Mike, must implement workaround solution here.
// Access g_leds_data; (mt65xx_led_data*) directly.
static void cmos_on(u8 bits);
static void cmos_free(void);
static void _set_isinks(u8 steps[], u8 dutys[]);
static inline u8 _map_blink_level(u8 color);
int mt_mt65xx_blink_set(struct led_classdev *led_cdev,
                        unsigned long *delay_on, unsigned long *delay_off)
{
	struct mt65xx_led_data *led_data =
	    container_of(led_cdev, struct mt65xx_led_data, cdev);
	// static int got_wake_lock;
	ulong on = *delay_on, off = *delay_off;

	// Mike, by checking dts, only mode PMIC and Backlight used here. But backlight won't use this one.
	if (led_data->cust.mode != MT65XX_LED_MODE_PMIC) {
		LEDS_DEBUG("mt_mt65xx_blink_set, unaccept led, who:%s, mode: %d\n", led_data->cust.name, led_data->cust.mode);
		return -1;
	}

	if (on == led_data->delay_on && off == led_data->delay_off) {
		LEDS_DEBUG("mt_mt65xx_blink_set, no need to update blink, who: %s\n", led_data->cust.name);
		return -1;
	} // Lazy ensure the led blinking is needed.

	if (on==0 && off == 0) {
		u8 steps[2] = {0}, dutys[2] = {0};
		_set_isinks(steps,dutys); // close it.
		led_data->delay_on = led_data->delay_off = 0;
		return 0;
	}
	// ignore no on but off, and case there is no off but on ... (invalid config.)
	if (g_leds_data == 0) {
		LEDS_DEBUG("g_leds_data null\n");
		return -1;
	}
	if (g_leds_data[0] == 0 || g_leds_data[1]==0 || g_leds_data[2]==0) {
		LEDS_DEBUG("g_leds_data[?] null\n");
		return -1;
	}
	/*
	if (g_leds_data[0]->cdev==0 || g_leds_data[1]->cdev==0 || g_leds_data[2]->cdev==0 ) {
		LEDS_DEBUG("g_leds_data[]->cdev is null\n");
		return -1;
	}*/
	do {
		struct nled_setting nled_tmp_setting = { .nled_mode=NLED_BLINK, .blink_level=0,
											.blink_on_time = on, .blink_off_time = off};
		int R = g_leds_data[MT65XX_LED_TYPE_RED]->cdev.blink_brightness;
		int G = g_leds_data[MT65XX_LED_TYPE_GREEN]->cdev.blink_brightness;
		int B = g_leds_data[MT65XX_LED_TYPE_BLUE]->cdev.blink_brightness;
		if (R) {
			g_leds_data[MT65XX_LED_TYPE_RED]->delay_on = on;
			g_leds_data[MT65XX_LED_TYPE_RED]->delay_off = off;
			g_leds_data[MT65XX_LED_TYPE_RED]->level = 255;
		}
		if (G) {
			g_leds_data[MT65XX_LED_TYPE_GREEN]->delay_on = on;
			g_leds_data[MT65XX_LED_TYPE_GREEN]->delay_off = off;
			g_leds_data[MT65XX_LED_TYPE_GREEN]->level = 255;
		}
		if (B) {
			g_leds_data[MT65XX_LED_TYPE_BLUE]->delay_on = on;
			g_leds_data[MT65XX_LED_TYPE_BLUE]->delay_off = off;
			g_leds_data[MT65XX_LED_TYPE_BLUE]->level = 255;
		}
		if (R) {
			if (R==G && R==B) {
				LEDS_DEBUG("strict white\n");
				nled_tmp_setting.blink_level = ISINK_1;
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK0, &nled_tmp_setting);
				cmos_on(2+1);
				nled_tmp_setting.blink_level = ISINK_5;
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK1, &nled_tmp_setting);
				return 0;
			}
			if (G!=0 && B!=0) {
				LEDS_DEBUG("generic white\n");
				nled_tmp_setting.blink_level = ISINK_3;
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK0, &nled_tmp_setting);
				cmos_on(2+1);
				nled_tmp_setting.blink_level = ISINK_5;
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK1, &nled_tmp_setting);
				return 0;
			}
			if (G!=0 && B==0) {
				LEDS_DEBUG("blink yellow\n");
				nled_tmp_setting.blink_level = ISINK_5;
				cmos_on(2);
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK0, &nled_tmp_setting);
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK1, &nled_tmp_setting);
				return 0;
			}
			if (G==0 && B!=0) {
				LEDS_DEBUG("blink pink\n");
				nled_tmp_setting.blink_level = ISINK_5;
				cmos_on(1);
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK0, &nled_tmp_setting);
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK1, &nled_tmp_setting);
				return 0;
			}
			if (G==0 && B==0) {
				LEDS_DEBUG("blink red\n");
				cmos_free();
				pmic_set_register_value(PMIC_ISINK_CH1_EN, NLED_OFF);
				nled_tmp_setting.blink_level = _map_blink_level(R); // ISINK_5;
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK0, &nled_tmp_setting);
				return 0;
			}
		}
		else { // no red.
			pmic_set_register_value(PMIC_ISINK_CH0_EN, NLED_OFF);
			if (G!=0 && B==0) {
				LEDS_DEBUG("blink green\n");
				nled_tmp_setting.blink_level = _map_blink_level(G); //ISINK_5;
				cmos_on(2);
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK1, &nled_tmp_setting);
				return 0;
			}
			if (G==0 && B!=0) {
				LEDS_DEBUG("blink blue\n");
				nled_tmp_setting.blink_level = _map_blink_level(B); //ISINK_5;
				cmos_on(1);
				mt_led_blink_pmic(MT65XX_LED_PMIC_NLED_ISINK1, &nled_tmp_setting);
				return 0;
			}
		}
	}while(0);
	return 0;
#if 0
	/* only allow software blink when delay_on or delay_off changed */
	if (*delay_on != led_data->delay_on
	        || *delay_off != led_data->delay_off) {
		led_data->delay_on = *delay_on;
		led_data->delay_off = *delay_off;
		if (led_data->delay_on && led_data->delay_off) {	/* enable blink */
			led_data->level = 255;	/* when enable blink  then to set the level  (255) */
			/* AP PWM all support OLD mode */
			if (led_data->cust.mode == MT65XX_LED_MODE_PWM) {
				nled_tmp_setting.nled_mode = NLED_BLINK;
				nled_tmp_setting.blink_off_time =
				    led_data->delay_off;
				nled_tmp_setting.blink_on_time =
				    led_data->delay_on;
				mt_led_set_pwm(led_data->cust.data,
				               &nled_tmp_setting);
				return 0;
			} else if ((led_data->cust.mode == MT65XX_LED_MODE_PMIC)
			           && (led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK0
			               || led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK1
			               || led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK2
			               || led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK3)) { // Mike, blinking goes here.
				nled_tmp_setting.nled_mode = NLED_BLINK;
				nled_tmp_setting.blink_off_time = led_data->delay_off;
				nled_tmp_setting.blink_on_time = led_data->delay_on;
				mt_led_blink_pmic(led_data->cust.data, &nled_tmp_setting); // #1: which isink, #2: the blinking setting, (on/off, brightness)
				return 0;
			} else if (!got_wake_lock) {
				wake_lock(&leds_suspend_lock);
				got_wake_lock = 1;
			}
		} else if (!led_data->delay_on && !led_data->delay_off) {	/* disable blink */
			/* AP PWM all support OLD mode */
			if (led_data->cust.mode == MT65XX_LED_MODE_PWM) {
				nled_tmp_setting.nled_mode = NLED_OFF;
				mt_led_set_pwm(led_data->cust.data,
				               &nled_tmp_setting);
				return 0;
			} else if ((led_data->cust.mode == MT65XX_LED_MODE_PMIC)
			           && (led_data->cust.data ==
			               MT65XX_LED_PMIC_NLED_ISINK0
			               || led_data->cust.data ==
			               MT65XX_LED_PMIC_NLED_ISINK1
			               || led_data->cust.data ==
			               MT65XX_LED_PMIC_NLED_ISINK2
			               || led_data->cust.data ==
			               MT65XX_LED_PMIC_NLED_ISINK3)) {
				mt_brightness_set_pmic(led_data->cust.data, 0,
				                       0);
				return 0;
			} else if (got_wake_lock) {
				wake_unlock(&leds_suspend_lock);
				got_wake_lock = 0;
			}
		}
		return -1;
	}
	/* delay_on and delay_off are not changed */
	return 0;
#endif
}

static inline u8 _map_blink_level(u8 color) {
	if (color<43) return 0;
	if (color<=85) return 1;
	if (color<128) return 2;
	if (color<=170) return 3;
	if (color<213) return 4;
	return 5;
}


#define GPIO_CHG_LED_CTL       (93 + 0x80000000)
#define GPIO_LEDB_CTL          (82 + 0x80000000)
#define GPIO_LEDR_CTL          (83 + 0x80000000)
#define GPIO_LEDG_CTL          (128 + 0x80000000)
static void _set_isinks(u8 steps[], u8 dutys[]);
static inline void fetch_pwm_config(const struct device_node * const node, u8 level, u8 *const step , u8 *const duty);
// Mike,
//  This is a workaround to mix RGB 3 leds into 2 ISINK.
//  Return: ref to mt_brightness_set_pmic() return.
static int cci_rgb_workaround(const struct cust_mt65xx_led *cust, int level)
{
	static int RR,GG,BB;
	unsigned int R,G,B;
	static struct device_node *nodes[3] = {0};
	// enum MT65XX_PMIC_ISINK_STEP step = 0, step2 = 0;
	// u8 duty = 0, duty2 = 0;
	u8 steps[2] = {0};
	u8 dutys[2] = {0};
	// static struct mt65xx_led_data *led_data[3];
	// const struct mt65xx_led_data *led_data = container_of(cust, const struct mt65xx_led_data, cust);
	LEDS_DEBUG("@ %s; level: %d; name %s; mode %d; data %ld\n", __func__, level, cust->name, cust->mode, cust->data);
	// LEDS_DEBUG("led_data %p, level %d\n", led_data, led_data->level);
	LEDS_DEBUG("leds_name: %p, cust.name: %p\n", leds_name[0], cust->name);
	if (nodes[0] == 0) {
		nodes[0] = of_find_compatible_node(NULL, NULL, "mediatek,red");
		nodes[1] = of_find_compatible_node(NULL, NULL, "mediatek,green");
		nodes[2] = of_find_compatible_node(NULL, NULL, "mediatek,blue");
	}

	do {
		const char *p = cust->name;
		LEDS_DEBUG("old RGB: %d, %d, %d\n", RR,GG,BB);
		if (p == leds_name[0]) RR = level;
		else if (p == leds_name[1]) GG = level;
		else if (p == leds_name[2]) BB = level;
		else LEDS_DEBUG("!!! Warning unknow RGB string pointer !!!\n");
		LEDS_DEBUG("new RGB: %d, %d, %d\n", RR,GG,BB);
	} while(0);
	do {
		R = RR==0?0:RR*40/256+1;
		G = GG==0?0:GG*40/256+1;
		B = BB==0?0:BB*40/256+1;
		LEDS_DEBUG("adjust rgb: %d, %d, %d\n", R,G,B);
	} while (0);
	if (R) {
		if ( R==G && G==B ) { // white
			LEDS_DEBUG("strict white\n");
			fetch_pwm_config(nodes[0], R/2, &steps[0], &dutys[0]);
			fetch_pwm_config(nodes[1], max(G,B), &steps[1], &dutys[1]);
			cmos_on(2+1);
			_set_isinks(steps, dutys);
		}
		if (B==0) { // yellow can mix normally.
			if (G) {
				LEDS_DEBUG("yellow\n");
				fetch_pwm_config(nodes[0], R, &steps[0], &dutys[0]);
				fetch_pwm_config(nodes[1], G, &steps[1], &dutys[1]);
				cmos_on(2);
				_set_isinks(steps, dutys);
			} else {
				LEDS_DEBUG("red\n");
				fetch_pwm_config(nodes[0], R, &steps[0], &dutys[0]);
				// cmos_on(0);
				cmos_free();
				_set_isinks(steps, dutys);
			}
		} else {
			if (G==0) {
				LEDS_DEBUG("pink\n");
				fetch_pwm_config(nodes[0], R, &steps[0], &dutys[0]);
				fetch_pwm_config(nodes[2], B, &steps[1], &dutys[1]);
				cmos_on(1);
				_set_isinks(steps, dutys);
			}
			else { // not strict white!
				LEDS_DEBUG("generic white\n");
				fetch_pwm_config(nodes[0], R/2, &steps[0], &dutys[0]);
				fetch_pwm_config(nodes[2], max(G,B), &steps[1], &dutys[1]);
				cmos_on(2+1);
				_set_isinks(steps, dutys);
			}
		}
	} else { // No red.
		if (G) {
			if (B==0) {
				LEDS_DEBUG("green\n");
				fetch_pwm_config(nodes[1], G, &steps[1], &dutys[1]);
				cmos_on(2);
				_set_isinks(steps, dutys);
			} else {
				LEDS_DEBUG("cyan\n");
				fetch_pwm_config(nodes[2], max(G,B), &steps[1], &dutys[1]);
				cmos_on(2+1);
				_set_isinks(steps, dutys);
			}
		} else { // no red or green
			if (B) {
				LEDS_DEBUG("blue!\n");
				fetch_pwm_config(nodes[2], B, &steps[1], &dutys[1]);
				cmos_on(1);
				_set_isinks(steps, dutys);;
			} else {
				cmos_free();
				_set_isinks(steps, dutys);
			}
		}
	}
	return 0;
}
// Ref APIs: int mt_brightness_set_pmic(enum mt65xx_led_pmic pmic_type, u32 level, u32 div);
// pmic_type := { MT65XX_LED_PMIC_NLED_ISINK0, MT65XX_LED_PMIC_NLED_ISINK1};
#if 1
static inline void fetch_pwm_config(const struct device_node * const node, u8 level, u8 *const step , u8 *const duty)
{
	int ret = 0;
	u32 data = 0;
	ret = of_property_read_u32_index(node, "cei,pwm_currents", level, &data);
	if (ret) goto error;
	*step = (u8) data;
	ret = of_property_read_u32_index(node, "cei,pwm_dutys", level, &data);
	if (ret) goto error;
	*duty = (u8) data;
	LEDS_DEBUG("level:%d, step:%d duty:%d\n", level, *step, *duty);
	return;
error:
	*step = 0;
	*duty = 0;
	pr_err("Error, fetch pwm configr!");
	return;
}
#endif

#if 1
static void _set_isinks(u8 steps[], u8 dutys[])
{
	LEDS_DEBUG("isink setting, 0:[%d,%d], 1:[%d,%d]\n", steps[0],dutys[0],steps[1],dutys[1]);
	mutex_lock(&leds_pmic_mutex);
	pmic_set_register_value(PMIC_RG_DRV_32K_CK_PDN, 0x0);   /* Disable power down */
	pmic_set_register_value(PMIC_RG_DRV_ISINK0_CK_PDN, 0);
	pmic_set_register_value(PMIC_RG_DRV_ISINK0_CK_CKSEL, 0);
	pmic_set_register_value(PMIC_ISINK_CH0_MODE, ISINK_PWM_MODE);
	pmic_set_register_value(PMIC_ISINK_CH0_STEP, clamp_val(steps[0], ISINK_0,ISINK_5) );  /* 16mA */
	pmic_set_register_value(PMIC_ISINK_DIM0_FSEL, ISINK_1KHZ);  /* 1KHz */
	if (dutys[0] == 0 && steps[0] == 0) {
		pmic_set_register_value(PMIC_ISINK_CH0_EN, NLED_OFF);
	} else {
		pmic_set_register_value(PMIC_ISINK_DIM0_DUTY, clamp_val(dutys[0],1,32) - 1);
		pmic_set_register_value(PMIC_ISINK_CH0_EN, NLED_ON);
	}
	// insink #1
	pmic_set_register_value(PMIC_RG_DRV_32K_CK_PDN, 0x0);   /* Disable power down */
	pmic_set_register_value(PMIC_RG_DRV_ISINK1_CK_PDN, 0);
	pmic_set_register_value(PMIC_RG_DRV_ISINK1_CK_CKSEL, 0);
	pmic_set_register_value(PMIC_ISINK_CH1_MODE, ISINK_PWM_MODE);
	pmic_set_register_value(PMIC_ISINK_CH1_STEP, clamp_val(steps[1], ISINK_0,ISINK_5));  /* 16mA */
	pmic_set_register_value(PMIC_ISINK_DIM1_FSEL, ISINK_1KHZ);  /* 1KHz */
	if (dutys[1] == 0 && steps[1] == 0) {
		pmic_set_register_value(PMIC_ISINK_CH1_EN, NLED_OFF);
	} else {
		pmic_set_register_value(PMIC_ISINK_DIM1_DUTY, clamp_val(dutys[1], 1,32) - 1);
		pmic_set_register_value(PMIC_ISINK_CH1_EN, NLED_ON);
	}
	mutex_unlock(&leds_pmic_mutex);
}
#endif

extern enum cei_hw_type get_cei_hw_id(void);
static void cmos_on(u8 bits) {
	switch( get_cei_hw_id() ) {
		case CEI_HW_DVT1:
		case CEI_HW_EVT1:
		case CEI_HW_EVT2:
			mt_set_gpio_out(GPIO_LEDR_CTL, (bits&0x04)? GPIO_OUT_ZERO:GPIO_OUT_ONE);
			mt_set_gpio_out(GPIO_LEDG_CTL, (bits&0x02)? GPIO_OUT_ZERO:GPIO_OUT_ONE);
			mt_set_gpio_out(GPIO_LEDB_CTL, (bits&0x01)? GPIO_OUT_ZERO:GPIO_OUT_ONE);
			break;
		case CEI_HW_DVT2:
		default:
			mt_set_gpio_out(GPIO_LEDR_CTL, (bits&0x04)? GPIO_OUT_ONE:GPIO_OUT_ZERO);
			mt_set_gpio_out(GPIO_LEDG_CTL, (bits&0x02)? GPIO_OUT_ONE:GPIO_OUT_ZERO);
			mt_set_gpio_out(GPIO_LEDB_CTL, (bits&0x01)? GPIO_OUT_ONE:GPIO_OUT_ZERO);
			break;
	}
}

static void cmos_free() {
	mt_set_gpio_out(GPIO_LEDR_CTL, GPIO_OUT_ZERO);
	mt_set_gpio_out(GPIO_LEDG_CTL, GPIO_OUT_ZERO);
	mt_set_gpio_out(GPIO_LEDB_CTL, GPIO_OUT_ZERO);
}
#if 0
#define GET_CURRENT_STEP() do { \
 
} while(0)
	ret = of_property_read_u32_index(node,
	                                 CEI_HWID_GPIOS_PROPERTY, i, &cei_hwid_gpios[i].gpio);
#define CEI_HWID_GPIOS_PROPERTY     "cei,hwid-gpios"
#endif
