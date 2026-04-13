/*------------------------------------------------------------------------------
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--        only as expressly authorized by a licensing agreement from          --
--                                                                            --
--                            Hantro Products Oy.                             --
--                                                                            --
--                   (C) COPYRIGHT 2012 HANTRO PRODUCTS OY                    --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--                 The entire notice above must be reproduced                 --
--                  on all copies and should not be removed.                  --
--                                                                            --
--------------------------------------------------------------------------------
--
--  Description : Multicore HW model unit test
--
------------------------------------------------------------------------------*/

#ifndef MC_UNITTEST_H
#define MC_UNITTEST_H

#include "basetype.h"

void MCUnitTestInit(void);
void MCUnitTestRelease(void);
u32 MCUnitOutputError(u32 *output);
void MCUnitStartHW(u32 *regBase, u32 numRegs);

#endif
