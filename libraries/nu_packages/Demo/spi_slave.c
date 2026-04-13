/**************************************************************************//**
*
* @copyright (C) 2019 Nuvoton Technology Corp. All rights reserved.
*
* SPDX-License-Identifier: Apache-2.0
*
* Brief: Demo message transferring between SPI slave and SPI master.
*        - Use SPI0 as master and SPI1 as slave role.
*        - Slave always echos master's message to master.
*        - Slave uses PDMA to transfer SPI message.
*
* Change Logs:
* Date            Author       Notes
* 2025-2-25       Wayne        First version
*
******************************************************************************/

#include <rtthread.h>

#if defined(BSP_USING_SPI0) && defined(BSP_USING_SPI1)

#include "rtdevice.h"

#define LOG_TAG    "spi_echo"
#define DBG_ENABLE
#define DBG_SECTION_NAME   LOG_TAG
#define DBG_LEVEL      LOG_LVL_DBG
#define DBG_COLOR
#include <rtdbg.h>

#define UTEST_SPI0_DEVNAME        "spi0"
#define UTEST_SPI0SLAVE_DEVNAME   UTEST_SPI0_DEVNAME"2"
#define UTEST_SPI1_DEVNAME        "spi1"
#define UTEST_SPI1SLAVE_DEVNAME   UTEST_SPI1_DEVNAME"2"

#define UTEST_SPI_CLOCK_MAX_HZ           (24*1000000)
#define UTEST_SPI_CLOCK_MAX_DATA_WIDTH   32
#define UTEST_SPI_BUF_LENGTH             32

static struct rt_spi_device spi0_device;
static struct rt_spi_device spi1_device;

static void spi_echo_slave_worker(void *parameter)
{
    struct rt_spi_device *spi_dev;
    struct rt_spi_message msg;
    struct rt_spi_configuration cfg =
    {
        /* Set role to Slave. */
        .mode = RT_SPI_SLAVE | RT_SPI_MODE_0 | RT_SPI_MSB,
        .data_width = UTEST_SPI_CLOCK_MAX_DATA_WIDTH,
        .max_hz = UTEST_SPI_CLOCK_MAX_HZ
    };
    uint8_t *pu8TxBuf, *pu8RxBuf;

    pu8TxBuf = RT_NULL;
    pu8RxBuf = RT_NULL;

    if (((struct rt_spi_device *)rt_device_find(UTEST_SPI1SLAVE_DEVNAME))  == RT_NULL)
        rt_spi_bus_attach_device(&spi1_device, UTEST_SPI1SLAVE_DEVNAME, UTEST_SPI1_DEVNAME, RT_NULL);

    spi_dev = (struct rt_spi_device *)rt_device_find(UTEST_SPI1SLAVE_DEVNAME);
    if (spi_dev == RT_NULL)
    {
        LOG_E("Can't found %s", UTEST_SPI1SLAVE_DEVNAME);
        goto exit_spi_echo_slave_worker;
    }

    if (rt_spi_configure(spi_dev, &cfg) != RT_EOK)
    {
        LOG_E("Can't configure %s", UTEST_SPI1SLAVE_DEVNAME);
        goto exit_spi_echo_slave_worker;
    }

    /* Allocate UTEST_SPI_BUF_LENGTH Byte and let start address is cache-line aligned. */
    pu8TxBuf = (uint8_t *)rt_malloc_align(RT_ALIGN(UTEST_SPI_BUF_LENGTH, 32), 32);
    if (pu8TxBuf == NULL)
    {
        LOG_E("Failed to allocate TX buffer memory.");
        goto exit_spi_echo_slave_worker;
    }

    pu8RxBuf = (uint8_t *)rt_malloc_align(RT_ALIGN(UTEST_SPI_BUF_LENGTH, 32), 32);
    if (pu8RxBuf == NULL)
    {
        LOG_E("Failed to allocate RX buffer memory.");
        goto exit_spi_echo_slave_worker;
    }

    rt_memset(pu8TxBuf, 0xA5, UTEST_SPI_BUF_LENGTH);
    rt_memset(pu8RxBuf, 0xA5, UTEST_SPI_BUF_LENGTH);

    msg.send_buf   = (void *)&pu8TxBuf[0];
    msg.recv_buf   = (void *)&pu8RxBuf[0];
    msg.length     = UTEST_SPI_BUF_LENGTH;   /* total byte */
    msg.cs_take    = 1;
    msg.cs_release = 1;
    msg.next       = RT_NULL;

    while (1)
    {
        /* Will blocking here if messages are not completed. */
        rt_spi_transfer_message(spi_dev, &msg);

        /* Dump receiving message from slave. */
        //LOG_HEX("ss_r", 16, (void *)pu8RxBuf, UTEST_SPI_BUF_LENGTH);

        /* Echo message in next message. */
        rt_memcpy(pu8TxBuf, pu8RxBuf, UTEST_SPI_BUF_LENGTH);
    }

exit_spi_echo_slave_worker:

    /* Free memory. */
    if (pu8TxBuf)
        rt_free_align(pu8TxBuf);

    if (pu8RxBuf)
        rt_free_align(pu8RxBuf);
}

static int test_spi_slave(void)
{
    /* Notice: Please take care the slave task priority in application. It must be a higher number. */
    rt_thread_t thread = rt_thread_create("spi_rx", spi_echo_slave_worker, (void *)NULL, 4096, 5, 10);
    if (thread != RT_NULL)
    {
        rt_thread_startup(thread);
        LOG_I("create spi slave worker ok!");
    }
    else
    {
        LOG_E("create spi slave worker failed!\n");
    }

    return 0;
}
//INIT_APP_EXPORT(test_spi_slave);

static int test_spi_master(int argc, char **argv)
{
    int i;

    struct rt_spi_device *spi_dev;
    struct rt_spi_message msg[2];
    struct rt_spi_configuration cfg =
    {
        /* Set role to Master. */
        .mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB,
        .data_width = UTEST_SPI_CLOCK_MAX_DATA_WIDTH,
        .max_hz = UTEST_SPI_CLOCK_MAX_HZ
    };

    uint8_t *pu8TxBuf, *pu8RxBuf;

    pu8TxBuf = RT_NULL;
    pu8RxBuf = RT_NULL;

    if (((struct rt_spi_device *)rt_device_find(UTEST_SPI0SLAVE_DEVNAME))  == RT_NULL)
        rt_spi_bus_attach_device(&spi0_device, UTEST_SPI0SLAVE_DEVNAME, UTEST_SPI0_DEVNAME, RT_NULL);

    spi_dev = (struct rt_spi_device *)rt_device_find(UTEST_SPI0SLAVE_DEVNAME);
    if (spi_dev == RT_NULL)
    {
        LOG_E("Can't found %s", UTEST_SPI0SLAVE_DEVNAME);
        return -1;
    }

    if (rt_spi_configure(spi_dev, &cfg) != RT_EOK)
    {
        LOG_E("Can't configure %s", UTEST_SPI1SLAVE_DEVNAME);
        return -1;
    }

    /* Allocate UTEST_SPI_BUF_LENGTH Byte and let start address is cache-line aligned. */
    pu8TxBuf = rt_malloc_align(RT_ALIGN(UTEST_SPI_BUF_LENGTH, 32), 32);
    if (pu8TxBuf == NULL)
    {
        LOG_E("Failed to allocate TX buffer memory.");
        goto exit_test_spi_master;
    }

    pu8RxBuf = rt_malloc_align(RT_ALIGN(UTEST_SPI_BUF_LENGTH, 32), 32);
    if (pu8RxBuf == NULL)
    {
        LOG_E("Failed to allocate RX buffer memory.");
        goto exit_test_spi_master;
    }

    for (i = 0; i < UTEST_SPI_BUF_LENGTH; i++)
    {
        pu8TxBuf[i] = i % 256;
        pu8RxBuf[i] = 0x0;
    }

    /* Send out a sequence number and receiving message from slave. */
    msg[0].send_buf   = (void *)&pu8TxBuf[0];
    msg[0].recv_buf   = (void *)&pu8RxBuf[0];
    msg[0].length     = UTEST_SPI_BUF_LENGTH;   /* total byte */
    msg[0].cs_take    = 1;
    msg[0].cs_release = 1;
    msg[0].next       = RT_NULL;
    rt_spi_transfer_message(spi_dev, &msg[0]);

    /* Dump receiving message from slave. */
    LOG_HEX("1st_W", 16, (void *)pu8TxBuf, UTEST_SPI_BUF_LENGTH);
    LOG_HEX("1st_R", 16, (void *)pu8RxBuf, UTEST_SPI_BUF_LENGTH);

    /* Just receive slave's echo message. */
    msg[1].send_buf   = RT_NULL;
    msg[1].recv_buf   = (void *)&pu8RxBuf[0];
    msg[1].length     = UTEST_SPI_BUF_LENGTH;   /* total byte */
    msg[1].cs_take    = 1;
    msg[1].cs_release = 1;
    msg[1].next       = RT_NULL;
    rt_spi_transfer_message(spi_dev, &msg[1]);

    /* Dump receiving message from slave. */
    LOG_HEX("2nd_R", 16, (void *)pu8RxBuf, UTEST_SPI_BUF_LENGTH);

exit_test_spi_master:

    /* Free memory. */
    if (pu8TxBuf)
        rt_free_align(pu8TxBuf);

    if (pu8RxBuf)
        rt_free_align(pu8RxBuf);

    return 0;
}
MSH_CMD_EXPORT(test_spi_master, spi test function);

#endif //if defined(BSP_USING_SPI0) && defined(BSP_USING_SPI1)
