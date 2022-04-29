#ifndef __CCI_HW_ID_H
#define __CCI_HW_ID_H

/* Implement CCI HW ID/PROJECT ID */

#define CEI_HWID_STRING_LEN 10
#define NUM_OF_HWID_GPIO 6

enum cei_hw_type {
	CEI_HW_EVT1 = 0,       /* PDP1 */
	CEI_HW_EVT2 = 1,       /* PDP2  */
	CEI_HW_DVT1 = 2,       /* SP  */
	CEI_HW_DVT2 = 3,       /* AP  */
	CEI_HW_TP     = 4,       /* TP  */
	CEI_HW_PVT   = 5,       /* PQ */
	CEI_HW_MP   = 6,         /* MP */
	CEI_HW_UNKNOWN = 7
};

enum cei_project_type {
	CEI_PROJECT_UNKNOWN = 0,
	CEI_PROJECT_CY32 = 1,
	CEI_PROJECT_CY33 = 2,
#ifdef  CEI_PROJ_DUAL_SIM	
	CEI_PROJECT_CY35 = 3,
	CEI_PROJECT_CY36 = 4
#endif
};

enum cei_hwid_gpio_index {
	CEI_HWID_GPIO_INDEX_START    = 0,
	CEI_HWID_GPIO_INDEX_END       = 2,
	CEI_RFID_GPIO_INDEX_START      = 3,
	CEI_RFID_GPIO_INDEX_END         = 5,
};

enum cei_sim_type {
	CEI_SIM_SS = 0,
	CEI_SIM_DS = 1,
	CEI_SIM_UNKNOWN = 2
};

enum cei_rf_type {
	CEI_RF_GINA = 0,
	CEI_RF_REX = 1,
	CEI_RF_GINA_APAC = 2,
	CEI_RF_RESERVED = 3,
	CEI_RF_UNKNOWN = 4
};

enum cei_lcd_type {
	CEI_LCD_SECOND = 0,
	CEI_LCD_MAIN = 1,
	CEI_LCD_UNKNOWN = 2
};

/*
 * API to get CEI HWID information:
 *
 * get_cei_hw_id()-      return enum cei_hw_type
 * get_cei_project_id()- return enum cei_project_type
 *
 * Please use **enum variable** in your function to get the return cei_***_type
 */
 
extern enum cei_hw_type get_cei_hw_id(void); 
extern enum cei_project_type get_cei_project_id(void);

#endif /* __CCI_HW_ID_H */
