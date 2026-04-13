/**************************************************************************//**
*
* @copyright (C) 2019 Nuvoton Technology Corp. All rights reserved.
*
* SPDX-License-Identifier: Apache-2.0
*
* Change Logs:
* Date            Author       Notes
* 2021-9-3        Wayne        First version
*
******************************************************************************/

#include <rtconfig.h>

#if defined(NU_PKG_USING_DA9062)

#include <rtthread.h>
#include <rtdevice.h>
#include "da9062_reg.h"

#define DBG_ENABLE
#define DBG_LEVEL DBG_LOG
#define DBG_SECTION_NAME  "da9062"
#define DBG_COLOR
#include <rtdbg.h>

struct regmap_range
{
    uint32_t range_min;
    uint32_t range_max;
};
#define regmap_reg_range(low, high) { .range_min = low, .range_max = high, }
#define ARRAY_SIZE(arr)    (sizeof(arr) / sizeof((arr)[0]))

#define BYTE_TO_BINARY_PATTERN "%c %c %c %c %c %c %c %c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')


#define DEF_DA9062_PAGE0_SLAVEADDR    (0xB0>>1)

static struct rt_i2c_bus_device *g_psNuDA9062 = RT_NULL;

static const struct regmap_range da9062_aa_readable_ranges[] =
{
    regmap_reg_range(DA9062AA_PAGE_CON, DA9062AA_STATUS_B),
    regmap_reg_range(DA9062AA_STATUS_D, DA9062AA_EVENT_C),
    regmap_reg_range(DA9062AA_IRQ_MASK_A, DA9062AA_IRQ_MASK_C),
    regmap_reg_range(DA9062AA_CONTROL_A, DA9062AA_GPIO_4),
    regmap_reg_range(DA9062AA_GPIO_WKUP_MODE, DA9062AA_BUCK4_CONT),
    regmap_reg_range(DA9062AA_BUCK3_CONT, DA9062AA_BUCK3_CONT),
    regmap_reg_range(DA9062AA_LDO1_CONT, DA9062AA_LDO4_CONT),
    regmap_reg_range(DA9062AA_DVC_1, DA9062AA_DVC_1),
    regmap_reg_range(DA9062AA_COUNT_S, DA9062AA_SECOND_D),
    regmap_reg_range(DA9062AA_SEQ, DA9062AA_ID_4_3),
    regmap_reg_range(DA9062AA_ID_12_11, DA9062AA_ID_16_15),
    regmap_reg_range(DA9062AA_ID_22_21, DA9062AA_ID_32_31),
    regmap_reg_range(DA9062AA_SEQ_A, DA9062AA_BUCK3_CFG),
    regmap_reg_range(DA9062AA_VBUCK2_A, DA9062AA_VBUCK4_A),
    regmap_reg_range(DA9062AA_VBUCK3_A, DA9062AA_VBUCK3_A),
    regmap_reg_range(DA9062AA_VLDO1_A, DA9062AA_VLDO4_A),
    regmap_reg_range(DA9062AA_VBUCK2_B, DA9062AA_VBUCK4_B),
    regmap_reg_range(DA9062AA_VBUCK3_B, DA9062AA_VBUCK3_B),
    regmap_reg_range(DA9062AA_VLDO1_B, DA9062AA_VLDO4_B),
    regmap_reg_range(DA9062AA_BBAT_CONT, DA9062AA_BBAT_CONT),
#if 0
    regmap_reg_range(DA9062AA_INTERFACE, DA9062AA_CONFIG_E),
    regmap_reg_range(DA9062AA_CONFIG_G, DA9062AA_CONFIG_K),
    regmap_reg_range(DA9062AA_CONFIG_M, DA9062AA_CONFIG_M),
    regmap_reg_range(DA9062AA_TRIM_CLDR, DA9062AA_GP_ID_19),
    regmap_reg_range(DA9062AA_DEVICE_ID, DA9062AA_CONFIG_ID),
#endif
};

static const struct regmap_range da9062_aa_writeable_ranges[] =
{
    regmap_reg_range(DA9062AA_PAGE_CON, DA9062AA_PAGE_CON),
    regmap_reg_range(DA9062AA_FAULT_LOG, DA9062AA_EVENT_C),
    regmap_reg_range(DA9062AA_IRQ_MASK_A, DA9062AA_IRQ_MASK_C),
    regmap_reg_range(DA9062AA_CONTROL_A, DA9062AA_GPIO_4),
    regmap_reg_range(DA9062AA_GPIO_WKUP_MODE, DA9062AA_BUCK4_CONT),
    regmap_reg_range(DA9062AA_BUCK3_CONT, DA9062AA_BUCK3_CONT),
    regmap_reg_range(DA9062AA_LDO1_CONT, DA9062AA_LDO4_CONT),
    regmap_reg_range(DA9062AA_DVC_1, DA9062AA_DVC_1),
    regmap_reg_range(DA9062AA_COUNT_S, DA9062AA_ALARM_Y),
    regmap_reg_range(DA9062AA_SEQ, DA9062AA_ID_4_3),
    regmap_reg_range(DA9062AA_ID_12_11, DA9062AA_ID_16_15),
    regmap_reg_range(DA9062AA_ID_22_21, DA9062AA_ID_32_31),
    regmap_reg_range(DA9062AA_SEQ_A, DA9062AA_BUCK3_CFG),
    regmap_reg_range(DA9062AA_VBUCK2_A, DA9062AA_VBUCK4_A),
    regmap_reg_range(DA9062AA_VBUCK3_A, DA9062AA_VBUCK3_A),
    regmap_reg_range(DA9062AA_VLDO1_A, DA9062AA_VLDO4_A),
    regmap_reg_range(DA9062AA_VBUCK2_B, DA9062AA_VBUCK4_B),
    regmap_reg_range(DA9062AA_VBUCK3_B, DA9062AA_VBUCK3_B),
    regmap_reg_range(DA9062AA_VLDO1_B, DA9062AA_VLDO4_B),
    regmap_reg_range(DA9062AA_BBAT_CONT, DA9062AA_BBAT_CONT),
    regmap_reg_range(DA9062AA_GP_ID_0, DA9062AA_GP_ID_19),
};

static const struct regmap_range da9062_aa_volatile_ranges[] =
{
    regmap_reg_range(DA9062AA_PAGE_CON, DA9062AA_STATUS_B),
    regmap_reg_range(DA9062AA_STATUS_D, DA9062AA_EVENT_C),
    regmap_reg_range(DA9062AA_CONTROL_A, DA9062AA_CONTROL_B),
    regmap_reg_range(DA9062AA_CONTROL_E, DA9062AA_CONTROL_F),
    regmap_reg_range(DA9062AA_BUCK2_CONT, DA9062AA_BUCK4_CONT),
    regmap_reg_range(DA9062AA_BUCK3_CONT, DA9062AA_BUCK3_CONT),
    regmap_reg_range(DA9062AA_LDO1_CONT, DA9062AA_LDO4_CONT),
    regmap_reg_range(DA9062AA_DVC_1, DA9062AA_DVC_1),
    regmap_reg_range(DA9062AA_COUNT_S, DA9062AA_SECOND_D),
    regmap_reg_range(DA9062AA_SEQ, DA9062AA_SEQ),
    regmap_reg_range(DA9062AA_EN_32K, DA9062AA_EN_32K),
};

struct da9062_reg_label
{
    uint16_t addr;
    const char *name;
};

static const struct da9062_reg_label da9062_reg_labels[] =
{
    {DA9062AA_PAGE_CON,       "PAGE_CON"},
    {DA9062AA_STATUS_A,       "STATUS_A"},
    {DA9062AA_STATUS_B,       "STATUS_B"},
    {DA9062AA_STATUS_D,       "STATUS_D"},
    {DA9062AA_FAULT_LOG,      "FAULT_LOG"},
    {DA9062AA_EVENT_A,        "EVENT_A"},
    {DA9062AA_EVENT_B,        "EVENT_B"},
    {DA9062AA_EVENT_C,        "EVENT_C"},
    {DA9062AA_IRQ_MASK_A,     "IRQ_MASK_A"},
    {DA9062AA_IRQ_MASK_B,     "IRQ_MASK_B"},
    {DA9062AA_IRQ_MASK_C,     "IRQ_MASK_C"},
    {DA9062AA_CONTROL_A,      "CONTROL_A"},
    {DA9062AA_CONTROL_B,      "CONTROL_B"},
    {DA9062AA_CONTROL_C,      "CONTROL_C"},
    {DA9062AA_CONTROL_D,      "CONTROL_D"},
    {DA9062AA_CONTROL_E,      "CONTROL_E"},
    {DA9062AA_CONTROL_F,      "CONTROL_F"},
    {DA9062AA_PD_DIS,         "PD_DIS"},
    {DA9062AA_GPIO_0_1,       "GPIO_0_1"},
    {DA9062AA_GPIO_2_3,       "GPIO_2_3"},
    {DA9062AA_GPIO_4,         "GPIO_4"},
    {DA9062AA_GPIO_WKUP_MODE, "GPIO_WKUP_MODE"},
    {DA9062AA_GPIO_MODE0_4,   "GPIO_MODE0_4"},
    {DA9062AA_GPIO_OUT0_2,    "GPIO_OUT0_2"},
    {DA9062AA_GPIO_OUT3_4,    "GPIO_OUT3_4"},
    {DA9062AA_BUCK2_CONT,     "BUCK2_CONT"},
    {DA9062AA_BUCK1_CONT,     "BUCK1_CONT"},
    {DA9062AA_BUCK4_CONT,     "BUCK4_CONT"},
    {DA9062AA_BUCK3_CONT,     "BUCK3_CONT"},
    {DA9062AA_LDO1_CONT,      "LDO1_CONT"},
    {DA9062AA_LDO2_CONT,      "LDO2_CONT"},
    {DA9062AA_LDO3_CONT,      "LDO3_CONT"},
    {DA9062AA_LDO4_CONT,      "LDO4_CONT"},
    {DA9062AA_DVC_1,          "DVC_1"},
    {DA9062AA_COUNT_S,        "COUNT_S"},
    {DA9062AA_COUNT_MI,       "COUNT_MI"},
    {DA9062AA_COUNT_H,        "COUNT_H"},
    {DA9062AA_COUNT_D,        "COUNT_D"},
    {DA9062AA_COUNT_MO,       "COUNT_MO"},
    {DA9062AA_COUNT_Y,        "COUNT_Y"},
    {DA9062AA_ALARM_S,        "ALARM_S"},
    {DA9062AA_ALARM_MI,       "ALARM_MI"},
    {DA9062AA_ALARM_H,        "ALARM_H"},
    {DA9062AA_ALARM_D,        "ALARM_D"},
    {DA9062AA_ALARM_MO,       "ALARM_MO"},
    {DA9062AA_ALARM_Y,        "ALARM_Y"},
    {DA9062AA_SECOND_A,       "SECOND_A"},
    {DA9062AA_SECOND_B,       "SECOND_B"},
    {DA9062AA_SECOND_C,       "SECOND_C"},
    {DA9062AA_SECOND_D,       "SECOND_D"},
    {DA9062AA_SEQ,            "SEQ"},
    {DA9062AA_SEQ_TIMER,      "SEQ_TIMER"},
    {DA9062AA_ID_2_1,         "ID_2_1"},
    {DA9062AA_ID_4_3,         "ID_4_3"},
    {DA9062AA_ID_12_11,       "ID_12_11"},
    {DA9062AA_ID_14_13,       "ID_14_13"},
    {DA9062AA_ID_16_15,       "ID_16_15"},
    {DA9062AA_ID_22_21,       "ID_22_21"},
    {DA9062AA_ID_24_23,       "ID_24_23"},
    {DA9062AA_ID_26_25,       "ID_26_25"},
    {DA9062AA_ID_28_27,       "ID_28_27"},
    {DA9062AA_ID_30_29,       "ID_30_29"},
    {DA9062AA_ID_32_31,       "ID_32_31"},
    {DA9062AA_SEQ_A,          "SEQ_A"},
    {DA9062AA_SEQ_B,          "SEQ_B"},
    {DA9062AA_WAIT,           "WAIT"},
    {DA9062AA_EN_32K,         "EN_32K"},
    {DA9062AA_RESET,          "RESET"},
    {DA9062AA_BUCK_ILIM_A,    "BUCK_ILIM_A"},
    {DA9062AA_BUCK_ILIM_B,    "BUCK_ILIM_B"},
    {DA9062AA_BUCK_ILIM_C,    "BUCK_ILIM_C"},
    {DA9062AA_BUCK2_CFG,      "BUCK2_CFG"},
    {DA9062AA_BUCK1_CFG,      "BUCK1_CFG"},
    {DA9062AA_BUCK4_CFG,      "BUCK4_CFG"},
    {DA9062AA_BUCK3_CFG,      "BUCK3_CFG"},
    {DA9062AA_VBUCK2_A,       "VBUCK2_A"},
    {DA9062AA_VBUCK1_A,       "VBUCK1_A"},
    {DA9062AA_VBUCK4_A,       "VBUCK4_A"},
    {DA9062AA_VBUCK3_A,       "VBUCK3_A"},
    {DA9062AA_VLDO1_A,        "VLDO1_A"},
    {DA9062AA_VLDO2_A,        "VLDO2_A"},
    {DA9062AA_VLDO3_A,        "VLDO3_A"},
    {DA9062AA_VLDO4_A,        "VLDO4_A"},
    {DA9062AA_VBUCK2_B,       "VBUCK2_B"},
    {DA9062AA_VBUCK1_B,       "VBUCK1_B"},
    {DA9062AA_VBUCK4_B,       "VBUCK4_B"},
    {DA9062AA_VBUCK3_B,       "VBUCK3_B"},
    {DA9062AA_VLDO1_B,        "VLDO1_B"},
    {DA9062AA_VLDO2_B,        "VLDO2_B"},
    {DA9062AA_VLDO3_B,        "VLDO3_B"},
    {DA9062AA_VLDO4_B,        "VLDO4_B"},
    {DA9062AA_BBAT_CONT,      "BBAT_CONT"},
    {DA9062AA_INTERFACE,      "INTERFACE"},
    {DA9062AA_CONFIG_A,       "CONFIG_A"},
    {DA9062AA_CONFIG_B,       "CONFIG_B"},
    {DA9062AA_CONFIG_C,       "CONFIG_C"},
    {DA9062AA_CONFIG_D,       "CONFIG_D"},
    {DA9062AA_CONFIG_E,       "CONFIG_E"},
    {DA9062AA_CONFIG_G,       "CONFIG_G"},
    {DA9062AA_CONFIG_H,       "CONFIG_H"},
    {DA9062AA_CONFIG_I,       "CONFIG_I"},
    {DA9062AA_CONFIG_J,       "CONFIG_J"},
    {DA9062AA_CONFIG_K,       "CONFIG_K"},
    {DA9062AA_CONFIG_M,       "CONFIG_M"},
    {DA9062AA_TRIM_CLDR,      "TRIM_CLDR"},
    {DA9062AA_GP_ID_0,        "GP_ID_0"},
    {DA9062AA_GP_ID_1,        "GP_ID_1"},
    {DA9062AA_GP_ID_2,        "GP_ID_2"},
    {DA9062AA_GP_ID_3,        "GP_ID_3"},
    {DA9062AA_GP_ID_4,        "GP_ID_4"},
    {DA9062AA_GP_ID_5,        "GP_ID_5"},
    {DA9062AA_GP_ID_6,        "GP_ID_6"},
    {DA9062AA_GP_ID_7,        "GP_ID_7"},
    {DA9062AA_GP_ID_8,        "GP_ID_8"},
    {DA9062AA_GP_ID_9,        "GP_ID_9"},
    {DA9062AA_GP_ID_10,       "GP_ID_10"},
    {DA9062AA_GP_ID_11,       "GP_ID_11"},
    {DA9062AA_GP_ID_12,       "GP_ID_12"},
    {DA9062AA_GP_ID_13,       "GP_ID_13"},
    {DA9062AA_GP_ID_14,       "GP_ID_14"},
    {DA9062AA_GP_ID_15,       "GP_ID_15"},
    {DA9062AA_GP_ID_16,       "GP_ID_16"},
    {DA9062AA_GP_ID_17,       "GP_ID_17"},
    {DA9062AA_GP_ID_18,       "GP_ID_18"},
    {DA9062AA_GP_ID_19,       "GP_ID_19"},
    {DA9062AA_DEVICE_ID,      "DEVICE_ID"},
    {DA9062AA_VARIANT_ID,     "VARIANT_ID"},
    {DA9062AA_CUSTOMER_ID,    "CUSTOMER_ID"},
    {DA9062AA_CONFIG_ID,      "CONFIG_ID"},
};

static const char *da9062_reg_name(uint16_t addr)
{
    for (int i = 0; i < (int)ARRAY_SIZE(da9062_reg_labels); i++)
    {
        if (da9062_reg_labels[i].addr == addr)
            return da9062_reg_labels[i].name;
    }
    return "UNKNOWN";
}

static void da9062_decode_reg(uint16_t addr, uint8_t val, char *buf, int bufsize)
{
    if (buf == RT_NULL || bufsize <= 0)
    {
        return;
    }

    switch (addr)
    {
    case DA9062AA_PAGE_CON:
        rt_snprintf(buf, bufsize, "page=%u write_mode=%u revert=%u", val & DA9062AA_PAGE_MASK,
                    (val & DA9062AA_WRITE_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_REVERT_MASK) ? 1 : 0);
        return;

    case DA9062AA_STATUS_A:
        rt_snprintf(buf, bufsize, "nonkey=%u dvc_busy=%u", val & DA9062AA_NONKEY_MASK,
                    (val & DA9062AA_DVC_BUSY_MASK) ? 1 : 0);
        return;

    case DA9062AA_STATUS_B:
        rt_snprintf(buf, bufsize, "gpi0=%u gpi1=%u gpi2=%u gpi3=%u gpi4=%u",
                    val & DA9062AA_GPI0_MASK ? 1 : 0,
                    val & DA9062AA_GPI1_MASK ? 1 : 0,
                    val & DA9062AA_GPI2_MASK ? 1 : 0,
                    val & DA9062AA_GPI3_MASK ? 1 : 0,
                    val & DA9062AA_GPI4_MASK ? 1 : 0);
        return;

    case DA9062AA_STATUS_D:
        rt_snprintf(buf, bufsize, "ldo1_ilim=%u ldo2_ilim=%u ldo3_ilim=%u ldo4_ilim=%u",
                    val & DA9062AA_LDO1_ILIM_MASK ? 1 : 0,
                    val & DA9062AA_LDO2_ILIM_MASK ? 1 : 0,
                    val & DA9062AA_LDO3_ILIM_MASK ? 1 : 0,
                    val & DA9062AA_LDO4_ILIM_MASK ? 1 : 0);
        return;

    case DA9062AA_FAULT_LOG:
        rt_snprintf(buf, bufsize, "twd_error=%u por=%u vdd_fault=%u vdd_start=%u temp_crit=%u key_reset=%u nshutdown=%u wait_shut=%u",
                    val & DA9062AA_TWD_ERROR_MASK ? 1 : 0,
                    val & DA9062AA_POR_MASK ? 1 : 0,
                    val & DA9062AA_VDD_FAULT_MASK ? 1 : 0,
                    val & DA9062AA_VDD_START_MASK ? 1 : 0,
                    val & DA9062AA_TEMP_CRIT_MASK ? 1 : 0,
                    val & DA9062AA_KEY_RESET_MASK ? 1 : 0,
                    val & DA9062AA_NSHUTDOWN_MASK ? 1 : 0,
                    val & DA9062AA_WAIT_SHUT_MASK ? 1 : 0);
        return;

    case DA9062AA_EVENT_A:
        rt_snprintf(buf, bufsize, "e_nonkey=%u e_alarm=%u e_tick=%u e_wdg_warn=%u e_seq_rdy=%u events_b=%u events_c=%u",
                    val & DA9062AA_E_NONKEY_MASK ? 1 : 0,
                    val & DA9062AA_E_ALARM_MASK ? 1 : 0,
                    val & DA9062AA_E_TICK_MASK ? 1 : 0,
                    val & DA9062AA_E_WDG_WARN_MASK ? 1 : 0,
                    val & DA9062AA_E_SEQ_RDY_MASK ? 1 : 0,
                    val & DA9062AA_EVENTS_B_MASK ? 1 : 0,
                    val & DA9062AA_EVENTS_C_MASK ? 1 : 0);
        return;

    case DA9062AA_EVENT_B:
        rt_snprintf(buf, bufsize, "e_temp=%u e_ldo_lim=%u e_dvc_rdy=%u e_vdd_warn=%u",
                    val & DA9062AA_E_TEMP_MASK ? 1 : 0,
                    val & DA9062AA_E_LDO_LIM_MASK ? 1 : 0,
                    val & DA9062AA_E_DVC_RDY_MASK ? 1 : 0,
                    val & DA9062AA_E_VDD_WARN_MASK ? 1 : 0);
        return;

    case DA9062AA_EVENT_C:
        rt_snprintf(buf, bufsize, "e_gpi0=%u e_gpi1=%u e_gpi2=%u e_gpi3=%u e_gpi4=%u",
                    val & DA9062AA_E_GPI0_MASK ? 1 : 0,
                    val & DA9062AA_E_GPI1_MASK ? 1 : 0,
                    val & DA9062AA_E_GPI2_MASK ? 1 : 0,
                    val & DA9062AA_E_GPI3_MASK ? 1 : 0,
                    val & DA9062AA_E_GPI4_MASK ? 1 : 0);
        return;

    case DA9062AA_IRQ_MASK_A:
        rt_snprintf(buf, bufsize, "m_nonkey=%u m_alarm=%u m_tick=%u m_wdg_warn=%u m_seq_rdy=%u",
                    val & DA9062AA_M_NONKEY_MASK ? 1 : 0,
                    val & DA9062AA_M_ALARM_MASK ? 1 : 0,
                    val & DA9062AA_M_TICK_MASK ? 1 : 0,
                    val & DA9062AA_M_WDG_WARN_MASK ? 1 : 0,
                    val & DA9062AA_M_SEQ_RDY_MASK ? 1 : 0);
        return;

    case DA9062AA_IRQ_MASK_B:
        rt_snprintf(buf, bufsize, "m_temp=%u m_ldo_lim=%u m_dvc_rdy=%u m_vdd_warn=%u",
                    val & DA9062AA_M_TEMP_MASK ? 1 : 0,
                    val & DA9062AA_M_LDO_LIM_MASK ? 1 : 0,
                    val & DA9062AA_M_DVC_RDY_MASK ? 1 : 0,
                    val & DA9062AA_M_VDD_WARN_MASK ? 1 : 0);
        return;

    case DA9062AA_IRQ_MASK_C:
        rt_snprintf(buf, bufsize, "m_gpi0=%u m_gpi1=%u m_gpi2=%u m_gpi3=%u m_gpi4=%u",
                    val & DA9062AA_M_GPI0_MASK ? 1 : 0,
                    val & DA9062AA_M_GPI1_MASK ? 1 : 0,
                    val & DA9062AA_M_GPI2_MASK ? 1 : 0,
                    val & DA9062AA_M_GPI3_MASK ? 1 : 0,
                    val & DA9062AA_M_GPI4_MASK ? 1 : 0);
        return;

    case DA9062AA_CONTROL_A:
        rt_snprintf(buf, bufsize, "system_en=%u power_en=%u power1_en=%u standby=%u",
                    (val & DA9062AA_SYSTEM_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_POWER_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_POWER1_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_STANDBY_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONTROL_B:
        rt_snprintf(buf, bufsize, "watchdog_pd=%u freeze_en=%u nres_mode=%u nonkey_lock=%u nfreeze=%u buck_slowstart=%u",
                    (val & DA9062AA_WATCHDOG_PD_MASK) ? 1 : 0,
                    (val & DA9062AA_FREEZE_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_NRES_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_NONKEY_LOCK_MASK) ? 1 : 0,
                    (val & DA9062AA_NFREEZE_MASK) >> DA9062AA_NFREEZE_SHIFT,
                    (val & DA9062AA_BUCK_SLOWSTART_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONTROL_C:
        rt_snprintf(buf, bufsize, "debouncing=%u auto_boot=%u otpread_en=%u slew_rate=%u def_supply=%u",
                    (val & DA9062AA_DEBOUNCING_MASK),
                    (val & DA9062AA_AUTO_BOOT_MASK) ? 1 : 0,
                    (val & DA9062AA_OTPREAD_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_SLEW_RATE_MASK) >> DA9062AA_SLEW_RATE_SHIFT,
                    (val & DA9062AA_DEF_SUPPLY_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONTROL_D:
        rt_snprintf(buf, bufsize, "twdscale=%u",
                    (val & DA9062AA_TWDSCALE_MASK));
        return;

    case DA9062AA_CONTROL_E:
        rt_snprintf(buf, bufsize, "rtc_mode_pd=%u rtc_mode_sd=%u rtc_en=%u v_lock=%u",
                    (val & DA9062AA_RTC_MODE_PD_MASK) ? 1 : 0,
                    (val & DA9062AA_RTC_MODE_SD_MASK) ? 1 : 0,
                    (val & DA9062AA_RTC_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_V_LOCK_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONTROL_F:
        rt_snprintf(buf, bufsize, "watchdog=%u shutdown=%u wake_up=%u",
                    (val & DA9062AA_WATCHDOG_MASK) ? 1 : 0,
                    (val & DA9062AA_SHUTDOWN_MASK) ? 1 : 0,
                    (val & DA9062AA_WAKE_UP_MASK) ? 1 : 0);
        return;

    case DA9062AA_PD_DIS:
        rt_snprintf(buf, bufsize, "gpi_dis=%u pmif_dis=%u cldr_pause=%u bbat_dis=%u out32k_pause=%u pmcont_dis=%u",
                    (val & DA9062AA_GPI_DIS_MASK) ? 1 : 0,
                    (val & DA9062AA_PMIF_DIS_MASK) ? 1 : 0,
                    (val & DA9062AA_CLDR_PAUSE_MASK) ? 1 : 0,
                    (val & DA9062AA_BBAT_DIS_MASK) ? 1 : 0,
                    (val & DA9062AA_OUT32K_PAUSE_MASK) ? 1 : 0,
                    (val & DA9062AA_PMCONT_DIS_MASK) ? 1 : 0);
        return;

    case DA9062AA_GPIO_0_1:
        rt_snprintf(buf, bufsize, "gpio0_pin=%u gpio0_type=%u gpio0_wen=%u gpio1_pin=%u gpio1_type=%u gpio1_wen=%u",
                    (val & DA9062AA_GPIO0_PIN_MASK),
                    (val & DA9062AA_GPIO0_TYPE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO0_WEN_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO1_PIN_MASK) >> DA9062AA_GPIO1_PIN_SHIFT,
                    (val & DA9062AA_GPIO1_TYPE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO1_WEN_MASK) ? 1 : 0);
        return;

    case DA9062AA_GPIO_2_3:
        rt_snprintf(buf, bufsize, "gpio2_pin=%u gpio2_type=%u gpio2_wen=%u gpio3_pin=%u gpio3_type=%u gpio3_wen=%u",
                    (val & DA9062AA_GPIO2_PIN_MASK),
                    (val & DA9062AA_GPIO2_TYPE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO2_WEN_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO3_PIN_MASK) >> DA9062AA_GPIO3_PIN_SHIFT,
                    (val & DA9062AA_GPIO3_TYPE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO3_WEN_MASK) ? 1 : 0);
        return;

    case DA9062AA_GPIO_4:
        rt_snprintf(buf, bufsize, "gpio4_pin=%u gpio4_type=%u gpio4_wen=%u",
                    (val & DA9062AA_GPIO4_PIN_MASK),
                    (val & DA9062AA_GPIO4_TYPE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO4_WEN_MASK) ? 1 : 0);
        return;

    case DA9062AA_GPIO_WKUP_MODE:
        rt_snprintf(buf, bufsize, "gpio0_wkup=%u gpio1_wkup=%u gpio2_wkup=%u gpio3_wkup=%u gpio4_wkup=%u",
                    (val & DA9062AA_GPIO0_WKUP_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO1_WKUP_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO2_WKUP_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO3_WKUP_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO4_WKUP_MODE_MASK) ? 1 : 0);
        return;

    case DA9062AA_GPIO_MODE0_4:
        rt_snprintf(buf, bufsize, "gpio0_mode=%u gpio1_mode=%u gpio2_mode=%u gpio3_mode=%u gpio4_mode=%u",
                    (val & DA9062AA_GPIO0_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO1_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO2_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO3_MODE_MASK) ? 1 : 0,
                    (val & DA9062AA_GPIO4_MODE_MASK) ? 1 : 0);
        return;

    case DA9062AA_GPIO_OUT0_2:
        rt_snprintf(buf, bufsize, "gpio0_out=%u gpio1_out=%u gpio2_out=%u",
                    (val & DA9062AA_GPIO0_OUT_MASK),
                    (val & DA9062AA_GPIO1_OUT_MASK) >> DA9062AA_GPIO1_OUT_SHIFT,
                    (val & DA9062AA_GPIO2_OUT_MASK) >> DA9062AA_GPIO2_OUT_SHIFT);
        return;

    case DA9062AA_GPIO_OUT3_4:
        rt_snprintf(buf, bufsize, "gpio3_out=%u gpio4_out=%u",
                    (val & DA9062AA_GPIO3_OUT_MASK),
                    (val & DA9062AA_GPIO4_OUT_MASK) >> DA9062AA_GPIO4_OUT_SHIFT);
        return;

    case DA9062AA_BUCK2_CONT:
        rt_snprintf(buf, bufsize, "buck2_en=%u buck2_gpi=%u buck2_conf=%u vbuck2_gpi=%u",
                    (val & DA9062AA_BUCK2_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_BUCK2_GPI_MASK) >> DA9062AA_BUCK2_GPI_SHIFT,
                    (val & DA9062AA_BUCK2_CONF_MASK) ? 1 : 0,
                    (val & DA9062AA_VBUCK2_GPI_MASK) >> DA9062AA_VBUCK2_GPI_SHIFT);
        return;

    case DA9062AA_BUCK1_CONT:
        rt_snprintf(buf, bufsize, "buck1_en=%u buck1_gpi=%u buck1_conf=%u vbuck1_gpi=%u",
                    (val & DA9062AA_BUCK1_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_BUCK1_GPI_MASK) >> DA9062AA_BUCK1_GPI_SHIFT,
                    (val & DA9062AA_BUCK1_CONF_MASK) ? 1 : 0,
                    (val & DA9062AA_VBUCK1_GPI_MASK) >> DA9062AA_VBUCK1_GPI_SHIFT);
        return;

    case DA9062AA_BUCK4_CONT:
        rt_snprintf(buf, bufsize, "buck4_en=%u buck4_gpi=%u buck4_conf=%u vbuck4_gpi=%u",
                    (val & DA9062AA_BUCK4_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_BUCK4_GPI_MASK) >> DA9062AA_BUCK4_GPI_SHIFT,
                    (val & DA9062AA_BUCK4_CONF_MASK) ? 1 : 0,
                    (val & DA9062AA_VBUCK4_GPI_MASK) >> DA9062AA_VBUCK4_GPI_SHIFT);
        return;

    case DA9062AA_BUCK3_CONT:
        rt_snprintf(buf, bufsize, "buck3_en=%u buck3_gpi=%u buck3_conf=%u vbuck3_gpi=%u",
                    (val & DA9062AA_BUCK3_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_BUCK3_GPI_MASK) >> DA9062AA_BUCK3_GPI_SHIFT,
                    (val & DA9062AA_BUCK3_CONF_MASK) ? 1 : 0,
                    (val & DA9062AA_VBUCK3_GPI_MASK) >> DA9062AA_VBUCK3_GPI_SHIFT);
        return;

    case DA9062AA_LDO1_CONT:
        rt_snprintf(buf, bufsize, "ldo1_en=%u ldo1_gpi=%u ldo1_pd_dis=%u vldo1_gpi=%u ldo1_conf=%u",
                    (val & DA9062AA_LDO1_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_LDO1_GPI_MASK) >> DA9062AA_LDO1_GPI_SHIFT,
                    (val & DA9062AA_LDO1_PD_DIS_MASK) ? 1 : 0,
                    (val & DA9062AA_VLDO1_GPI_MASK) >> DA9062AA_VLDO1_GPI_SHIFT,
                    (val & DA9062AA_LDO1_CONF_MASK) ? 1 : 0);
        return;

    case DA9062AA_LDO2_CONT:
        rt_snprintf(buf, bufsize, "ldo2_en=%u ldo2_gpi=%u ldo2_pd_dis=%u vldo2_gpi=%u ldo2_conf=%u",
                    (val & DA9062AA_LDO2_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_LDO2_GPI_MASK) >> DA9062AA_LDO2_GPI_SHIFT,
                    (val & DA9062AA_LDO2_PD_DIS_MASK) ? 1 : 0,
                    (val & DA9062AA_VLDO2_GPI_MASK) >> DA9062AA_VLDO2_GPI_SHIFT,
                    (val & DA9062AA_LDO2_CONF_MASK) ? 1 : 0);
        return;

    case DA9062AA_LDO3_CONT:
        rt_snprintf(buf, bufsize, "ldo3_en=%u ldo3_gpi=%u ldo3_pd_dis=%u vldo3_gpi=%u ldo3_conf=%u",
                    (val & DA9062AA_LDO3_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_LDO3_GPI_MASK) >> DA9062AA_LDO3_GPI_SHIFT,
                    (val & DA9062AA_LDO3_PD_DIS_MASK) ? 1 : 0,
                    (val & DA9062AA_VLDO3_GPI_MASK) >> DA9062AA_VLDO3_GPI_SHIFT,
                    (val & DA9062AA_LDO3_CONF_MASK) ? 1 : 0);
        return;

    case DA9062AA_LDO4_CONT:
        rt_snprintf(buf, bufsize, "ldo4_en=%u ldo4_gpi=%u ldo4_pd_dis=%u vldo4_gpi=%u ldo4_conf=%u",
                    (val & DA9062AA_LDO4_EN_MASK) ? 1 : 0,
                    (val & DA9062AA_LDO4_GPI_MASK) >> DA9062AA_LDO4_GPI_SHIFT,
                    (val & DA9062AA_LDO4_PD_DIS_MASK) ? 1 : 0,
                    (val & DA9062AA_VLDO4_GPI_MASK) >> DA9062AA_VLDO4_GPI_SHIFT,
                    (val & DA9062AA_LDO4_CONF_MASK) ? 1 : 0);
        return;

    case DA9062AA_DVC_1:
        rt_snprintf(buf, bufsize, "vldo1_sel=%u vldo2_sel=%u vldo3_sel=%u vldo4_sel=%u vbuck3_sel=%u vbuck4_sel=%u vbuck2_sel=%u vbuck1_sel=%u",
                    (val & DA9062AA_VLDO1_SEL_MASK) >> DA9062AA_VLDO1_SEL_SHIFT,
                    (val & DA9062AA_VLDO2_SEL_MASK) >> DA9062AA_VLDO2_SEL_SHIFT,
                    (val & DA9062AA_VLDO3_SEL_MASK) >> DA9062AA_VLDO3_SEL_SHIFT,
                    (val & DA9062AA_VLDO4_SEL_MASK) >> DA9062AA_VLDO4_SEL_SHIFT,
                    (val & DA9062AA_VBUCK3_SEL_MASK) >> DA9062AA_VBUCK3_SEL_SHIFT,
                    (val & DA9062AA_VBUCK4_SEL_MASK) >> DA9062AA_VBUCK4_SEL_SHIFT,
                    (val & DA9062AA_VBUCK2_SEL_MASK) >> DA9062AA_VBUCK2_SEL_SHIFT,
                    (val & DA9062AA_VBUCK1_SEL_MASK) >> DA9062AA_VBUCK1_SEL_SHIFT);
        return;

    case DA9062AA_COUNT_S:
        rt_snprintf(buf, bufsize, "count_sec=%u", val & DA9062AA_COUNT_SEC_MASK);
        return;

    case DA9062AA_COUNT_MI:
        rt_snprintf(buf, bufsize, "count_min=%u", val & DA9062AA_COUNT_MIN_MASK);
        return;

    case DA9062AA_COUNT_H:
        rt_snprintf(buf, bufsize, "count_hour=%u", val & DA9062AA_COUNT_HOUR_MASK);
        return;

    case DA9062AA_COUNT_D:
        rt_snprintf(buf, bufsize, "count_day=%u", val & DA9062AA_COUNT_DAY_MASK);
        return;

    case DA9062AA_COUNT_MO:
        rt_snprintf(buf, bufsize, "count_month=%u", val & DA9062AA_COUNT_MONTH_MASK);
        return;

    case DA9062AA_COUNT_Y:
        rt_snprintf(buf, bufsize, "count_year=%u", val & DA9062AA_COUNT_YEAR_MASK);
        return;

    case DA9062AA_ALARM_S:
        rt_snprintf(buf, bufsize, "alarm_sec=%u alarm_status=%u", val & DA9062AA_ALARM_SEC_MASK, (val & DA9062AA_ALARM_STATUS_MASK) ? 1 : 0);
        return;

    case DA9062AA_ALARM_MI:
        rt_snprintf(buf, bufsize, "alarm_min=%u", val & DA9062AA_ALARM_MIN_MASK);
        return;

    case DA9062AA_ALARM_H:
        rt_snprintf(buf, bufsize, "alarm_hour=%u", val & DA9062AA_ALARM_HOUR_MASK);
        return;

    case DA9062AA_ALARM_D:
        rt_snprintf(buf, bufsize, "alarm_day=%u", val & DA9062AA_ALARM_DAY_MASK);
        return;

    case DA9062AA_ALARM_MO:
        rt_snprintf(buf, bufsize, "tick_wake=%u tick_type=%u alarm_month=%u", (val & DA9062AA_TICK_WAKE_MASK) ? 1 : 0, (val & DA9062AA_TICK_TYPE_MASK) ? 1 : 0, val & DA9062AA_ALARM_MONTH_MASK);
        return;

    case DA9062AA_ALARM_Y:
        rt_snprintf(buf, bufsize, "tick_on=%u alarm_on=%u alarm_year=%u", (val & DA9062AA_TICK_ON_MASK) ? 1 : 0, (val & DA9062AA_ALARM_ON_MASK) ? 1 : 0, val & DA9062AA_ALARM_YEAR_MASK);
        return;

    case DA9062AA_SECOND_A:
        rt_snprintf(buf, bufsize, "seconds_a=%u", val & DA9062AA_SECONDS_A_MASK);
        return;

    case DA9062AA_SECOND_B:
        rt_snprintf(buf, bufsize, "seconds_b=%u", val & DA9062AA_SECONDS_B_MASK);
        return;

    case DA9062AA_SECOND_C:
        rt_snprintf(buf, bufsize, "seconds_c=%u", val & DA9062AA_SECONDS_C_MASK);
        return;

    case DA9062AA_SECOND_D:
        rt_snprintf(buf, bufsize, "seconds_d=%u", val & DA9062AA_SECONDS_D_MASK);
        return;

    case DA9062AA_SEQ:
        rt_snprintf(buf, bufsize, "seq_pointer=%u nxt_seq_start=%u", (val & DA9062AA_SEQ_POINTER_MASK) >> DA9062AA_SEQ_POINTER_SHIFT, (val & DA9062AA_NXT_SEQ_START_MASK) >> DA9062AA_NXT_SEQ_START_SHIFT);
        return;

    case DA9062AA_SEQ_TIMER:
        rt_snprintf(buf, bufsize, "seq_time=%u seq_dummy=%u", (val & DA9062AA_SEQ_TIME_MASK) >> DA9062AA_SEQ_TIME_SHIFT, (val & DA9062AA_SEQ_DUMMY_MASK) >> DA9062AA_SEQ_DUMMY_SHIFT);
        return;

    case DA9062AA_ID_2_1:
        rt_snprintf(buf, bufsize, "ldo2_step=%u ldo1_step=%u", (val & DA9062AA_LDO2_STEP_MASK) >> DA9062AA_LDO2_STEP_SHIFT, (val & DA9062AA_LDO1_STEP_MASK) >> DA9062AA_LDO1_STEP_SHIFT);
        return;

    case DA9062AA_ID_4_3:
        rt_snprintf(buf, bufsize, "ldo4_step=%u ldo3_step=%u", (val & DA9062AA_LDO4_STEP_MASK) >> DA9062AA_LDO4_STEP_SHIFT, (val & DA9062AA_LDO3_STEP_MASK) >> DA9062AA_LDO3_STEP_SHIFT);
        return;

    case DA9062AA_ID_12_11:
        rt_snprintf(buf, bufsize, "pd_dis_step=%u", (val & DA9062AA_PD_DIS_STEP_MASK) >> DA9062AA_PD_DIS_STEP_SHIFT);
        return;

    case DA9062AA_ID_14_13:
        rt_snprintf(buf, bufsize, "buck1_step=%u buck4_step=%u", (val & DA9062AA_BUCK1_STEP_MASK) >> DA9062AA_BUCK1_STEP_SHIFT, (val & DA9062AA_BUCK4_STEP_MASK) >> DA9062AA_BUCK4_STEP_SHIFT);
        return;

    case DA9062AA_ID_16_15:
        rt_snprintf(buf, bufsize, "buck3_step=%u", (val & DA9062AA_BUCK3_STEP_MASK) >> DA9062AA_BUCK3_STEP_SHIFT);
        return;

    case DA9062AA_ID_22_21:
        rt_snprintf(buf, bufsize, "gp_fall1_step=%u gp_rise1_step=%u", (val & DA9062AA_GP_FALL1_STEP_MASK) >> DA9062AA_GP_FALL1_STEP_SHIFT, (val & DA9062AA_GP_RISE1_STEP_MASK) >> DA9062AA_GP_RISE1_STEP_SHIFT);
        return;

    case DA9062AA_ID_24_23:
        rt_snprintf(buf, bufsize, "gp_fall2_step=%u gp_rise2_step=%u", (val & DA9062AA_GP_FALL2_STEP_MASK) >> DA9062AA_GP_FALL2_STEP_SHIFT, (val & DA9062AA_GP_RISE2_STEP_MASK) >> DA9062AA_GP_RISE2_STEP_SHIFT);
        return;

    case DA9062AA_ID_26_25:
        rt_snprintf(buf, bufsize, "gp_fall3_step=%u gp_rise3_step=%u", (val & DA9062AA_GP_FALL3_STEP_MASK) >> DA9062AA_GP_FALL3_STEP_SHIFT, (val & DA9062AA_GP_RISE3_STEP_MASK) >> DA9062AA_GP_RISE3_STEP_SHIFT);
        return;

    case DA9062AA_ID_28_27:
        rt_snprintf(buf, bufsize, "gp_fall4_step=%u gp_rise4_step=%u", (val & DA9062AA_GP_FALL4_STEP_MASK) >> DA9062AA_GP_FALL4_STEP_SHIFT, (val & DA9062AA_GP_RISE4_STEP_MASK) >> DA9062AA_GP_RISE4_STEP_SHIFT);
        return;

    case DA9062AA_ID_30_29:
        rt_snprintf(buf, bufsize, "gp_fall5_step=%u gp_rise5_step=%u", (val & DA9062AA_GP_FALL5_STEP_MASK) >> DA9062AA_GP_FALL5_STEP_SHIFT, (val & DA9062AA_GP_RISE5_STEP_MASK) >> DA9062AA_GP_RISE5_STEP_SHIFT);
        return;

    case DA9062AA_ID_32_31:
        rt_snprintf(buf, bufsize, "en32k_step=%u wait_step=%u", (val & DA9062AA_EN32K_STEP_MASK) >> DA9062AA_EN32K_STEP_SHIFT, (val & DA9062AA_WAIT_STEP_MASK) >> DA9062AA_WAIT_STEP_SHIFT);
        return;

    case DA9062AA_SEQ_A:
        rt_snprintf(buf, bufsize, "power_end=%u system_end=%u", (val & DA9062AA_POWER_END_MASK) ? 1 : 0, (val & DA9062AA_SYSTEM_END_MASK) ? 1 : 0);
        return;

    case DA9062AA_SEQ_B:
        rt_snprintf(buf, bufsize, "part_down=%u max_count=%u", (val & DA9062AA_PART_DOWN_MASK) ? 1 : 0, (val & DA9062AA_MAX_COUNT_MASK));
        return;

    case DA9062AA_WAIT:
        rt_snprintf(buf, bufsize, "wait_dir=%u timeout=%u wait_mode=%u wait_time=%u", (val & DA9062AA_WAIT_DIR_MASK) ? 1 : 0, (val & DA9062AA_TIME_OUT_MASK) >> DA9062AA_TIME_OUT_SHIFT, (val & DA9062AA_WAIT_MODE_MASK) ? 1 : 0, (val & DA9062AA_WAIT_TIME_MASK));
        return;

    case DA9062AA_EN_32K:
        rt_snprintf(buf, bufsize, "en_32kout=%u out_clock=%u delay_mode=%u crystal=%u stabilisation_time=%u", (val & DA9062AA_EN_32KOUT_MASK) ? 1 : 0, (val & DA9062AA_OUT_CLOCK_MASK) >> DA9062AA_OUT_CLOCK_SHIFT, (val & DA9062AA_DELAY_MODE_MASK) ? 1 : 0, (val & DA9062AA_CRYSTAL_MASK) ? 1 : 0, (val & DA9062AA_STABILISATION_TIME_MASK));
        return;

    case DA9062AA_RESET:
        rt_snprintf(buf, bufsize, "reset_event=%u reset_timer=%u", (val & DA9062AA_RESET_EVENT_MASK) ? 1 : 0, (val & DA9062AA_RESET_TIMER_MASK));
        return;

    case DA9062AA_BUCK_ILIM_A:
        rt_snprintf(buf, bufsize, "buck3_ilim=%u buck4_ilim=%u", (val & DA9062AA_BUCK3_ILIM_MASK) >> DA9062AA_BUCK3_ILIM_SHIFT, (val & DA9062AA_BUCK4_ILIM_MASK) >> DA9062AA_BUCK4_ILIM_SHIFT);
        return;

    case DA9062AA_BUCK_ILIM_B:
        rt_snprintf(buf, bufsize, "buck2_ilim=%u buck1_ilim=%u", (val & DA9062AA_BUCK2_ILIM_MASK) >> DA9062AA_BUCK2_ILIM_SHIFT, (val & DA9062AA_BUCK1_ILIM_MASK) >> DA9062AA_BUCK1_ILIM_SHIFT);
        return;

    case DA9062AA_BUCK_ILIM_C:
        rt_snprintf(buf, bufsize, "buck1_ilim=%u buck2_ilim=%u buck3_ilim=%u", (val & DA9062AA_BUCK1_ILIM_MASK) >> DA9062AA_BUCK1_ILIM_SHIFT, (val & DA9062AA_BUCK2_ILIM_MASK) >> DA9062AA_BUCK2_ILIM_SHIFT, (val & DA9062AA_BUCK3_ILIM_MASK) >> DA9062AA_BUCK3_ILIM_SHIFT);
        return;

    case DA9062AA_BUCK2_CFG:
        rt_snprintf(buf, bufsize, "buck2_mode=%u buck2_pd_dis=%u", (val & DA9062AA_BUCK2_MODE_MASK) ? 1 : 0, (val & DA9062AA_BUCK2_PD_DIS_MASK) ? 1 : 0);
        return;

    case DA9062AA_BUCK1_CFG:
        rt_snprintf(buf, bufsize, "buck1_mode=%u buck1_pd_dis=%u", (val & DA9062AA_BUCK1_MODE_MASK) ? 1 : 0, (val & DA9062AA_BUCK1_PD_DIS_MASK) ? 1 : 0);
        return;

    case DA9062AA_BUCK4_CFG:
        rt_snprintf(buf, bufsize, "buck4_mode=%u buck4_pd_dis=%u buck4_vtt_en=%u", (val & DA9062AA_BUCK4_MODE_MASK) ? 1 : 0, (val & DA9062AA_BUCK4_PD_DIS_MASK) ? 1 : 0, (val & DA9062AA_BUCK4_VTT_EN_MASK) ? 1 : 0);
        return;

    case DA9062AA_BUCK3_CFG:
        rt_snprintf(buf, bufsize, "buck3_mode=%u buck3_pd_dis=%u", (val & DA9062AA_BUCK3_MODE_MASK) ? 1 : 0, (val & DA9062AA_BUCK3_PD_DIS_MASK) ? 1 : 0);
        return;

    case DA9062AA_VBUCK2_A:
        rt_snprintf(buf, bufsize, "vbuck2_sl_a=%u vbuck2_a=%u", (val & DA9062AA_BUCK2_SL_A_MASK) >> DA9062AA_BUCK2_SL_A_SHIFT, val & DA9062AA_VBUCK2_A_MASK);
        return;

    case DA9062AA_VBUCK1_A:
        rt_snprintf(buf, bufsize, "vbuck1_sl_a=%u vbuck1_a=%u", (val & DA9062AA_BUCK1_SL_A_MASK) >> DA9062AA_BUCK1_SL_A_SHIFT, val & DA9062AA_VBUCK1_A_MASK);
        return;

    case DA9062AA_VBUCK4_A:
        rt_snprintf(buf, bufsize, "vbuck4_sl_a=%u vbuck4_a=%u", (val & DA9062AA_BUCK4_SL_A_MASK) >> DA9062AA_BUCK4_SL_A_SHIFT, val & DA9062AA_VBUCK4_A_MASK);
        return;

    case DA9062AA_VBUCK3_A:
        rt_snprintf(buf, bufsize, "vbuck3_sl_a=%u vbuck3_a=%u", (val & DA9062AA_BUCK3_SL_A_MASK) >> DA9062AA_BUCK3_SL_A_SHIFT, val & DA9062AA_VBUCK3_A_MASK);
        return;

    case DA9062AA_VLDO1_A:
        rt_snprintf(buf, bufsize, "vldo1_sl_a=%u vldo1_a=%u", (val & DA9062AA_LDO1_SL_A_MASK) >> DA9062AA_LDO1_SL_A_SHIFT, val & DA9062AA_VLDO1_A_MASK);
        return;

    case DA9062AA_VLDO2_A:
        rt_snprintf(buf, bufsize, "vldo2_sl_a=%u vldo2_a=%u", (val & DA9062AA_LDO2_SL_A_MASK) >> DA9062AA_LDO2_SL_A_SHIFT, val & DA9062AA_VLDO2_A_MASK);
        return;

    case DA9062AA_VLDO3_A:
        rt_snprintf(buf, bufsize, "vldo3_sl_a=%u vldo3_a=%u", (val & DA9062AA_LDO3_SL_A_MASK) >> DA9062AA_LDO3_SL_A_SHIFT, val & DA9062AA_VLDO3_A_MASK);
        return;

    case DA9062AA_VLDO4_A:
        rt_snprintf(buf, bufsize, "vldo4_sl_a=%u vldo4_a=%u", (val & DA9062AA_LDO4_SL_A_MASK) >> DA9062AA_LDO4_SL_A_SHIFT, val & DA9062AA_VLDO4_A_MASK);
        return;

    case DA9062AA_VBUCK2_B:
        rt_snprintf(buf, bufsize, "vbuck2_sl_b=%u vbuck2_b=%u", (val & DA9062AA_BUCK2_SL_B_MASK) >> DA9062AA_BUCK2_SL_B_SHIFT, val & DA9062AA_VBUCK2_B_MASK);
        return;

    case DA9062AA_VBUCK1_B:
        rt_snprintf(buf, bufsize, "vbuck1_sl_b=%u vbuck1_b=%u", (val & DA9062AA_BUCK1_SL_B_MASK) >> DA9062AA_BUCK1_SL_B_SHIFT, val & DA9062AA_VBUCK1_B_MASK);
        return;

    case DA9062AA_VBUCK4_B:
        rt_snprintf(buf, bufsize, "vbuck4_sl_b=%u vbuck4_b=%u", (val & DA9062AA_BUCK4_SL_B_MASK) >> DA9062AA_BUCK4_SL_B_SHIFT, val & DA9062AA_VBUCK4_B_MASK);
        return;

    case DA9062AA_VBUCK3_B:
        rt_snprintf(buf, bufsize, "vbuck3_sl_b=%u vbuck3_b=%u", (val & DA9062AA_BUCK3_SL_B_MASK) >> DA9062AA_BUCK3_SL_B_SHIFT, val & DA9062AA_VBUCK3_B_MASK);
        return;

    case DA9062AA_VLDO1_B:
        rt_snprintf(buf, bufsize, "vldo1_sl_b=%u vldo1_b=%u", (val & DA9062AA_LDO1_SL_B_MASK) >> DA9062AA_LDO1_SL_B_SHIFT, val & DA9062AA_VLDO1_B_MASK);
        return;

    case DA9062AA_VLDO2_B:
        rt_snprintf(buf, bufsize, "vldo2_sl_b=%u vldo2_b=%u", (val & DA9062AA_LDO2_SL_B_MASK) >> DA9062AA_LDO2_SL_B_SHIFT, val & DA9062AA_VLDO2_B_MASK);
        return;

    case DA9062AA_VLDO3_B:
        rt_snprintf(buf, bufsize, "vldo3_sl_b=%u vldo3_b=%u", (val & DA9062AA_LDO3_SL_B_MASK) >> DA9062AA_LDO3_SL_B_SHIFT, val & DA9062AA_VLDO3_B_MASK);
        return;

    case DA9062AA_VLDO4_B:
        rt_snprintf(buf, bufsize, "vldo4_sl_b=%u vldo4_b=%u", (val & DA9062AA_LDO4_SL_B_MASK) >> DA9062AA_LDO4_SL_B_SHIFT, val & DA9062AA_VLDO4_B_MASK);
        return;

    case DA9062AA_BBAT_CONT:
        rt_snprintf(buf, bufsize, "bchg_iset=%u bchg_vset=%u", (val & DA9062AA_BCHG_ISET_MASK) >> DA9062AA_BCHG_ISET_SHIFT, (val & DA9062AA_BCHG_VSET_MASK) >> DA9062AA_BCHG_VSET_SHIFT);
        return;

    case DA9062AA_INTERFACE:
        rt_snprintf(buf, bufsize, "if_base_addr=%u", (val & DA9062AA_IF_BASE_ADDR_MASK));
        return;

    case DA9062AA_CONFIG_A:
        rt_snprintf(buf, bufsize, "pm_if_hsm=%u pm_if_fmp=%u pm_if_v=%u irq_type=%u pm_o_type=%u pm_i_v=%u",
                    (val & DA9062AA_PM_IF_HSM_MASK) ? 1 : 0,
                    (val & DA9062AA_PM_IF_FMP_MASK) ? 1 : 0,
                    (val & DA9062AA_PM_IF_V_MASK) ? 1 : 0,
                    (val & DA9062AA_IRQ_TYPE_MASK) ? 1 : 0,
                    (val & DA9062AA_PM_O_TYPE_MASK) ? 1 : 0,
                    (val & DA9062AA_PM_I_V_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONFIG_B:
        rt_snprintf(buf, bufsize, "vdd_hyst_adj=%u vdd_fault_adj=%u", (val & DA9062AA_VDD_HYST_ADJ_MASK), (val & DA9062AA_VDD_FAULT_ADJ_MASK));
        return;

    case DA9062AA_CONFIG_C:
        rt_snprintf(buf, bufsize, "buck3_clk_inv=%u buck4_clk_inv=%u buck1_clk_inv=%u buck_actv_dischrg=%u", (val & DA9062AA_BUCK3_CLK_INV_MASK) ? 1 : 0, (val & DA9062AA_BUCK4_CLK_INV_MASK) ? 1 : 0, (val & DA9062AA_BUCK1_CLK_INV_MASK) ? 1 : 0, (val & DA9062AA_BUCK_ACTV_DISCHRG_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONFIG_D:
        rt_snprintf(buf, bufsize, "force_reset=%u system_en_rd=%u nirq_mode=%u gpi_v=%u", (val & DA9062AA_FORCE_RESET_MASK) ? 1 : 0, (val & DA9062AA_SYSTEM_EN_RD_MASK) ? 1 : 0, (val & DA9062AA_NIRQ_MODE_MASK) ? 1 : 0, (val & DA9062AA_GPI_V_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONFIG_E:
        rt_snprintf(buf, bufsize, "buck3_auto=%u buck4_auto=%u buck2_auto=%u buck1_auto=%u", (val & DA9062AA_BUCK3_AUTO_MASK) ? 1 : 0, (val & DA9062AA_BUCK4_AUTO_MASK) ? 1 : 0, (val & DA9062AA_BUCK2_AUTO_MASK) ? 1 : 0, (val & DA9062AA_BUCK1_AUTO_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONFIG_G:
        rt_snprintf(buf, bufsize, "ldo4_auto=%u ldo3_auto=%u ldo2_auto=%u ldo1_auto=%u", (val & DA9062AA_LDO4_AUTO_MASK) ? 1 : 0, (val & DA9062AA_LDO3_AUTO_MASK) ? 1 : 0, (val & DA9062AA_LDO2_AUTO_MASK) ? 1 : 0, (val & DA9062AA_LDO1_AUTO_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONFIG_H:
        rt_snprintf(buf, bufsize, "buck1_od=%u buck2_od=%u buck1_2_merge=%u", (val & DA9062AA_BUCK1_OD_MASK) ? 1 : 0, (val & DA9062AA_BUCK2_OD_MASK) ? 1 : 0, (val & DA9062AA_BUCK1_2_MERGE_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONFIG_I:
        rt_snprintf(buf, bufsize, "ldo_sd=%u int_sd_mode=%u host_sd_mode=%u key_sd_mode=%u watchdog_sd=%u nonkey_sd=%u nonkey_pin=%u", (val & DA9062AA_LDO_SD_MASK) ? 1 : 0, (val & DA9062AA_INT_SD_MODE_MASK) ? 1 : 0, (val & DA9062AA_HOST_SD_MODE_MASK) ? 1 : 0, (val & DA9062AA_KEY_SD_MODE_MASK) ? 1 : 0, (val & DA9062AA_WATCHDOG_SD_MASK) ? 1 : 0, (val & DA9062AA_nONKEY_SD_MASK) ? 1 : 0, (val & DA9062AA_NONKEY_PIN_MASK) ? 1 : 0);
        return;

    case DA9062AA_CONFIG_J:
        rt_snprintf(buf, bufsize, "if_reset=%u twowire_to=%u reset_duration=%u shut_delay=%u key_delay=%u", (val & DA9062AA_IF_RESET_MASK) ? 1 : 0, (val & DA9062AA_TWOWIRE_TO_MASK), (val & DA9062AA_RESET_DURATION_MASK), (val & DA9062AA_SHUT_DELAY_MASK), (val & DA9062AA_KEY_DELAY_MASK));
        return;

    case DA9062AA_CONFIG_K:
        rt_snprintf(buf, bufsize, "gpio4_pupd=%u gpio3_pupd=%u gpio2_pupd=%u gpio1_pupd=%u gpio0_pupd=%u wdg_mode=%u", (val & DA9062AA_GPIO4_PUPD_MASK) ? 1 : 0, (val & DA9062AA_GPIO3_PUPD_MASK) ? 1 : 0, (val & DA9062AA_GPIO2_PUPD_MASK) ? 1 : 0, (val & DA9062AA_GPIO1_PUPD_MASK) ? 1 : 0, (val & DA9062AA_GPIO0_PUPD_MASK) ? 1 : 0, (val & DA9062AA_WDG_MODE_MASK));
        return;

    case DA9062AA_CONFIG_M:
        rt_snprintf(buf, bufsize, "osc_frq=%u", (val & DA9062AA_OSC_FRQ_MASK));
        return;

    case DA9062AA_TRIM_CLDR:
        rt_snprintf(buf, bufsize, "trim_cldr=%u", (val & DA9062AA_TRIM_CLDR_MASK));
        return;

    case DA9062AA_GP_ID_0:
    case DA9062AA_GP_ID_1:
    case DA9062AA_GP_ID_2:
    case DA9062AA_GP_ID_3:
    case DA9062AA_GP_ID_4:
    case DA9062AA_GP_ID_5:
    case DA9062AA_GP_ID_6:
    case DA9062AA_GP_ID_7:
    case DA9062AA_GP_ID_8:
    case DA9062AA_GP_ID_9:
    case DA9062AA_GP_ID_10:
    case DA9062AA_GP_ID_11:
    case DA9062AA_GP_ID_12:
    case DA9062AA_GP_ID_13:
    case DA9062AA_GP_ID_14:
    case DA9062AA_GP_ID_15:
    case DA9062AA_GP_ID_16:
    case DA9062AA_GP_ID_17:
    case DA9062AA_GP_ID_18:
    case DA9062AA_GP_ID_19:
        rt_snprintf(buf, bufsize, "gp_id=%u", val);
        return;

    case DA9062AA_DEVICE_ID:
        rt_snprintf(buf, bufsize, "device_id=%u", val);
        return;

    case DA9062AA_VARIANT_ID:
        rt_snprintf(buf, bufsize, "variant_id=%u", val);
        return;

    case DA9062AA_CUSTOMER_ID:
        rt_snprintf(buf, bufsize, "customer_id=%u", val);
        return;

    case DA9062AA_CONFIG_ID:
        rt_snprintf(buf, bufsize, "config_id=%u", val);
        return;

    default:
        rt_snprintf(buf, bufsize, "raw=0x%02X", val);
        return;
    }
}

static int da9062_i2c_write(uint8_t u8addr, uint8_t u8data)
{
    struct rt_i2c_msg msg;
    char au8TxData[2];

    RT_ASSERT(g_psNuDA9062 != NULL);

    au8TxData[0] = u8addr;          //addr [ 7:0]
    au8TxData[1] = u8data;          //data [ 7:0]

    msg.addr  = DEF_DA9062_PAGE0_SLAVEADDR;      /* Slave address */
    msg.flags = RT_I2C_WR;                       /* Write flag */
    msg.buf   = (rt_uint8_t *)&au8TxData[0];     /* Slave register address */
    msg.len   = sizeof(au8TxData);               /* Number of bytes sent */

    if (g_psNuDA9062 && rt_i2c_transfer(g_psNuDA9062, &msg, 1) != 1)
    {
        rt_kprintf("[Failed] addr=%x, data=%d\n", u8addr, u8data);
        return -RT_ERROR;
    }

    return RT_EOK;
}

static int da9062_i2c_read(uint8_t u8addr, uint8_t *pu8data)
{
    struct rt_i2c_msg msgs[2];
    char u8TxData;

    RT_ASSERT(g_psNuDA9062 != NULL);
    RT_ASSERT(pu8data != NULL);

    u8TxData      = u8addr;        //addr [ 7:0]

    msgs[0].addr  = DEF_DA9062_PAGE0_SLAVEADDR;  /* Slave address */
    msgs[0].flags = RT_I2C_WR;                   /* Write flag */
    msgs[0].buf   = (rt_uint8_t *)&u8TxData;     /* Number of bytes sent */
    msgs[0].len   = sizeof(u8TxData);            /* Number of bytes read */

    msgs[1].addr  = DEF_DA9062_PAGE0_SLAVEADDR;  /* Slave address */
    msgs[1].flags = RT_I2C_RD;                   /* Read flag */
    msgs[1].buf   = (rt_uint8_t *)pu8data ;      /* Read data pointer */
    msgs[1].len   = 1;                           /* Number of bytes read */

    if (rt_i2c_transfer(g_psNuDA9062, &msgs[0], 2) != 2)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

int da9062_regs_dump(void)
{
    int i;
    char desc[128];

    rt_kprintf("===============================================================================================\n");
    rt_kprintf("| Addr | Value | Name            | 7 6 5 4 3 2 1 0 | Description\n");
    rt_kprintf("===============================================================================================\n");

    for (i = 0; i < ARRAY_SIZE(da9062_aa_readable_ranges); i++)
    {
        int start = da9062_aa_readable_ranges[i].range_min;
        int end = da9062_aa_readable_ranges[i].range_max;

        while (start <= end)
        {
            uint8_t u8Value = 0;
            const char *name = da9062_reg_name((uint16_t)start);

            if (da9062_i2c_read((uint8_t)start, &u8Value) != RT_EOK)
            {
                rt_kprintf("Can't readback value@0x%02x!\n", start);
                return -RT_ERROR;
            }

            da9062_decode_reg((uint16_t)start, u8Value, desc, sizeof(desc));

            rt_kprintf("| 0x%03X | 0x%02X | %-15s | "BYTE_TO_BINARY_PATTERN" | %s\n",
                       start,
                       u8Value,
                       name,
                       BYTE_TO_BINARY(u8Value),
                       desc);

            start++;
        }
    }

    rt_kprintf("===============================================================================================\n");
    return RT_EOK;
}

int rt_hw_da9062_init(const char *i2c_dev)
{
    RT_ASSERT(i2c_dev != RT_NULL);

    /* Find I2C bus */
    g_psNuDA9062 = (struct rt_i2c_bus_device *)rt_device_find(i2c_dev);
    if (g_psNuDA9062 == RT_NULL)
    {
        LOG_E("Can't found I2C bus - %s..!\n", i2c_dev);
        goto exit_rt_hw_da9062_init;
    }

    return RT_EOK;

exit_rt_hw_da9062_init:

    return -RT_ERROR;
}

int da9062_dump(void)
{
    rt_hw_da9062_init("i2c0");
    return da9062_regs_dump();
}
MSH_CMD_EXPORT(da9062_dump, dump da9062 registers);

#endif //#if defined(NU_PKG_USING_DA9062)
