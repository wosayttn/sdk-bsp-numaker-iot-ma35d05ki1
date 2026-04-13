/**************************************************************************//**
*
* @copyright (C) 2020 Nuvoton Technology Corp. All rights reserved.
*
* SPDX-License-Identifier: Apache-2.0
*
* Change Logs:
* Date            Author       Notes
* 2021-8-11       Wayne        First version
*
******************************************************************************/

#include <rtconfig.h>

#if defined(BSP_USING_DISP)

#include <stdio.h>
#include <rtdevice.h>
#include "NuMicro.h"
#include "drv_sys.h"

#include "drv_disp.h"

#if defined(NU_PKG_USING_DISPLIB)
    #include "displib.h"
#endif

#define LOG_TAG    "drv.disp"
//#undef  DBG_ENABLE
#define DBG_SECTION_NAME   LOG_TAG
#define DBG_LEVEL          LOG_LVL_INFO
#define DBG_COLOR
#include <rtdbg.h>

#if !defined(DISP_USING_LCD_IDX)
    #define DISP_USING_LCD_IDX eDispLcd_1024x600
#endif

#if !defined(BSP_LCD_BPP)
    #define BSP_LCD_BPP 32
#endif

#define DEF_VPOST_BUFFER_NUMBER 3

typedef enum
{
    eDispLcd_1024x600   = 0,
    eDispLcd_800x480    = 1,
#if defined(LCD_USING_CUSTOMER)
    eDispLcd_Customized = 2,
#endif
    eDispLcd_Cnt
} E_DISP_LCD;

const static DISP_LCD_INFO g_sLcdInfo_arr [eDispLcd_Cnt] =
{
    {
        /* eDispLcd_1024x600 */
        .u32ResolutionWidth  = BSP_LCD_WIDTH,
        .u32ResolutionHeight = BSP_LCD_HEIGHT,
        .sLcdTiming =
        {
            .u32PCF          = 51000000,
            .u32HA           = BSP_LCD_WIDTH,
            .u32HSL          = 1,
            .u32HFP          = 160,
            .u32HBP          = 160,
            .u32VA           = BSP_LCD_HEIGHT,
            .u32VSL          = 1,
            .u32VFP          = 23,
            .u32VBP          = 12,
            .eHSPP           = ePolarity_Positive,
            .eVSPP           = ePolarity_Positive
        },
        .sPanelConf =
        {
            .eDpiFmt         = eDPIFmt_D24,
            .eDEP            = ePolarity_Positive,
            .eDP             = ePolarity_Positive,
            .eCP             = ePolarity_Positive
        },
    },
    {
        /* eDispLcd_800x480 */
        .u32ResolutionWidth  = BSP_LCD_WIDTH,
        .u32ResolutionHeight = BSP_LCD_HEIGHT,
        .sLcdTiming =
        {
            .u32PCF          = 45000000,
            .u32HA           = BSP_LCD_WIDTH,
            .u32HSL          = 1,
            .u32HFP          = 210,
            .u32HBP          = 46,
            .u32VA           = BSP_LCD_HEIGHT,
            .u32VSL          = 1,
            .u32VFP          = 22,
            .u32VBP          = 23,
            .eHSPP           = ePolarity_Positive,
            .eVSPP           = ePolarity_Positive
        },
        .sPanelConf =
        {
            .eDpiFmt         = eDPIFmt_D24,
            .eDEP            = ePolarity_Positive,
            .eDP             = ePolarity_Positive,
            .eCP             = ePolarity_Positive
        },
    },
#if defined(LCD_USING_CUSTOMER)
    {
        /* eDispLcd_Customized */
        .u32ResolutionWidth  = BSP_LCD_WIDTH,
        .u32ResolutionHeight = BSP_LCD_HEIGHT,
        .sLcdTiming =
        {
            .u32PCF     = BSP_LCD_PIXEL_CLOCK,
            .u32HA      = BSP_LCD_WIDTH,
            .u32HSL     = BSP_LCD_HSL,
            .u32HFP     = BSP_LCD_HFP,
            .u32HBP     = BSP_LCD_HBP,
            .u32VA      = BSP_LCD_HEIGHT,
            .u32VSL     = BSP_LCD_VSL,
            .u32VFP     = BSP_LCD_VFP,
            .u32VBP     = BSP_LCD_VBP,
            .eHSPP      = ePolarity_Positive,
            .eVSPP      = ePolarity_Positive
        },
        .sPanelConf =
        {
            .eDpiFmt    = eDPIFmt_D24,
            .eDEP       = ePolarity_Positive,
            .eDP        = ePolarity_Positive,
            .eCP        = ePolarity_Positive
        },
    },
#endif
};


/* Private typedef --------------------------------------------------------------*/
struct nu_disp
{
    struct rt_device      dev;
    char                 *name;
    E_DISP_LAYER          layer;
    uint32_t              last_commit;
    uint32_t              ref_count;
    struct rt_device_graphic_info info;
    rt_uint8_t            *pu8FBDMABuf;
};
typedef struct nu_disp *nu_disp_t;

static volatile uint32_t g_u32VSyncBlank = 0;
static struct rt_completion vsync_wq;
static uint32_t s_u32VideoUnderFlow = 0;
static uint32_t s_u32OverlayUnderFlow = 0;

#if defined(DISP_USING_OVERLAY)
    static rt_mutex_t disp_lock;
#endif

static struct nu_disp nu_fbdev[eLayer_Cnt] =
{
    {
        .name  = "lcd",
        .layer = eLayer_Video,
        .ref_count = 0,
    }
#if defined(DISP_USING_OVERLAY)
    , {
        .name  = "overlay",
        .layer = eLayer_Overlay,
        .ref_count = 0,
    }
#else
    , {
        .layer = eLayer_Cnt,
    }
#endif
#if defined(DISP_USING_CURSOR)
    , {
        .name  = "cursor",
        .layer = eLayer_Cursor,
        .ref_count = 0,
    }
#else
    , {
        .layer = eLayer_Cnt,
    }
#endif
};

RT_WEAK void nu_lcd_backlight_on(void) { }

RT_WEAK void nu_lcd_backlight_off(void) { }

static const DISP_LCD_INFO *DISP_GetLCDInst(E_DISP_LCD eDispLcd)
{
    if (eDispLcd < eDispLcd_Cnt)
        return &g_sLcdInfo_arr[eDispLcd];

    return (const DISP_LCD_INFO *)NULL;
}

static void nu_disp_isr(int vector, void *param)
{

    if (DISP_VIDEO_IS_UNDERFLOW())
    {
        s_u32VideoUnderFlow++;
    }

    if (DISP_OVERLAY_IS_UNDERFLOW())
    {
        s_u32OverlayUnderFlow++;
    }

    /* Get DISP INTSTS */
    if (DISP_GET_INTSTS())
    {
        g_u32VSyncBlank++;
        rt_completion_done(&vsync_wq);
    }
}

static rt_err_t disp_layer_open(rt_device_t dev, rt_uint16_t oflag)
{
    nu_disp_t psDisp = (nu_disp_t)dev;
    RT_ASSERT(psDisp != RT_NULL);

#if defined(DISP_USING_OVERLAY) || defined(DISP_USING_CURSOR)
    rt_mutex_take(disp_lock, RT_WAITING_FOREVER);
#endif

    psDisp->ref_count++;

#if defined(DISP_USING_OVERLAY) || defined(DISP_USING_CURSOR)
    if ((psDisp->layer == eLayer_Overlay) || (psDisp->layer == eLayer_Cursor))    // Depend on video layer
    {
        nu_fbdev[eLayer_Video].ref_count++;
    }
#endif

    if (nu_fbdev[eLayer_Video].ref_count == 1)
    {
        DISP_SetTransparencyMode(eLayer_Video, eMASK);
        DISP_Trigger(eLayer_Video, 1);
        DISP_ENABLE_INT();

#if defined(NU_PKG_USING_DISPLIB)
        disp_converter_init();
#endif

    }

#if defined(DISP_USING_OVERLAY) || defined(DISP_USING_CURSOR)
    if ((psDisp->layer == eLayer_Overlay) && (nu_fbdev[eLayer_Overlay].ref_count == 1))
    {
        DISP_SetTransparencyMode(eLayer_Overlay, eOPAQUE);
        DISP_Trigger(eLayer_Overlay, 1);
    }

    if ((psDisp->layer == eLayer_Cursor) && (nu_fbdev[eLayer_Cursor].ref_count == 1))
    {
        DISP_Trigger(eLayer_Cursor, 1);
    }

    rt_mutex_release(disp_lock);
#endif

    return RT_EOK;
}

static rt_err_t disp_layer_close(rt_device_t dev)
{
    nu_disp_t psDisp = (nu_disp_t)dev;
    RT_ASSERT(psDisp != RT_NULL);

#if defined(DISP_USING_OVERLAY) || defined(DISP_USING_CURSOR)
    rt_mutex_take(disp_lock, RT_WAITING_FOREVER);
#endif

    psDisp->ref_count--;

#if defined(DISP_USING_OVERLAY) || defined(DISP_USING_CURSOR) // Depend on video layer
    if ((psDisp->layer == eLayer_Overlay) || (psDisp->layer == eLayer_Cursor))
    {
        nu_fbdev[eLayer_Video].ref_count--;
    }

    if ((psDisp->layer == eLayer_Cursor) && (nu_fbdev[eLayer_Cursor].ref_count == 0))
    {
        DISP_Trigger(eLayer_Cursor, 0);
    }

    if ((psDisp->layer == eLayer_Overlay) && (nu_fbdev[eLayer_Overlay].ref_count == 0))
    {
        DISP_Trigger(eLayer_Overlay, 0);
    }
#endif

    if (nu_fbdev[eLayer_Video].ref_count == 0)
    {
        DISP_DISABLE_INT();
        DISP_Trigger(eLayer_Video, 0);

#if defined(NU_PKG_USING_DISPLIB)
        disp_converter_fini();
#endif

    }

#if defined(DISP_USING_OVERLAY) || defined(DISP_USING_CURSOR)
    rt_mutex_release(disp_lock);
#endif

    return RT_EOK;
}

void _disp_blank(void)
{
    rt_completion_init(&vsync_wq);
    rt_completion_wait(&vsync_wq, RT_TICK_PER_SECOND / DISP_LCDTIMING_GetFPS(RT_NULL));
}


static rt_err_t disp_layer_control(rt_device_t dev, int cmd, void *args)
{
    nu_disp_t psDisp = (nu_disp_t)dev;
    RT_ASSERT(psDisp);

    switch (cmd)
    {
    case RTGRAPHIC_CTRL_POWERON:
    {
        nu_lcd_backlight_on();
    }
    break;

    case RTGRAPHIC_CTRL_POWEROFF:
    {
        nu_lcd_backlight_off();
    }
    break;

    case RTGRAPHIC_CTRL_GET_INFO:
    {
        struct rt_device_graphic_info *info = (struct rt_device_graphic_info *) args;
        RT_ASSERT(info != RT_NULL);
        rt_memcpy(args, (void *)&psDisp->info, sizeof(struct rt_device_graphic_info));
    }
    break;

    case RTGRAPHIC_CTRL_SET_MODE:
    {
        int pixfmt, bpp, pitch;
        E_FB_FMT eFBFmt;

        RT_ASSERT(args);
        pixfmt = *((int *)args);

        switch (pixfmt)
        {
        case RTGRAPHIC_PIXEL_FORMAT_RGB565:
            eFBFmt = eFBFmt_R5G6B5;
            bpp = 16;
            pitch = psDisp->info.width * (bpp >> 3U);
            break;

        case RTGRAPHIC_PIXEL_FORMAT_ARGB888:
            eFBFmt = eFBFmt_A8R8G8B8;
            bpp = 32;
            pitch = psDisp->info.width * (bpp >> 3U);
            break;

        case RTGRAPHIC_PIXEL_FORMAT_NV12:
            eFBFmt = eFBFmt_NV12;
            bpp = 16;
            pitch = RT_ALIGN(psDisp->info.width, 16);
            break;

        case RTGRAPHIC_PIXEL_FORMAT_MONO:
        case RTGRAPHIC_PIXEL_FORMAT_GRAY4:
        case RTGRAPHIC_PIXEL_FORMAT_GRAY16:
        case RTGRAPHIC_PIXEL_FORMAT_RGB332:
        case RTGRAPHIC_PIXEL_FORMAT_RGB444:
        case RTGRAPHIC_PIXEL_FORMAT_BGR565:
        case RTGRAPHIC_PIXEL_FORMAT_RGB666:
        case RTGRAPHIC_PIXEL_FORMAT_RGB888:
        case RTGRAPHIC_PIXEL_FORMAT_BGR888:
        case RTGRAPHIC_PIXEL_FORMAT_ABGR888:
        case RTGRAPHIC_PIXEL_FORMAT_RESERVED:
        default:
            return -RT_ERROR;
        }

        psDisp->info.bits_per_pixel = bpp;
        psDisp->info.pixel_format = pixfmt;
        psDisp->info.pitch = pitch;

        /* Initial LCD */
        DISP_SetFBFmt(psDisp->layer, eFBFmt, psDisp->info.pitch);
    }
    break;

    case RTGRAPHIC_CTRL_GET_MODE:
    {
        RT_ASSERT(args);
        *((int *)args) = psDisp->info.pixel_format;
    }
    break;

    case RTGRAPHIC_CTRL_PAN_DISPLAY:
    {
        if (args != RT_NULL)
        {
            uint32_t u32BufPtr = (uint32_t)args;
            psDisp->last_commit = g_u32VSyncBlank;

            u32BufPtr = VA2PA(u32BufPtr); //Force to non-cacheable address.

            /* Pan display */
            return (DISP_SetFBAddr(psDisp->layer, u32BufPtr) == 0) ? RT_EOK : -RT_ERROR;
        }
        else
            return -RT_ERROR;
    }
    break;

    case RTGRAPHIC_CTRL_WAIT_VSYNC:
    {
        if (args != RT_NULL)
            psDisp->last_commit = g_u32VSyncBlank + 1;

        if (psDisp->last_commit >= g_u32VSyncBlank)
        {
            _disp_blank();
        }
    }
    break;

    default:
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t disp_layer_init(rt_device_t dev)
{
    nu_disp_t psDisp = (nu_disp_t)dev;
    RT_ASSERT(psDisp);

    return RT_EOK;
}

static void disp_panel_timing(const DISP_LCD_TIMING *psDispLcdTiming)
{
    LOG_I("%dx%d(Pixel Clock: %d Hz)", psDispLcdTiming->u32HA, psDispLcdTiming->u32VA, psDispLcdTiming->u32PCF);
    LOG_I("Horizontal Active: %d", psDispLcdTiming->u32HA);
    LOG_I("Horizontal Sync Length: %d", psDispLcdTiming->u32HSL);
    LOG_I("Horizontal Front Porch: %d", psDispLcdTiming->u32HFP);
    LOG_I("Horizontal Back Porch: %d", psDispLcdTiming->u32HBP);
    LOG_I("Vertical Active: %d", psDispLcdTiming->u32VA);
    LOG_I("Vertical Sync Len: %d", psDispLcdTiming->u32VSL);
    LOG_I("Vertical Front Porch: %d", psDispLcdTiming->u32VFP);
    LOG_I("Vertical Back Porch: %d", psDispLcdTiming->u32VBP);
}

int rt_hw_disp_init(void)
{
    int i;
    rt_err_t ret;

#define REG_SYS_CHIPCFG      (SYS_BASE + 0x1F4)
    uint32_t u32ChipCfg = *((vu32 *)REG_SYS_CHIPCFG);
    uint32_t u32ChipCfg_MMDIS = u32ChipCfg & (1 << 7);

    if (u32ChipCfg_MMDIS)
    {
        rt_kprintf("MMDIS is not set, DISP may not work properly!\n");
        return -1;
    }

    /* Get LCD panel instance by ID. */
    const DISP_LCD_INFO *psDispLcdInstance = DISP_GetLCDInst(DISP_USING_LCD_IDX);
    RT_ASSERT(psDispLcdInstance != RT_NULL);

    disp_panel_timing(&psDispLcdInstance->sLcdTiming);

    /* Initial LCD */
    DISP_LCDInit(psDispLcdInstance);

    for (i = 0;
            i < sizeof(nu_fbdev) / sizeof(struct nu_disp);
            i++)
    {
        nu_disp_t psDisp = &nu_fbdev[i];

        if (psDisp->layer == eLayer_Cnt)
            continue;

        rt_memset((void *)&psDisp->info, 0, sizeof(struct rt_device_graphic_info));

        /* Register Disp device information */
        if (psDisp->layer == eLayer_Cursor)
        {
            psDisp->info.bits_per_pixel = 32;
            psDisp->info.pixel_format = RTGRAPHIC_PIXEL_FORMAT_ARGB888;
            psDisp->info.pitch = 32 * (32 / 8);
            psDisp->info.width = 32;
            psDisp->info.height = 32;
        }
        else
        {
            psDisp->info.bits_per_pixel = BSP_LCD_BPP;
            psDisp->info.pixel_format = (BSP_LCD_BPP == 32) ? RTGRAPHIC_PIXEL_FORMAT_ARGB888 : RTGRAPHIC_PIXEL_FORMAT_RGB565;
            psDisp->info.pitch = psDispLcdInstance->u32ResolutionWidth * (BSP_LCD_BPP / 8);
            psDisp->info.width = psDispLcdInstance->u32ResolutionWidth;
            psDisp->info.height = psDispLcdInstance->u32ResolutionHeight;
        }

        /* Get pointer of video frame buffer */
        rt_uint8_t *pu8FBDMABuf = rt_malloc_align(psDisp->info.pitch * psDisp->info.height * DEF_VPOST_BUFFER_NUMBER, 128);
        if (pu8FBDMABuf == NULL)
        {
            LOG_E("Fail to get VRAM buffer for %s layer.\n", psDisp->name);
            RT_ASSERT(0);
        }
        else
        {
            /* Register non-cacheable DMA address to upper layer. */
            psDisp->info.framebuffer = (rt_uint8_t *)PA2VA(pu8FBDMABuf);

            uint32_t u32FBSize = psDisp->info.pitch * psDispLcdInstance->u32ResolutionHeight;
            psDisp->info.smem_len = u32FBSize * DEF_VPOST_BUFFER_NUMBER;
            rt_memset(psDisp->info.framebuffer, 0, psDisp->info.smem_len);
        }

        if (psDisp->layer == eLayer_Cursor)
        {
            DISP_CURSOR_CONF sCursorConf = {0};
            sCursorConf.u32FrameBuffer = (uint32_t)pu8FBDMABuf;
            sCursorConf.sHotSpot.u32X = 4;
            sCursorConf.sHotSpot.u32Y = 4;
            sCursorConf.sInitPosition.u32X = psDispLcdInstance->u32ResolutionWidth / 2;
            sCursorConf.sInitPosition.u32Y = psDispLcdInstance->u32ResolutionHeight / 2;

            DISP_InitCursor(&sCursorConf);
        }
        else
        {
            E_FB_FMT eFbFmt = (psDisp->info.pixel_format == RTGRAPHIC_PIXEL_FORMAT_ARGB888) ? eFBFmt_A8R8G8B8 : eFBFmt_R5G6B5 ;
            ret = (rt_err_t)DISP_SetFBConfig(i, eFbFmt, psDisp->info.width, psDisp->info.height, (uint32_t)pu8FBDMABuf);
            RT_ASSERT(ret == RT_EOK);
        }

        psDisp->pu8FBDMABuf = pu8FBDMABuf;

        /* Register member functions of lcd device */
        psDisp->dev.type    = RT_Device_Class_Graphic;
        psDisp->dev.init    = disp_layer_init;
        psDisp->dev.open    = disp_layer_open;
        psDisp->dev.close   = disp_layer_close;
        psDisp->dev.control = disp_layer_control;

        /* register graphic device driver */
        ret = rt_device_register(&psDisp->dev, psDisp->name, RT_DEVICE_FLAG_RDWR);
        RT_ASSERT(ret == RT_EOK);

        LOG_I("%s's fbdev video memory at 0x%08x.", psDisp->name, psDisp->info.framebuffer);
    }

#if defined(DISP_USING_OVERLAY)
    /* Initial display lock */
    disp_lock = rt_mutex_create("displock", RT_IPC_FLAG_FIFO);
    RT_ASSERT(disp_lock);
#endif

    /* Initial vsync waitqueue instance */
    rt_completion_init(&vsync_wq);

    /* Register ISR */
    rt_hw_interrupt_install(DISP_IRQn, nu_disp_isr, RT_NULL, "DISP");

    /* Disable interrupt. */
    DISP_DISABLE_INT();

    /* Unmask interrupt. */
    rt_hw_interrupt_umask(DISP_IRQn);

    LOG_I("LCD panel timing is %d FPS.", DISP_LCDTIMING_GetFPS(&psDispLcdInstance->sLcdTiming));

    return 0;
}
INIT_DEVICE_EXPORT(rt_hw_disp_init);

static void nu_disp_show_video(void)
{
    rt_device_open(&nu_fbdev[eLayer_Video].dev, RT_DEVICE_FLAG_RDWR);
}
MSH_CMD_EXPORT(nu_disp_show_video, show video layer);

static void nu_disp_hide_video(void)
{
    rt_device_close(&nu_fbdev[eLayer_Video].dev);
}
MSH_CMD_EXPORT(nu_disp_hide_video, hide video layer);

static void nu_disp_dump_info(void)
{
    LOG_I("Blank:%d", g_u32VSyncBlank);
    LOG_I("FIFO underflow - Video:%d Overlay:%d", s_u32VideoUnderFlow, s_u32OverlayUnderFlow);
}
MSH_CMD_EXPORT(nu_disp_dump_info, dump some information);


static rt_err_t nu_disp_set_fps(int argc, char **argv)
{
    uint32_t arg;

    if (sscanf(argv[1], "%ld", &arg) != 1)
        return -1;

    DISP_LCDTIMING_SetFPS(arg);
    LOG_I("Set FPS to %d", arg);

    return 0;
}
MSH_CMD_EXPORT(nu_disp_set_fps, e.g: nu_disp_set_fps fps);


#if defined(DISP_USING_OVERLAY)

int nu_disp_overlay_set_colkey(uint32_t u32ColKey)
{
    DISP_SetColorKeyValue(u32ColKey, u32ColKey);
    DISP_SetTransparencyMode(eLayer_Overlay, eKEY);
    return 0;
}
RTM_EXPORT(nu_disp_overlay_set_colkey);

/* Support "nu_disp_set_colkey" command line in msh mode */
static rt_err_t nu_disp_set_colkey(int argc, char **argv)
{
    uint32_t arg;

    if (sscanf(argv[1], "%lx", &arg) != 1)
        return -1;

    LOG_I("colkey:0x%08x", arg);
    nu_disp_overlay_set_colkey(arg);

    return 0;
}
MSH_CMD_EXPORT(nu_disp_set_colkey, e.g: nu_disp_set_colkey colkey);

static void nu_disp_show_overlay(void)
{
    rt_device_open(&nu_fbdev[eLayer_Overlay].dev, RT_DEVICE_FLAG_RDWR);
}
MSH_CMD_EXPORT(nu_disp_show_overlay, show overlay layer);

static void nu_disp_hide_overlay(void)
{
    rt_device_close(&nu_fbdev[eLayer_Overlay].dev);
}
MSH_CMD_EXPORT(nu_disp_hide_overlay, hide overlay layer);

static void nu_disp_overlay_fill_color(void)
{
    nu_disp_t psDispLayer;
    int idx = eLayer_Overlay;

    psDispLayer = &nu_fbdev[idx];
    if (psDispLayer->info.framebuffer != RT_NULL)
    {
        int i;
        uint32_t fill_num = psDispLayer->info.height * psDispLayer->info.width;
        uint32_t *fbmem_start = (uint32_t *)psDispLayer->info.framebuffer;
        uint32_t color = (0x3F << 24) | (rand() % 0x1000000) ;
        LOG_I("fill color=0x%08x on %s layer", color, psDispLayer->name);
        for (i = 0; i < fill_num; i++)
        {
            rt_memcpy((void *)&fbmem_start[i], &color, (psDispLayer->info.bits_per_pixel / 8));
        }
    }
}
MSH_CMD_EXPORT(nu_disp_overlay_fill_color, fill random color on overlay layer);

static rt_err_t nu_disp_set_alphablend_opmode(int argc, char **argv)
{
    unsigned int index, len, arg[1];

    rt_memset(arg, 0, sizeof(arg));
    len = (argc >= 2) ? 2 : argc;

    for (index = 0; index < (len - 1); index ++)
    {
        if (sscanf(argv[index + 1], "%x", &arg[index]) != 1)
            return -1;
    }

    LOG_I("opmode:0x%08x", arg[0]);

    if (arg[0] <= DC_BLEND_MODE_SRC_OUT)
        DISP_SetBlendOpMode(arg[0], eGloAM_NORMAL, eGloAM_NORMAL);

    return 0;
}
MSH_CMD_EXPORT(nu_disp_set_alphablend_opmode, Set alpha blending opmode);
#endif

#if defined(DISP_USING_CURSOR)
static void nu_disp_show_cursor(void)
{
    rt_device_open(&nu_fbdev[eLayer_Cursor].dev, RT_DEVICE_FLAG_RDWR);
}
MSH_CMD_EXPORT(nu_disp_show_cursor, show hw cursor);

static void nu_disp_hide_cursor(void)
{
    rt_device_close(&nu_fbdev[eLayer_Cursor].dev);
}
MSH_CMD_EXPORT(nu_disp_hide_cursor, hide hw cursor);
#endif

#endif /* if defined(BSP_USING_DISP) */
