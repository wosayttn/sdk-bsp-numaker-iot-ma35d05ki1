/*  Copyright 2012 Google Inc. All Rights Reserved. */
/*  Author: vmr@google.com (Ville-Mikko Rautio) */

#ifndef __DWL_HW_CORE_ARRAY_H__
#define __DWL_HW_CORE_ARRAY_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "dwlthread.h"

#include "basetype.h"
#include "dwl_hw_core.h"

typedef const void *hw_core_array;

/* Initializes core array and individual hardware core abstractions. */
hw_core_array initialize_core_array();
/* Releases the core array and hardware core abstractions. */
void release_core_array(hw_core_array inst);

int stop_core_array(hw_core_array inst);

/* Get usage rights for single core. Blocks until there is available core. */
core borrow_hw_core(hw_core_array inst);
/* Returns previously borrowed |hw_core|. */
void return_hw_core(hw_core_array inst, core hw_core);

u32 get_core_count();

/* Get a reference to the nth core */
core get_core_by_id(hw_core_array inst, int nth);

/* wait for a core, any core, to finish processing */
int wait_any_core_rdy(hw_core_array inst);

#ifdef __cplusplus
}
#endif

#endif  /* __DWL_HW_CORE_ARRAY_H__ */

