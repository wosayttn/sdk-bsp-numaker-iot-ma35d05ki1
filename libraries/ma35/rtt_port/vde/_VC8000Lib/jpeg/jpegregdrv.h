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
--  Description :  X170 JPEG Decoder HW register access functions interface
--
------------------------------------------------------------------------------*/

#ifndef _JPG_HWREGDRV_H_
#define _JPG_HWREGDRV_H_

#include "basetype.h"
#include "deccfg.h"
#include "regdrv.h"

#define JPEG_X170_MODE_JPEG       3

#define JPEGDEC_X170_IRQ_DEC_RDY        DEC_8190_IRQ_RDY
#define JPEGDEC_X170_IRQ_BUS_ERROR      DEC_8190_IRQ_BUS
#define JPEGDEC_X170_IRQ_BUFFER_EMPTY   DEC_8190_IRQ_BUFFER
#define JPEGDEC_X170_IRQ_ASO            DEC_8190_IRQ_ASO
#define JPEGDEC_X170_IRQ_STREAM_ERROR   DEC_8190_IRQ_ERROR
#define JPEGDEC_X170_IRQ_SLICE_READY    DEC_8190_IRQ_SLICE
#define JPEGDEC_X170_IRQ_TIMEOUT        DEC_8170_IRQ_TIMEOUT

#endif /* #define _JPG_HWREGDRV_H_ */
