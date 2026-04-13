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
--  Abstract :
--
------------------------------------------------------------------------------*/

#ifndef ERRORHANDLING_H_DEFINED
#define ERRORHANDLING_H_DEFINED

#include "basetype.h"

#ifndef HANTRO_TRUE
    #define HANTRO_TRUE     (1)
#endif /* HANTRO_TRUE */

#ifndef HANTRO_FALSE
    #define HANTRO_FALSE    (0)
#endif /* HANTRO_FALSE*/

void PreparePartialFreeze(u8 *pDecOut, u32 vopWidth, u32 vopHeight);
u32  ProcessPartialFreeze(u8 *pDecOut, const u8 *pRefPic, u32 vopWidth,
                          u32 vopHeight, u32 copy);

#endif /* ERRORHANDLING_H_DEFINED */
