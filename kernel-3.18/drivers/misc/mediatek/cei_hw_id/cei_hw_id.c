#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <mt-plat/mt_gpio.h>
#include <mt-plat/mt_gpio_core.h>
#include <linux/gpio.h>
#include "cei_hw_id.h"

#define CEI_HWID_GPIO_NAME_PROPERTY "cei,hwid-gpio-names"
#define CEI_HWID_GPIOS_PROPERTY 	"cei,hwid-gpios"

#define CEI_HWID_DEBUG

#define GPIO_LCD_ID 97

//#define CEI_PROJ_DUAL_SIM

struct hwid_pinctrl {
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_act;
};

struct hwid_gpio {
	const char *gpio_name;
	int gpio;	
}; 

struct hwid_info {
	enum cei_hw_type hw_id;
	enum cei_project_type project_id;
	enum cei_rf_type rf_id;
	enum cei_sim_type sim_id;
	enum cei_lcd_type lcd_id;
};

static struct hwid_pinctrl cei_hwid_pinctrl;
static struct hwid_info cei_hwid_info;
static struct hwid_gpio cei_hwid_gpios[NUM_OF_HWID_GPIO];

/*** @OWNER NEED TO DEFINE ***/
static const char cei_hw_type_str[][CEI_HWID_STRING_LEN] = 
	{"EVT1", "EVT2", "DVT1", "DVT2", "TP", "PVT", "MP","UNKNOWN"};

static const char j68_hw_type_str[][CEI_HWID_STRING_LEN] = 
	{"PDP1", "PDP2", "SP", "AP", "TP", "PQ", "MP", "UNKNOWN"};

#ifdef CEI_PROJ_DUAL_SIM
static const char cei_project_type_str[][CEI_HWID_STRING_LEN] = 
	{"UNKNOWN", "CY32", "CY33", "CY35", "CY36"};
#else
static const char cei_project_type_str[][CEI_HWID_STRING_LEN] = 
	{"UNKNOWN", "CY32", "CY33"};
#endif

static const char cei_rf_type_str[][CEI_HWID_STRING_LEN] = 
	{"GINA", "REX", "GINA_APAC", "RESERVED", "UNKNOWN"};

static const char cei_sim_type_str[][CEI_HWID_STRING_LEN] = 
	{"SS", "DS", "UNKNOWN"};

static const char cei_lcd_type_str[][CEI_HWID_STRING_LEN] =
	{"SECOND", "MAIN", "UNKNOWN"};

#ifdef CEI_HWID_DEBUG
void cei_hwid_property_debug(void) 
{
	int i = 0;

	printk(KERN_INFO "DEBUG cei,hwid-gpios\n");
	for (i = 0; i < NUM_OF_HWID_GPIO; i++) {
		printk(KERN_INFO "[%d]\n", cei_hwid_gpios[i].gpio);
	}
	
	printk(KERN_INFO "DEBUG cei,hwid-gpio-names\n");
	for (i = 0; i < NUM_OF_HWID_GPIO; i++) {
		printk(KERN_INFO "[%s]\n", cei_hwid_gpios[i].gpio_name);
	}
}
#endif

int cei_hwid_parse_dt(struct device *dev)
{
	int i = 0, ret;
	struct device_node *node = dev->of_node;
	//struct device_node *node;
	//node = of_find_compatible_node(NULL, NULL, "compal, cei_hwid");
	
	printk(KERN_INFO "cei_hwid_parse_dt_gpio\n");
	if (node) {		
		/* Get gpio and gpio name from DT */
		for (i = 0; i < NUM_OF_HWID_GPIO; i++) {
			ret = of_property_read_string_index(node, 
				CEI_HWID_GPIO_NAME_PROPERTY, i, &cei_hwid_gpios[i].gpio_name);
			if (ret) {
				dev_err(dev, "couldn't read gpio-hwids string [%d] name\n", i);
				return -EINVAL;
			}			
			ret = of_property_read_u32_index(node, 
				CEI_HWID_GPIOS_PROPERTY, i, &cei_hwid_gpios[i].gpio);
			if (ret) {
				dev_err(dev, "couldn't read gpio-hwids %d gpio!\n", i);
				return -EINVAL;
			}
		}
		#ifdef CEI_HWID_DEBUG
		cei_hwid_property_debug();
		#endif
	
		/* Get pinctrl from DT */
		cei_hwid_pinctrl.pinctrl = devm_pinctrl_get(dev);
		if (IS_ERR(cei_hwid_pinctrl.pinctrl)) {
			goto gpio_pinctrl_err;
		}

		cei_hwid_pinctrl.pinctrl_act = 
			pinctrl_lookup_state(cei_hwid_pinctrl.pinctrl, "cei_hwid_pinctrl_active");
		if (IS_ERR(cei_hwid_pinctrl.pinctrl_act)) {
			goto gpio_pinctrl_err;
		}
	
	} else {
		printk(KERN_WARNING "cei_hwid_parse_dt : of_node err\n");
		return -EINVAL;
	}
	return 0;
	
gpio_pinctrl_err:
	printk(KERN_WARNING "cei_hwid_parse_dt : pinctrl err\n");
	return -EINVAL;
	
}

int parse_rf_id(int rfid)
{
	int ret;

	switch (rfid) {
	case 0:
	//case 4:		
		ret = CEI_RF_GINA;
		break;
	case 1:
	//case 5:
		ret = CEI_RF_REX;
		break;
	default:
		ret = CEI_RF_UNKNOWN;
	}
	return ret;
}

int parse_sim_id(int rfid)
{
	int ret;

	switch (rfid) {
	case 0:
	case 1:		
		ret = CEI_SIM_SS;
		break;
	//case 4:
	//case 5:
	//	ret = CEI_SIM_DS;
	//	break;
	default:
		ret = CEI_SIM_UNKNOWN;
	}
	return ret;
}

int parse_project_id(int rfid)
{
	int ret;

	switch (rfid) {
	case 0:
		ret = CEI_PROJECT_CY33;
		break;
	case 1:
		ret = CEI_PROJECT_CY32;
		break;
	//case 4:
	//	ret = CEI_PROJECT_CY35;
	//	break;
	//case 5:
	//	ret = CEI_PROJECT_CY36;
	//	break;
	default:
		ret = CEI_PROJECT_UNKNOWN;
	}
	return ret;
}

void cei_hwid_detect(void) 
{
	int i = 0, hwid = 0, rfid = 0, ret = 0;

#ifdef CONFIG_HWID_GPIO_BY_MTK_API
	printk(KERN_INFO "cei_hwid_detect config HWID by mt API\n");
	for (i = 0; i < NUM_OF_HWID_GPIO; i++) {
		mt_set_gpio_mode(cei_hwid_gpios[i].gpio, 0);
		mt_set_gpio_dir(cei_hwid_gpios[i].gpio, GPIO_DIR_IN);
		mt_set_gpio_pull_enable(cei_hwid_gpios[i].gpio, GPIO_PULL_DISABLE);
		gpio_request(cei_hwid_gpios[i].gpio, cei_hwid_gpios[i].gpio_name);
	}
	ret = 0;
#else
	/* Configure HWID gpio */
	printk(KERN_INFO "cei_hwid_detect config HWID by pinctrl\n");
	for (i = 0; i < NUM_OF_HWID_GPIO; i++) {
		gpio_request(cei_hwid_gpios[i].gpio, cei_hwid_gpios[i].gpio_name);
		gpio_direction_input(cei_hwid_gpios[i].gpio);
	}
	ret = pinctrl_select_state(cei_hwid_pinctrl.pinctrl, cei_hwid_pinctrl.pinctrl_act);
	if (ret)
		printk(KERN_ERR "cei_hwid_pinctrl pinctrl_select_state err\n");
#endif

	/* Config LCD ID gpio */
	gpio_request(GPIO_LCD_ID, "cei_gpio_lcdid");
	mt_set_gpio_mode(GPIO_LCD_ID, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_LCD_ID, GPIO_DIR_IN);

	/*** 
	  * @OWNER
	  * Get HWID gpio value:
	  * HWID1 HWID2 HWID3 | HWID6 HWID5 HWID4 | 
	  * |<-      HW ID          ->|<-          RF ID          ->|
	  *
	  * Read hwid gpio value from "large" bit to "smal"l bit"
	     //printk(KERN_INFO "hwid_gpios[%d]=%d value=%d\n", i, 
	     //	cei_hwid_gpios[i].gpio, gpio_get_value(cei_hwid_gpios[i].gpio));	  
	  *
	  */	

	for ( i = CEI_HWID_GPIO_INDEX_START; i <= CEI_HWID_GPIO_INDEX_END; i++) 
		hwid += gpio_get_value(cei_hwid_gpios[i].gpio) << (CEI_HWID_GPIO_INDEX_END - i);
	
	for ( i = CEI_RFID_GPIO_INDEX_START; i <= CEI_RFID_GPIO_INDEX_END; i++) 
		rfid += gpio_get_value(cei_hwid_gpios[i].gpio) << (CEI_RFID_GPIO_INDEX_END - i);

	cei_hwid_info.hw_id = hwid;
	cei_hwid_info.rf_id = parse_rf_id(rfid);
	cei_hwid_info.project_id  = parse_project_id(rfid);
	cei_hwid_info.sim_id  = parse_sim_id(rfid);
	cei_hwid_info.lcd_id = gpio_get_value(GPIO_LCD_ID);

	if (hwid == 7) { /*Force MP definition to 6 for framework property definition*/
		cei_hwid_info.hw_id = CEI_HW_MP;
	}

	printk(KERN_INFO "cei_project_id=%d, cei_hw_id=%d, cei_rf_id=%d, cei_sim_id=%d, cei_lcd_id=%d\n",
				cei_hwid_info.project_id,
				cei_hwid_info.hw_id,
				cei_hwid_info.rf_id,
				cei_hwid_info.sim_id,
				cei_hwid_info.lcd_id);

	for (i = 0; i < NUM_OF_HWID_GPIO; i++) 
		gpio_free(cei_hwid_gpios[i].gpio);

	gpio_free(GPIO_LCD_ID);
}

/*
 * API to get CEI HWID information:
 *
 * get_cei_hw_id()-      return enum cei_hw_type
 * get_cei_project_id()- return enum cei_project_type
 *
 * Enum definition is defined in cei_hw_id.h
 */
enum cei_hw_type get_cei_hw_id(void)
{
	return cei_hwid_info.hw_id;
}
EXPORT_SYMBOL(get_cei_hw_id);

enum cei_project_type get_cei_project_id(void)
{
	return cei_hwid_info.project_id;
}
EXPORT_SYMBOL(get_cei_project_id);

static int cei_hwid_info_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "projectid=%d hwid=%d rfid=%d simid=%d lcdid=%d\n", 
				cei_hwid_info.project_id,
				cei_hwid_info.hw_id, 
				cei_hwid_info.rf_id,
				cei_hwid_info.sim_id,
				cei_hwid_info.lcd_id);
	return 0;
}

static int cei_hwid_info_string_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s=%s %s=%s(%s) %s=%s %s=%s %s=%s\n", 
				"projectid", cei_project_type_str[cei_hwid_info.project_id], 
				"hwid", cei_hw_type_str [cei_hwid_info.hw_id], j68_hw_type_str[cei_hwid_info.hw_id],
				"rfid", cei_rf_type_str[cei_hwid_info.rf_id],
				"simid", cei_sim_type_str[cei_hwid_info.sim_id],
				"lcdid", cei_lcd_type_str[cei_hwid_info.lcd_id]);
	return 0;
}

static int cei_hwid_info_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cei_hwid_info_proc_show, NULL);
}

static int cei_hwid_info_string_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cei_hwid_info_string_proc_show, NULL);
}

static const struct file_operations cei_hwid_info_string_proc_fops = {
	.open	= cei_hwid_info_string_proc_open,
	.read	= seq_read,
	.llseek	= seq_lseek,
	.release	= single_release,
};

static const struct file_operations cei_hwid_info_proc_fops = {
	.open	= cei_hwid_info_proc_open,
	.read	= seq_read,
	.llseek	= seq_lseek,
	.release	= single_release,
};

static const struct of_device_id cei_hw_id_of_match[] = {
	{ .compatible = "compal, cei_hwid", },
	{ },
};
MODULE_DEVICE_TABLE(of, cei_hw_id_of_match);


static int cei_hw_id_probe(struct platform_device *pdev)
{
	int ret = 0;

	printk(KERN_INFO "cei_hw_id_probe\n");

	/* Initialize cei_hwid_info */
	cei_hwid_info.project_id = CEI_PROJECT_UNKNOWN;
	cei_hwid_info.hw_id = CEI_HW_UNKNOWN;
	cei_hwid_info.rf_id = CEI_RF_UNKNOWN;
	cei_hwid_info.sim_id = CEI_SIM_UNKNOWN;
	cei_hwid_info.lcd_id = CEI_LCD_UNKNOWN;
	
	/* Get cei hwid info from dt */

	ret = cei_hwid_parse_dt(&pdev->dev);
	if (ret)
		printk(KERN_INFO "cei_hwid_parse_dt error=%d\n", ret);

	cei_hwid_detect();

	/* Create proc node for userspace */
	proc_create("cei_hwid_info",0,NULL,&cei_hwid_info_proc_fops);
	proc_create("cei_hwid_info_string",0,NULL,&cei_hwid_info_string_proc_fops);	
	
	return 0;
}

static int cei_hw_id_remove(struct platform_device *pdev)
{
	printk(KERN_INFO "CEI HWID remove\n");
	return 0;
}

static struct platform_driver cei_hw_id_driver = {
	.probe      = cei_hw_id_probe,
	.remove     = cei_hw_id_remove,
	.driver = {
		.name = "cei-hwid-driver",
		.owner = THIS_MODULE,
		.of_match_table = cei_hw_id_of_match,
	},
};

static int __init cei_hw_id_init(void)
{
	printk(KERN_INFO "cei_hw_id_init\n");
	return platform_driver_register(&cei_hw_id_driver);
}
static void __exit cei_hw_id_exit(void)
{
	printk(KERN_INFO "cei_hw_id_exit\n");
	platform_driver_unregister(&cei_hw_id_driver);
}

module_init(cei_hw_id_init);
module_exit(cei_hw_id_exit);

MODULE_DESCRIPTION("cci hardware ID driver");
MODULE_LICENSE("GPL");
