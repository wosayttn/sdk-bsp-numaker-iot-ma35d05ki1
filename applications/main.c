/**************************************************************************//**
*
* @copyright (C) 2019 Nuvoton Technology Corp. All rights reserved.
*
* SPDX-License-Identifier: Apache-2.0
*
* Change Logs:
* Date            Author       Notes
* 2021-6-1        Wayne        First version
*
******************************************************************************/

#include <rtthread.h>
#include <rtdevice.h>
#include "drv_common.h"

#if defined(RT_USING_PIN)
#include "drv_gpio.h"

/* defined the LED_0 pin: PB8 */
#define LED_0   NU_GET_PININDEX(NU_PB, 8)

/* defined the Key0 pin: PL15 */
#define KEY_0   NU_GET_PININDEX(NU_PL, 15)

static uint32_t u32Key0 = KEY_0;

void nu_button_cb(void *args)
{
    uint32_t u32Key = *((uint32_t *)(args));

    rt_kprintf("[%s] %d Pressed!\n", __func__, u32Key);
}

int main(int argc, char **argv)
{
    /* set LED_0 pin mode to output */
    rt_pin_mode(LED_0, PIN_MODE_OUTPUT);

    /* set KEY_0 pin mode to input */
    rt_pin_mode(KEY_0, PIN_MODE_INPUT);
    rt_pin_attach_irq(KEY_0, PIN_IRQ_MODE_FALLING, nu_button_cb, &u32Key0);
    rt_pin_irq_enable(KEY_0, PIN_IRQ_ENABLE);

    while (1)
    {
        rt_pin_write(LED_0, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED_0, PIN_LOW);
        rt_thread_mdelay(500);
    }

    return 0;
}

#else

int main(int argc, char **argv)
{
    rt_kprintf("cpu-%d %d\r\n", rt_hw_cpu_id(), nu_cpu_dcache_line_size());
    return 0;
}

#endif
