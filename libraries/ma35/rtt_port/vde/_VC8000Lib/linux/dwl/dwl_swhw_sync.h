/* Copyright 2012 Google Inc. All Rights Reserved. */
/* Author: attilanagy@google.com (Atti Nagy) */


#ifndef SOFTWARE_LINUX_DWL_DWL_SWHW_SYNC_H_
#define SOFTWARE_LINUX_DWL_DWL_SWHW_SYNC_H_

#include <unistd.h>

#include "dwl.h"
#include "dwlthread.h"
#include "decapicommon.h"

typedef struct mc_listener_thread_params_
{
    int fd;
    int bStopped;
    unsigned int nDecCores;
    unsigned int nPPCores;
    sem_t sc_dec_rdy_sem[MAX_ASIC_CORES];
    sem_t sc_pp_rdy_sem[MAX_ASIC_CORES];
    volatile u32 *pRegBase[MAX_ASIC_CORES];
    DWLIRQCallbackFn *pCallback[MAX_ASIC_CORES];
    void *pCallbackArg[MAX_ASIC_CORES];
} mc_listener_thread_params;

void *thread_mc_listener(void *args);

#endif /* SOFTWARE_LINUX_DWL_DWL_SWHW_SYNC_H_ */
