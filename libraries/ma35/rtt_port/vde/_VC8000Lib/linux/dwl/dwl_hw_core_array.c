/*  Copyright 2012 Google Inc. All Rights Reserved. */
/*  Author: vmr@google.com (Ville-Mikko Rautio) */

#include "asic.h"
#include "decapicommon.h"
#include "dwl_hw_core_array.h"
#include "dwlthread.h"

#include <assert.h>
#include <stdlib.h>
#include <errno.h>

#ifdef CORES
    #if CORES > MAX_ASIC_CORES
        #error Too many cores! Check max number of cores in <decapicommon.h>
    #else
        #define HW_CORE_COUNT       CORES
    #endif
#else
    #define HW_CORE_COUNT       1
#endif

typedef struct hw_core_container_
{
    core core_;
} hw_core_container;

typedef struct hw_core_array_instance_
{
    u32 num_of_cores_;
    hw_core_container *cores_;
    sem_t core_lock_;
    sem_t core_rdy_;
} hw_core_array_instance;

hw_core_array initialize_core_array()
{
    u32 i;
    hw_core_array_instance *array = malloc(sizeof(hw_core_array_instance));
    if (array == NULL)
        return NULL;
    array->num_of_cores_ = get_core_count();
    sem_init(&array->core_lock_, 0, array->num_of_cores_);

    sem_init(&array->core_rdy_, 0, 0);

    array->cores_ = calloc(array->num_of_cores_, sizeof(hw_core_container));
    if (array->cores_ == NULL)
    {
        sem_destroy(&array->core_lock_);
        sem_destroy(&array->core_rdy_);
        free(array);
        return NULL;
    }
    assert(array->cores_);
    for (i = 0; i < array->num_of_cores_; i++)
    {
        array->cores_[i].core_ = hw_core_init();
        assert(array->cores_[i].core_);

        hw_core_setid(array->cores_[i].core_, i);
        hw_core_set_hw_rdy_sem(array->cores_[i].core_, &array->core_rdy_);
    }
    return array;
}

void release_core_array(hw_core_array inst)
{
    u32 i;

    hw_core_array_instance *array = (hw_core_array_instance *)inst;

    /* TODO(vmr): Wait for all cores to finish. */
    for (i = 0; i < array->num_of_cores_; i++)
    {
        hw_core_release(array->cores_[i].core_);
    }

    free(array->cores_);
    sem_destroy(&array->core_lock_);
    sem_destroy(&array->core_rdy_);
    free(array);
}

core borrow_hw_core(hw_core_array inst)
{
    u32 i = 0;
    hw_core_array_instance *array = (hw_core_array_instance *)inst;

#ifdef USE_DDD_DEBUGGER
    while (sem_wait(&array->core_lock_) != 0 && errno == EINTR) {}
#else
    sem_wait(&array->core_lock_);
#endif

    while (!hw_core_try_lock(array->cores_[i].core_))
    {
        i++;
    }

    return array->cores_[i].core_;
}

void return_hw_core(hw_core_array inst, core core)
{
    hw_core_array_instance *array = (hw_core_array_instance *)inst;

    hw_core_unlock(core);

    sem_post(&array->core_lock_);
}

u32 get_core_count()
{
    /* TODO(vmr): implement dynamic mechanism for calculating. */
    return HW_CORE_COUNT;
}

core get_core_by_id(hw_core_array inst, int nth)
{
    hw_core_array_instance *array = (hw_core_array_instance *)inst;

    assert(nth < (int)get_core_count());

    return array->cores_[nth].core_;
}

int wait_any_core_rdy(hw_core_array inst)
{
    hw_core_array_instance *array = (hw_core_array_instance *)inst;

#ifdef USE_DDD_DEBUGGER
    int ret = 1;
    while (ret != 0)
    {
        ret = sem_wait(&array->core_rdy_);
        if (errno != EINTR)
            break;
    }
    return ret;
#else
    return sem_wait(&array->core_rdy_);
#endif
}

int stop_core_array(hw_core_array inst)
{
    hw_core_array_instance *array = (hw_core_array_instance *)inst;
    return sem_post(&array->core_rdy_);
}
