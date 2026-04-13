/******************************************************************************
 * @copyright (C) 2019 Nuvoton Technology Corp. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ******************************************************************************/

#include <rtthread.h>
#include <rtdevice.h>
#include "drv_gpio.h"
#include "drv_sys.h"
#include "drv_sspcc.h"

#include "board.h"

#if defined(BOARD_USING_STORAGE_SPINAND) && defined(NU_PKG_USING_SPINAND)

#include "drv_qspi.h"
#include "spinand.h"

struct rt_mtd_nand_device mtd_partitions[MTD_SPINAND_PARTITION_NUM] =
{
    [0] =
    {
        /*nand0: U-boot, env, rtthread*/
        .block_start = 0,
        .block_end   = 63,
        .block_total = 63-0+1,
    },
    [1] =
    {
        /*nand1: for filesystem mounting*/
        .block_start = 64,
        .block_end   = 2047,
        .block_total = 2047-64+1,
    },
    [2] =
    {
        /*nand2: Whole blocks size, overlay*/
        .block_start = 0,
        .block_end   = 2047,
        .block_total = 2048,
    }
};

static int rt_hw_spinand_init(void)
{
    if (nu_qspi_bus_attach_device("qspi0", "qspi01", 4, RT_NULL, RT_NULL) != RT_EOK)
        return -1;

    if (rt_hw_mtd_spinand_register("qspi01") != RT_EOK)
        return -1;

    return 0;
}

INIT_DEVICE_EXPORT(rt_hw_spinand_init);
#endif

#if defined(BSP_USING_RTP)
void nu_rtp_sspcc_setup(void)
{
    /* PDMA2/3 */
    SSPCC_SET_REALM(SSPCC_PDMA2, SSPCC_SSET_SUBM);
    SSPCC_SET_REALM(SSPCC_PDMA3, SSPCC_SSET_SUBM);
}
#endif

