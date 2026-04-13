/* Copyright 2012 Google Inc. All Rights Reserved. */
/* Author: attilanagy@google.com (Atti Nagy) */

#include "basetype.h"
#include "dwl_hw_core_array.h"
#include "dwl_swhw_sync.h"
#include "dwlthread.h"


#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _DWL_DEBUG
#define DWL_DEBUG(fmt, args...) sysprintf(__FILE__":%d:%s() " fmt,\
        __LINE__, __func__, ## args)
#else
#define DWL_DEBUG(fmt, args...) do {} while (0)
#endif

extern hw_core_array gHwCoreArray;

void *thread_mc_listener(void *args)
{
    mc_listener_thread_params *params = (mc_listener_thread_params *) args;

    while (!params->bStopped)
    {
        u32 i, ret;

        ret = wait_any_core_rdy(gHwCoreArray);
        assert(ret == 0);

        if (params->bStopped)
            break;

        /* check all decoder IRQ status register */
        for (i = 0; i < params->nDecCores; i++)
        {
            core c = get_core_by_id(gHwCoreArray, i);

            /* check DEC IRQ status */
            if (hw_core_is_dec_rdy(c))
            {
                DWL_DEBUG("DEC IRQ by Core %d\n", i);

                if (params->pCallback[i] != NULL)
                {
                    params->pCallback[i](params->pCallbackArg[i], i);
                }
                else
                {
                    /* single core instance => post dec ready */
                    hw_core_post_dec_rdy(c);
                }

                break;
            }
        }

        /* check all post-processor IRQ status register */
        for (i = 0; i < params->nPPCores; i++)
        {
            core c = get_core_by_id(gHwCoreArray, i);

            /* check PP IRQ status */
            if (hw_core_is_pp_rdy(c))
            {
                DWL_DEBUG("PP IRQ by Core %d\n", i);

                /* single core instance => post pp ready */
                hw_core_post_pp_rdy(c);

                break;
            }
        }
    }

    return NULL;
}
