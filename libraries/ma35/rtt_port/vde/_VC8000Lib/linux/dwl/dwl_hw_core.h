/*  Copyright 2012 Google Inc. All Rights Reserved. */
/*  Author: vmr@google.com (Ville-Mikko Rautio) */

#ifndef __DWL_HW_CORE_H__
#define __DWL_HW_CORE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "basetype.h"
#include <dwlthread.h>

typedef const void *core;

core hw_core_init(void);
void hw_core_release(core instance);

int hw_core_try_lock(core inst);
void hw_core_unlock(core inst);

u32 *hw_core_get_base_address(core instance);
void hw_core_setid(core instance, int id);
int hw_core_getid(core instance);

/* Starts the execution on a core. */
void hw_core_dec_enable(core instance);
void hw_core_pp_enable(core instance, int start);

/* Tries to interrupt the execution on a core as soon as possible. */
void hw_core_disable(core instance);

int  hw_core_wait_dec_rdy(core instance);
int  hw_core_wait_pp_rdy(core instance);

int  hw_core_post_pp_rdy(core instance);
int  hw_core_post_dec_rdy(core instance);

int hw_core_is_dec_rdy(core instance);
int hw_core_is_pp_rdy(core instance);

void hw_core_set_hw_rdy_sem(core instance, sem_t *rdy);

#ifdef __cplusplus
}
#endif

#endif  /* #ifndef __DWL_HW_CORE_H__ */

