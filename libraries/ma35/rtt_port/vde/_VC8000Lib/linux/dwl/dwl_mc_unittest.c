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

#include <stdlib.h>
#include <string.h>
#include "basetype.h"
#include "dwl.h"
#include "dwlthread.h"
#include "asic.h"
#include "assert.h"
#include "regdrv.h"

static void *MCDecodeThread(void *);
static u32 picSize(void);

void *pMCCore;
u8 *pMCUnitOut;
u32 *pMCBase;
u32 keepRunning;

pthread_t testMCThread;
pthread_mutex_t unitCoreStart;
pthread_mutex_t unitCoreDone;

void MCUnitTestInit(void)
{
    keepRunning = 1;
    pMCUnitOut = NULL;

    pMCCore = (void *)HwCoreInit();
    assert(pMCCore != NULL);

    pMCBase = HwCoreGetBase(pMCCore);

    pthread_mutex_init(&unitCoreStart, NULL);
    pthread_mutex_lock(&unitCoreStart);

    pthread_mutex_init(&unitCoreDone, NULL);
    pthread_mutex_lock(&unitCoreDone);

    pthread_create(&testMCThread, NULL, MCDecodeThread, NULL);
}

void MCUnitTestRelease(void)
{
    keepRunning = 0;
    pthread_mutex_unlock(&unitCoreStart);
    free(pMCUnitOut);
    HwCoreRelease(pMCCore);
    pthread_mutex_destroy(&unitCoreStart);
    pthread_mutex_destroy(&unitCoreDone);

}

void MCUnitStartHW(u32 *regBase, u32 numRegs)
{
    u32 i;

    /* Prepare unittest core for decode. */
    for (i = 0; i < numRegs; i++)
        pMCBase[i] = regBase[i];

    /* Alloc output buffer. */
    if (pMCUnitOut == NULL)
        pMCUnitOut = calloc(picSize(), sizeof(u8));
    /* Write in our own picture buffer. */
    pMCBase[13] = (u32) pMCUnitOut;
    /* Unit test core gets go-ahead. */
    pthread_mutex_unlock(&unitCoreStart);
}

u32 MCUnitOutputError(u32 *output)
{
    /* Wait until unittest core is finished. */
    pthread_mutex_lock(&unitCoreDone);
    return memcmp(pMCUnitOut, output, picSize());
}

static void *MCDecodeThread(void *param)
{
    UNUSED(param); /* Suppress compiler warning. */
    while (keepRunning)
    {
        /* Waiting for go-ahead from main thread. */
        pthread_mutex_lock(&unitCoreStart);
        runAsic(pMCCore);
        pthread_mutex_unlock(&unitCoreDone);
    }
    pthread_exit((void *)0);
}

static u32 picSize(void)
{
    /* MB width * MB Height * 16 * 16 * 1.5 */
    u32 size = ((pMCBase[4] >> 23) & 0x1FF) * ((pMCBase[4] >> 11) & 0xFF) * 16 * 24;
    assert(size != 0);
    return size;
}
