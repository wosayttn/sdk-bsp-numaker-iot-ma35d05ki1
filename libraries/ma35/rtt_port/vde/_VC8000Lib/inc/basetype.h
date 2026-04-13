/*------------------------------------------------------------------------------
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--        only as expressly authorized by a licensing agreement from          --
--                                                                            --
--                            Hantro Products Oy.                             --
--                                                                            --
--                   (C) COPYRIGHT 2011 HANTRO PRODUCTS OY                    --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--                 The entire notice above must be reproduced                 --
--                  on all copies and should not be removed.                  --
--                                                                            --
--------------------------------------------------------------------------------
--
--  Description : Basic type definitions.
--
------------------------------------------------------------------------------*/

/*!\file
 * \brief Basic type definitions.
 *
 * Basic numeric data type definitions used in the decoder software.
 */


#ifndef __BASETYPE_H__
#define __BASETYPE_H__

/*! \addtogroup common Common definitions
 *  @{
 */

#if defined( __linux__ ) || defined( WIN32 )
    #include <stddef.h>
#endif

#ifndef NULL
    #ifdef  __cplusplus
        #define NULL    0
    #else
        #define NULL    ((void *)0)
    #endif
#endif

#include "NuMicro.h"
#include "rtthread.h"
#include "rtdevice.h"
#include "stdio.h"

/* Macro to signal unused parameter. */
#define UNUSED(x) (void)(x)


/* add non-cache mask to get a 32-bits address */
#define NON_CACHE       0x40000000u
#define nc_addr(x)      ((uint32_t)(x) | NON_CACHE)

/* add non-cache mask to get a pointer for non-cache access */
#define nc_ptr(x)       ((void *)nc_addr(x))

/* translate a pointer to be 4GB safe and keep non-cache bit unchanged  */
#define ptr_nc_s(x)     ((void *)((uint32_t)(x) & NON_CACHE))

/* translate a pointer to be 4GB safe and clear non-cache bit. */
#define ptr_s(x)        ((void *)((uint32_t)(x) & ~NON_CACHE))

/* translate a pointer to be 4GB safe address and clear non-cache bit. */
#define addr_nc_s(x)    ((uint32_t)ptr_nc_s(x))

/* translate a pointer to be 4GB safe address and clear non-cache bit. */
#define addr_s(x)       ((uint32_t)ptr_s(x))

typedef unsigned char u8; /**< unsigned 8 bits integer value */
typedef signed char i8; /**< signed 8 bits integer value */
typedef unsigned short u16; /**< unsigned 16 bits integer value */
typedef signed short i16; /**< signed 16 bits integer value */
typedef unsigned int u32; /**< unsigned 32 bits integer value */
typedef signed int i32; /**< signed 8 bits integer value */

typedef long long  i64;
typedef unsigned long long u64;

#if defined(_WIN64)
    typedef unsigned long long g1_addr_t;
#else
    typedef unsigned long g1_addr_t; /**< unsigned 64 bits integer value */
#endif

/*!\cond SWDEC*/
/* SW decoder 16 bits types */
#if defined(VC1SWDEC_16BIT) || defined(MP4ENC_ARM11)
    typedef unsigned short u16x;
    typedef signed short i16x;
#else
    typedef unsigned int u16x;
    typedef signed int i16x;
#endif
/*!\endcond */


/*-----------------------------------------------------*/
/*  ychuang added                                      */
/*-----------------------------------------------------*/
#define PLAT_NUA3500

#define DWL_USE_DEC_IRQ             // use interrupt
#define VDEC_IRQ_NUM         VDE_IRQn

/* H264 Debug message setting */
// #define H264DEC_TRACE
// #define PP_TRACE
//#define MEMORY_USAGE_TRACE
//#define _DWL_DEBUG
//#define _DEBUG_PRINT

/* PP debug setting */
//#define _PPDEBUG_PRINT
//#define TRACE_PP_CTRL   sysprintf

/* JPEG debug setting */
//#define JPEGDEC_TRACE



/* Configuration */
#define PP_H264DEC_PIPELINE_SUPPORT
#define PP_JPEGDEC_PIPELINE_SUPPORT
#define PP_PIPELINE_ENABLED

//#define DWL_REFBUFFER_DISABLE

#define HLINA_START_ADDRESS    0xC0000000
#define HLINA_SIZE             64

/* data type */
#define off64_t     uint64_t
#define sem_t       int

#define bool        char

/* macro to get smaller of two values */
#ifndef MIN
    #define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* macro to get greater of two values */
#ifndef MAX
    #define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef TRUE
    #define TRUE        1
#endif
#ifndef FALSE
    #define FALSE       0
#endif

#ifndef true
    #define true        1
#endif
#ifndef false
    #define false       0
#endif

#ifndef size_t
    typedef unsigned int    size_t;
#endif

typedef unsigned int    __u32;

#define sysprintf           printf
//#define sysprintf(...)

#define ptr_to_u32(x)       ((uint32_t)(x))

extern void delay_us(int usec);
extern void dump_buff_hex(uint8_t *pucBuff, int nBytes);

extern int  hx170dec_init(void);
extern long hx170dec_ioctl(unsigned int cmd,  void *arg);

extern int  memalloc_init(void);
extern long memalloc_ioctl(unsigned int cmd, void *arg);


/*! @} - end group common */

#endif /* __BASETYPE_H__ */
