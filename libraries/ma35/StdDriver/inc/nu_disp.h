/**************************************************************************//**
 * @file     nu_disp.h
 * @brief    DISP driver header file
 *
 * SPDX-License-Identifier: Apache-2.0
 * @copyright (C) 2020 Nuvoton Technology Corp. All rights reserved.
 *****************************************************************************/
#ifndef __NU_DISP_H__
#define __NU_DISP_H__

#ifdef __cplusplus
extern "C"
{
#endif


/** @addtogroup Standard_Driver Standard Driver
  @{
*/

/** @addtogroup DISP_Driver DISP Driver
  @{
*/

/** @addtogroup DISP_EXPORTED_CONSTANTS DISP Exported Constants
  @{
*/

typedef enum
{
    ePolarity_Disable  = -1,   /*!< Pins disable */
    ePolarity_Positive = 0,    /*!< Pins are active high */
    ePolarity_Negative = 1     /*!< Pins are active low */
} E_POLARITY;

typedef enum
{
    eDPIFmt_D16CFG1,      /*!< DPI interface data format D16CFG1 */
    eDPIFmt_D16CFG2,      /*!< DPI interface data format D16CFG2 */
    eDPIFmt_D16CFG3,      /*!< DPI interface data format D16CFG3 */
    eDPIFmt_D18CFG1,      /*!< DPI interface data format D18CFG1 */
    eDPIFmt_D18CFG2,      /*!< DPI interface data format D18CFG2 */
    eDPIFmt_D24           /*!< DPI interface data format D24 */
} E_DPI_DATA_FMT;

typedef enum
{
    eLayer_Video   = 0,   /*!< Framebuffer layer */
    eLayer_Overlay = 1,   /*!< Overlay layer */
    eLayer_Cursor  = 2,   /*!< H/W Cursor layer */
    eLayer_Cnt
} E_DISP_LAYER;

typedef enum
{
    eYUV_709_BT709   = 1,   /*!< YUV standard 709_BT709 */
    eYUV_2020_BT2020 = 3,   /*!< YUV standard 2020_BT2020 */
} E_YUV_STANDARD;

typedef enum
{
    eFBFmt_X4R4G4B4      = 0,   /*!< X4R4G4B4 format */
    eFBFmt_A4R4G4B4      = 1,   /*!< A4R4G4B4 format */
    eFBFmt_X1R5G5B5      = 2,   /*!< X1R5G5B5 format */
    eFBFmt_A1R5G5B5      = 3,   /*!< A1R5G5B5 format */
    eFBFmt_R5G6B5        = 4,   /*!< R5G6B5 format */
    eFBFmt_X8R8G8B8      = 5,   /*!< X8R8G8B8 format */
    eFBFmt_A8R8G8B8      = 6,   /*!< A8R8G8B8 format */
    eFBFmt_YUY2          = 7,   /*!< YUY2 format */
    eFBFmt_UYVY          = 8,   /*!< UYVY format */
    eFBFmt_INDEX8        = 9,   /*!< INDEX8 format */
    eFBFmt_MONOCHROME    = 10,  /*!< MONOCHROME format */
    eFBFmt_YV12          = 15,  /*!< YV12 format */
    eFBFmt_A8            = 16,  /*!< A8 format */
    eFBFmt_NV12          = 17,  /*!< NV12 format */
    eFBFmt_NV16          = 18,  /*!< NV16 format */
    eFBFmt_RG16          = 19,  /*!< RG16 format */
    eFBFmt_R8            = 20,  /*!< R8 format */
    eFBFmt_NV12_10BIT    = 21,  /*!< NV12_10BIT format */
    eFBFmt_A2R10G10B10   = 22,  /*!< A2R10G10B10 format */
    eFBFmt_NV16_10BIT    = 23,  /*!< NV16_10BIT format */
    eFBFmt_INDEX1        = 24,  /*!< INDEX1 format */
    eFBFmt_INDEX2        = 25,  /*!< INDEX2 format */
    eFBFmt_INDEX4        = 26,  /*!< INDEX4 format */
    eFBFmt_P010          = 27,  /*!< P010 format */
    eFBFmt_NV12_10BIT_L1 = 28,  /*!< NV12_10BIT_L1 format */
    eFBFmt_NV16_10BIT_L1 = 29   /*!< NV16_10BIT_L1 format */
} E_FB_FMT;

typedef enum
{
    eOPAQUE,     /*!< OPAQUE effects */
    eMASK,       /*!< MASK effect */
    eKEY         /*!< OPAQUE effect */
} E_TRANSPARENCY_MODE;

typedef enum
{
    DC_BLEND_MODE_CLEAR,
    DC_BLEND_MODE_SRC,
    DC_BLEND_MODE_DST,
    DC_BLEND_MODE_SRC_OVER,
    DC_BLEND_MODE_DST_OVER,
    DC_BLEND_MODE_SRC_IN,
    DC_BLEND_MODE_DST_IN,
    DC_BLEND_MODE_SRC_OUT
} E_DC_BLEND_MODE;

typedef enum
{
    eGloAM_NORMAL,
    eGloAM_GLOBAL,
    eGloAM_SCALED
} E_GLOBAL_ALPHA_MODE;

typedef enum
{
    eBM_ZERO,
    eBM_ONE,
    eBM_NORMAL,
    eBM_INVERSED,
    eBM_COLOR,
    eBM_COLOR_INVERSED,
    eBM_SATURATED_ALPHA,
    eBM_SATURATED_DEST_ALPHA
} E_BLENDING_MODE;

typedef enum
{
    eCURSOR_FMT_DISABLE = 0,
    eCURSOR_FMT_MASKED,
    eCURSOR_FMT_ARGB8888
} E_CURSOR_FMT;

typedef struct
{
    uint32_t  u32X;
    uint32_t  u32Y;
} S_COORDINATE;

typedef struct
{
    /*
        Set display panel timing.
        htotal: u32HA + u32HBP + u32HFP + u32HSL
        vtotal: u32VA + u32VBP + u32VFP + u32VSL
        Panel Pixel Clock frequency: htotal * vtotal * fps
    */
    uint32_t u32PCF;    /*!< Panel Pixel Clock Frequency in Hz   */

    uint32_t u32HA;     /*!< Horizontal Active, Horizontal panel resolution   */
    uint32_t u32HSL;    /*!< Horizontal Sync Length, panel timing   */
    uint32_t u32HFP;    /*!< Horizontal Front Porch, panel timing   */
    uint32_t u32HBP;    /*!< Horizontal Back Porch, panel timing   */
    uint32_t u32VA;     /*!< Vertical Active, Vertical panel resolution in pixels   */
    uint32_t u32VSL;    /*!< Vertical Sync Length, panel timing   */
    uint32_t u32VFP;    /*!< Vertical Front Porch, panel timing   */
    uint32_t u32VBP;    /*!< Vertical Back Porch, panel timing   */

    E_POLARITY eHSPP;   /*!< Polarity of the horizontal sync pulse   */
    E_POLARITY eVSPP;   /*!< VSync Pulse Polarity   */

} DISP_LCD_TIMING;

typedef struct
{
    E_DPI_DATA_FMT   eDpiFmt; // DPI Data Format
    E_POLARITY       eDEP;    // DE Polarity
    E_POLARITY       eDP;     // DATA Polarity
    E_POLARITY       eCP;     // CLOCK Polarity
} DISP_PANEL_CONF;

typedef struct
{
    uint32_t u32ResolutionWidth;  /*!< Panel Width    */
    uint32_t u32ResolutionHeight; /*!< Panel Height   */
    DISP_LCD_TIMING sLcdTiming;   /*!< Panel timings for some registers   */
    DISP_PANEL_CONF sPanelConf;   /*!< Panel Configure information   */
} DISP_LCD_INFO;

typedef struct
{
    E_CURSOR_FMT  eFmt;
    uint32_t      u32FrameBuffer;

    S_COORDINATE  sHotSpot;
    S_COORDINATE  sInitPosition;

    union
    {
        uint32_t u32BGColor;
        struct
        {
            uint8_t B;
            uint8_t G;
            uint8_t R;
        } S_BGCOLOR;
    };

    union
    {
        uint32_t u32FGColor;
        struct
        {
            uint8_t B;
            uint8_t G;
            uint8_t R;
        } S_FGCOLOR;
    };

} DISP_CURSOR_CONF;

#define DISP_ENABLE_INT()     (DISP->DisplayIntrEnable |=  DISP_DisplayIntrEnable_DISP0_Msk)
#define DISP_DISABLE_INT()    (DISP->DisplayIntrEnable &= ~DISP_DisplayIntrEnable_DISP0_Msk)
#define DISP_GET_INTSTS()     (DISP->DisplayIntr & DISP_DisplayIntr_DISP0_Msk)

#define DISP_CURSOR_SET_FORMAT(f)  (DISP->CursorConfig = (DISP->CursorConfig & ~DISP_CursorConfig_FORMAT_Msk) | f )

#define DISP_VIDEO_IS_UNDERFLOW()    ((DISP->FrameBufferConfig0 & DISP_FrameBufferConfig0_UNDERFLOW_Msk) ? 1 : 0 )
#define DISP_OVERLAY_IS_UNDERFLOW()  ((DISP->OverlayConfig0 & DISP_OverlayConfig0_UNDERFLOW_Msk) ? 1 : 0)

#define DISP_CURSOR_SET_POSITION(x,y) \
                    ( DISP->CursorLocation = \
                      ((x<<DISP_CursorLocation_X_Pos) & DISP_CursorLocation_X_Msk) | \
                      ((y<<DISP_CursorLocation_Y_Pos) & DISP_CursorLocation_Y_Msk) )

#define DISP_CURSOR_SET_BGCOLOR(r,g,b) \
                    ( DISP->CursorBackground = \
                      ((r<<DISP_CursorBackground_RED_Pos) & DISP_CursorBackground_RED_Msk) | \ ((g<<DISP_CursorBackground_GREEN_Pos) & DISP_CursorBackground_GREEN_Msk) | \ ((b<<DISP_CursorBackground_BLUE_Pos) & DISP_CursorBackground_BLUE_Msk) )

#define DISP_CURSOR_SET_FGCOLOR(r,g,b) \
                    ( DISP->CursorForeground = \
                      ((r<<DISP_CursorForeground_RED_Pos) & DISP_CursorForeground_RED_Msk) | \ ((g<<DISP_CursorForeground_GREEN_Pos) & DISP_CursorForeground_GREEN_Msk) | \ ((b<<DISP_CursorForeground_BLUE_Pos) & DISP_CursorForeground_BLUE_Msk) )

int32_t DISP_LCDInit(const DISP_LCD_INFO *psLCDInfo);
int32_t DISP_LCDDeinit(void);
int DISP_SetFBConfig(E_DISP_LAYER eLayer, E_FB_FMT eFbFmt, uint32_t u32ResWidth, uint32_t u32ResHeight, uint32_t u32DMAFBStartAddr);
void DISP_SetPanelConf(DISP_PANEL_CONF *psPanelConf);
void DISP_SetTiming(DISP_LCD_TIMING *psLCDTiming);
int DISP_Trigger(E_DISP_LAYER eLayer, uint32_t u32Action);
int DISP_SetTransparencyMode(E_DISP_LAYER eLayer, E_TRANSPARENCY_MODE eTM);
int DISP_SetBlendOpMode(E_DC_BLEND_MODE eDCBM, E_GLOBAL_ALPHA_MODE eGloAM_Src, E_GLOBAL_ALPHA_MODE eGloAM_Dst);
void DISP_SetBlendValue(uint32_t u32GloAV_Src, uint32_t u32GloAV_Dst);
void DISP_SetColorKeyValue(uint32_t u32ColorKeyLow, uint32_t u32ColorKeyHigh);
int DISP_SetFBAddr(E_DISP_LAYER eLayer, uint32_t u32DMAFBStartAddr);
int DISP_SetFBFmt(E_DISP_LAYER eLayer, E_FB_FMT eFbFmt, uint32_t u32Pitch);
uint32_t DISP_LCDTIMING_GetFPS(const DISP_LCD_TIMING *psDispLCDTiming);
uint32_t DISP_LCDTIMING_SetFPS(uint32_t u32FPS);
void DISP_SetCursorPosition(uint32_t u32X, uint32_t u32Y);
void DISP_InitCursor(DISP_CURSOR_CONF *psCursorConf);
const DISP_LCD_TIMING *DISP_GetLCDTimingCtx(void);

/*@}*/ /* end of group DISP_EXPORTED_FUNCTIONS */

/*@}*/ /* end of group DISP_Driver */

/*@}*/ /* end of group Standard_Driver */

#ifdef __cplusplus
}
#endif

#endif /* __NU_DISP_H__ */

