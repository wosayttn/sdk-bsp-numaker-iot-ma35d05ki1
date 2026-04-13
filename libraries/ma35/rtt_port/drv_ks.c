/**************************************************************************//**
 * @file     crypto.c
 * @brief  Cryptographic Accelerator driver source file
 *
 * @copyright (C) 2023 Nuvoton Technology Corp. All rights reserved.
*****************************************************************************/
#include "rtthread.h"

#if defined(SOC_SERIES_MA35H0) || defined(SOC_SERIES_MA35D0)

#include <stdio.h>
#include <rthw.h>
#include "NuMicro.h"

static void ShowStatus(void)
{
    uint32_t bcnt, kcnt;
    uint32_t u32Status;

    u32Status = KS->STS;

    rt_kprintf("Key Store Status:");

    if (u32Status & KS_STS_IF_Msk)
        rt_kprintf(" STS_IF!");

    if (u32Status & KS_STS_EIF_Msk)
        rt_kprintf(" STS_EIF!");

    if (u32Status & KS_STS_BUSY_Msk)
        rt_kprintf(" STS_BUSY!");

    if (u32Status & KS_STS_SRAMFULL_Msk)
        rt_kprintf(" STS_SRAMFULL!");

    if ((u32Status & KS_STS_INITDONE_Msk) != KS_STS_INITDONE_Msk)
        rt_kprintf(" STS_NotInit!");
    rt_kprintf("\n");

    if (KS_GetSRAMRemain(&bcnt, &kcnt) == 0)
    {
        rt_kprintf("Key Store SRAM remaining space byte count: %d\n", bcnt);
        rt_kprintf("Key Store SRAM remaining key count: %d\n", kcnt);
    }
    rt_kprintf("\n");
}

static int ks_dump_key(uint8_t au8KeyData[], int i32WordLen)
{
    int  i;


    return 0;
}

static int ks_read_key(int argc, char *argv[])
{
#define KS_KEY_MAX      (KS_MAX_KEY_SIZE / 32)

    uint32_t    au32RKey[KS_KEY_MAX];
    int         err, i;
    int         key_index;

    if ((argc < 2) || sscanf(argv[1], "%d", &key_index) != 1)
        return -1;

    memset(au32RKey, 0, sizeof(au32RKey));

    // Clear status
    KS->STS = KS_STS_IF_Msk | KS_STS_EIF_Msk;

    err = KS_Read(KS_OTP, key_index, au32RKey, 256 / 32);
    if (err < 0)
    {
        rt_kprintf("Failed to read KEY#%d! %d\n", key_index, err);
        ShowStatus();

        return -1;
    }

    uint8_t *au8KeyData = (uint8_t *)au32RKey;
    rt_kprintf("KEY#%d: ", key_index);
    for (i = 0; i < (256 / 8); i++)
    {
        rt_kprintf("%02X", au8KeyData[i]);
    }
    rt_kprintf("\n");

    return 0;
}
MSH_CMD_EXPORT(ks_read_key, test ks read function);

/**
  * @brief  Initialize keystore
  * @return None
  */
int rt_hw_keystore_init(void)
{
    /* Enable clock */
    outpw(TSI_CLK_BASE + 0x4, inpw(TSI_CLK_BASE + 0x4) | (1 << 12));

    return 0;
}
INIT_DEVICE_EXPORT(rt_hw_keystore_init);
#endif
