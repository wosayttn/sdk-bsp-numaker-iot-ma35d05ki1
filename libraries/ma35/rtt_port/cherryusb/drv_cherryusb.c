/**************************************************************************//**
*
* @copyright (C) 2020 Nuvoton Technology Corp. All rights reserved.
*
* SPDX-License-Identifier: Apache-2.0
*
* Change Logs:
* Date            Author           Notes
* 2023-8-8        Wayne            First version
*
******************************************************************************/
#include "rtthread.h"

#if defined(PKG_USING_CHERRYUSB)

#include "rthw.h"
#include "NuMicro.h"
#include "usb_config.h"

#define LOG_TAG    "drv.cherry"
#define DBG_ENABLE
#define DBG_SECTION_NAME   LOG_TAG
#define DBG_LEVEL   LOG_LVL_DBG
#define DBG_COLOR
#include <rtdbg.h>

#if defined(PKG_CHERRYUSB_DEVICE)

static void nu_hsusbd_isr(int vector, void *param)
{
    // Call cherryusb interrupt service routine.
    void HSUSBD_IRQHandler(void);

    HSUSBD_IRQHandler();

}

void usb_dc_low_level_init(void)
{
    /* Enable USBD clock */
    CLK_EnableModuleClock(HSUSBD_MODULE);

    /* Reset USBD engine */
    SYS_ResetModule(HSUSBD_RST);

    /* Initial USB PHY */
    SYS->USBPMISCR &= ~(SYS_USBPMISCR_PHY0POR_Msk | SYS_USBPMISCR_PHY0SUSPEND_Msk | SYS_USBPMISCR_PHY0COMN_Msk);
    SYS->USBPMISCR |= SYS_USBPMISCR_PHY0SUSPEND_Msk;

    /* wait PHY clock ready */
    while ((SYS->USBPMISCR & SYS_USBPMISCR_PHY0DEVCKSTB_Msk) != SYS_USBPMISCR_PHY0DEVCKSTB_Msk);

    /* Register interrupt service routine. */
    rt_hw_interrupt_install(HSUSBD_IRQn, nu_hsusbd_isr, NULL, "hsusbd");

    /* Enable interrupt */
    rt_hw_interrupt_umask(HSUSBD_IRQn);
}

void usb_dc_low_level_deinit(void)
{
    /* Disable interrupt */
    rt_hw_interrupt_mask(HSUSBD_IRQn);

    /* Reset USBD engine */
    SYS_ResetModule(HSUSBD_RST);

    /* Disable USBD clock */
    CLK_DisableModuleClock(HSUSBD_MODULE);
}
#endif

#if defined(PKG_CHERRYUSB_HOST)

#include "usbh_core.h"

static void nu_echi_isr(int vector, void *param)
{
    // Call cherryusb interrupt service routine.
    void HSUSBH_IRQHandler(void);
    HSUSBH_IRQHandler();
}

static void nu_ochi_isr(int vector, void *param)
{
    // Call cherryusb interrupt service routine.
    void USBH_IRQHandler(void);
    USBH_IRQHandler();
}

void usb_hc_low_level_init(void)
{
    int timeout = 100;

    /* Enable USBH clock */
#if (CONFIG_USB_EHCI_HCCR_BASE==HSUSBH0_BASE)   //HSUSB0
    CLK_EnableModuleClock(HUSBH0_MODULE);
    SYS_ResetModule(HSUSBH0_RST);
#elif (CONFIG_USB_EHCI_HCCR_BASE==HSUSBH1_BASE)   //HSUSB1
    CLK_EnableModuleClock(HUSBH1_MODULE);
    SYS_ResetModule(HSUSBH1_RST);
#endif

    /* set UHOVRCURH(SYS_MISCFCR0[12]) 1 => USBH Host over-current detect is high-active */
    /*                                 0 => USBH Host over-current detect is low-active  */
    //SYS->MISCFCR0 |= SYS_MISCFCR0_UHOVRCURH_Msk;
    SYS->MISCFCR0 &= ~SYS_MISCFCR0_UHOVRCURH_Msk;

#if (CONFIG_USB_EHCI_HCCR_BASE==HSUSBH0_BASE)   //HSUSB0
    /* Clock engine clock Configuration */
    SYS->USBPMISCR &= ~(SYS_USBPMISCR_PHY0POR_Msk | SYS_USBPMISCR_PHY0COMN_Msk);
    rt_thread_mdelay(20);
    SYS->USBPMISCR |= SYS_USBPMISCR_PHY0SUSPEND_Msk | SYS_USBPMISCR_PHY0COMN_Msk;
#endif

#if (CONFIG_USB_EHCI_HCCR_BASE==HSUSBH1_BASE)   //HSUSB1
    /* Clock engine clock Configuration */
    SYS->USBPMISCR &= ~(SYS_USBPMISCR_PHY1POR_Msk | SYS_USBPMISCR_PHY1COMN_Msk);
    rt_thread_mdelay(20);
    SYS->USBPMISCR |= SYS_USBPMISCR_PHY1SUSPEND_Msk | SYS_USBPMISCR_PHY1COMN_Msk;
#endif

    while (1)
    {
        rt_thread_mdelay(1);
        if (
#if (CONFIG_USB_EHCI_HCCR_BASE==HSUSBH0_BASE)   //HSUSB0
            (SYS->USBPMISCR & SYS_USBPMISCR_PHY0HSTCKSTB_Msk) &&
#elif (CONFIG_USB_EHCI_HCCR_BASE==HSUSBH1_BASE)   //HSUSB1
            (SYS->USBPMISCR & SYS_USBPMISCR_PHY1HSTCKSTB_Msk)
#endif
        )
            break;   /* both USB PHY0 and PHY1 clock 60MHz UTMI clock stable */

        timeout--;
        if (timeout == 0)
        {
            rt_kprintf("USB PHY reset failed. USBPMISCR = 0x%08x\n", SYS->USBPMISCR);
            return;
        }
    }

#if (CONFIG_USB_EHCI_HCCR_BASE==HSUSBH0_BASE)   //HSUSB0
    /* Register interrupt service routine. */
    rt_hw_interrupt_install(HSUSBH0_IRQn, nu_echi_isr, NULL, "ehci0");

    /* Enable interrupt */
    rt_hw_interrupt_umask(HSUSBH0_IRQn);
#endif

#if (CONFIG_USB_EHCI_HCCR_BASE==HSUSBH1_BASE)   //HSUSB1
    /* Register interrupt service routine. */
    rt_hw_interrupt_install(HSUSBH1_IRQn, nu_echi_isr, NULL, "ehci1");

    /* Enable interrupt */
    rt_hw_interrupt_umask(HSUSBH1_IRQn);
#endif
}


void usb_hc_low_level2_init(void)
{
}

uint8_t usbh_get_port_speed(const uint8_t port)
{
    return USB_SPEED_HIGH;
}

#endif

#endif /* if defined(PKG_USING_CHERRYUSB) */
