/*************************************************************************//**
 * @file     vc8000_glue.c
 * @brief    Provide interface for integration with VC8000 library export functions.
 *
 * @copyright (C) 2023 Nuvoton Technology Corp. All rights reserved.
*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NuMicro.h"

#include "basetype.h"
#include "h264hwd_regdrv.h"
#include "h264hwd_asic.h"
#include "h264decapi.h"
#include "dwl.h"
#include "ppinternal.h"
#include "vc8000_glue.h"
#include "vc8000_lib.h"

static struct vc8000_inst  _vc8000_inst[VC8000_MAX_INST];
static struct h264_ctx     _h264_ctx[H264_MAX_CTX];
static struct jpeg_ctx     _jpeg_ctx[JPEG_MAX_CTX];

static int _vc8000_inst_used[VC8000_MAX_INST];
static int _h264_ctx_used[H264_MAX_CTX];
static int _jpeg_ctx_used[JPEG_MAX_CTX];

static rt_mutex_t pMutexVDE = RT_NULL;

void vde_os_unlock(void)
{
    rt_mutex_release(pMutexVDE);
}

void vde_os_lock(void)
{
    rt_mutex_take(pMutexVDE, RT_WAITING_FOREVER);
}

uint32_t vde_os_gettick(void)
{
    return (uint32_t)rt_tick_get();
}

static int is_valid_handle(int handle)
{
    uint32_t idx;

    handle -= HANDLE_BASE;

    if ((handle < 0) || (handle >= VC8000_MAX_INST))
        return VC8000_ERR_INVALID_INSTANCE;

    if (_vc8000_inst_used[handle] == 0)
        return VC8000_ERR_INVALID_INSTANCE;

    idx = _vc8000_inst[handle].hw_data_index;

    if (_vc8000_inst[handle].codec == VC8000_CODEC_H264)
    {
        if ((idx >= H264_MAX_CTX) || (_h264_ctx_used[idx] == 0))
            return VC8000_ERR_LIB_SERIOUS;
    }
    else if (_vc8000_inst[handle].codec == VC8000_CODEC_JPEG)
    {
        if ((idx >= JPEG_MAX_CTX) || (_jpeg_ctx_used[idx] == 0))
            return VC8000_ERR_LIB_SERIOUS;
    }
    return 0;
}

int VC8000_DrvInit(uint32_t buf_base, uint32_t buf_size)
{
    int  i;

    if (!pMutexVDE)
    {
        pMutexVDE = rt_mutex_create("VDE", RT_IPC_FLAG_PRIO);
        RT_ASSERT(pMutexVDE);
    }

    vde_os_lock();

    for (i = 0; i < VC8000_MAX_INST; i++)
        _vc8000_inst_used[i] = 0;

    for (i = 0; i < H264_MAX_CTX; i++)
        _h264_ctx_used[i] = 0;

    for (i = 0; i < JPEG_MAX_CTX; i++)
        _jpeg_ctx_used[i] = 0;

    vc8000_mem_init(buf_base, buf_size);
    hx170dec_init();

    vde_os_unlock();

    return 0;
}

int VC8000_Open(E_VC8000_CODEC codec, pfnPicFlush pic_flush)
{
    int  i, j, ret = VC8000_ERR_NO_FREE_INSTANCE;

    vde_os_lock();

    for (i = 0; i < VC8000_MAX_INST; i++)
    {

        if (_vc8000_inst_used[i] == 0)
        {
            switch (codec)
            {
            case VC8000_CODEC_H264:
            {
                for (j = 0; j < H264_MAX_CTX; j++)
                {
                    if (_h264_ctx_used[j] == 0)
                    {
                        _vc8000_inst_used[i] = 1;
                        _h264_ctx_used[j] = 1;

                        memset(&_vc8000_inst[i], 0, sizeof(struct vc8000_inst));
                        memset(&_h264_ctx[j], 0, sizeof(struct h264_ctx));

                        _vc8000_inst[i].codec = VC8000_CODEC_H264;
                        _vc8000_inst[i].hw_data_index = j;
                        _vc8000_inst[i].pic_flush = pic_flush;

                        if (vc8000_h264_dec_init(&_vc8000_inst[i], &_h264_ctx[j]))
                        {
                            _vc8000_inst_used[i] = 0;
                            _h264_ctx_used[j] = 0;
                            ret = VC8000_ERR_DEC_INIT;
                        }
                        else
                        {
                            ret = HANDLE_BASE + i;
                        }
                        goto exit_VC8000_Open;
                    }
                } // for (j = 0; j < H264_MAX_CTX; j++)
            } // case VC8000_CODEC_H264:
            break;

            case VC8000_CODEC_JPEG:
            {
                for (j = 0; j < JPEG_MAX_CTX; j++)
                {
                    if (_jpeg_ctx_used[j] == 0)
                    {
                        _vc8000_inst_used[i] = 1;
                        _jpeg_ctx_used[j] = 1;

                        memset(&_vc8000_inst[i], 0, sizeof(struct vc8000_inst));
                        memset(&_jpeg_ctx[j], 0, sizeof(struct jpeg_ctx));

                        _vc8000_inst[i].codec = VC8000_CODEC_JPEG;
                        _vc8000_inst[i].hw_data_index = j;
                        _vc8000_inst[i].pic_flush = pic_flush;

                        if (vc8000_jpeg_dec_init(&_vc8000_inst[i], &_jpeg_ctx[j]))
                        {
                            _vc8000_inst_used[i] = 0;
                            _jpeg_ctx_used[j] = 0;
                            ret = VC8000_ERR_DEC_INIT;
                        }
                        else
                        {
                            ret = HANDLE_BASE + i;
                        }
                        goto exit_VC8000_Open;
                    }
                } //for (j = 0; j < JPEG_MAX_CTX; j++)
            } // case VC8000_CODEC_JPEG:
            break;

            default:
                sysprintf("%s - serious error! No such codec!\n", __func__);
                ret = VC8000_ERR_LIB_SERIOUS;
                goto exit_VC8000_Open;
            }

        } // if (_vc8000_inst_used[i] == 0)

    } // for (i = 0; i < VC8000_MAX_INST; i++)

exit_VC8000_Open:

    vde_os_unlock();

    return ret;
}

int VC8000_PostProcess(int handle, struct pp_params *pp)
{
    int ret, vidx;

    vde_os_lock();

    ret = is_valid_handle(handle);
    if (ret || !pp)
        goto  exit_VC8000_PostProcess;

    vidx = handle - HANDLE_BASE;

    pp->pp_out_dst = addr_s(pp->pp_out_dst);

    memcpy(&_vc8000_inst[vidx].pp_ctx, pp, sizeof(struct pp_params));

    _vc8000_inst[vidx].enable_pp = 1;
    _vc8000_inst[vidx].pp_changed = 1;

exit_VC8000_PostProcess:

    vde_os_unlock();

    return ret;
}

int VC8000_Decode(int handle, uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t *remain)
{
    int ret, vidx, hw_idx;

    if (!in || !in_size)
        return VC8000_ERR_INVALID_PARAM;

    vde_os_lock();

    ret = is_valid_handle(handle);
    if (ret)
        goto exit_VC8000_Decode;

    vidx = handle - HANDLE_BASE;

    hw_idx = _vc8000_inst[vidx].hw_data_index;

    /* Flush input buffer into memory. */
    extern void rt_hw_cpu_dcache_clean(void *addr, int size);
    rt_hw_cpu_dcache_clean((void *)in, in_size);

    switch (_vc8000_inst[vidx].codec)
    {
    case VC8000_CODEC_H264:
    {
#define DEF_MAX_STREAM_SIZE    (8*1024*1024)
        uint32_t u32TriggerSize = (in_size >= DEF_MAX_STREAM_SIZE) ? DEF_MAX_STREAM_SIZE : in_size;
        ret = ma35d1_h264_dec_run(&_vc8000_inst[vidx], &_h264_ctx[hw_idx], in, u32TriggerSize, out, remain);
        *remain = *remain + (in_size - u32TriggerSize);
    }
    break;

    case VC8000_CODEC_JPEG:
        ret = ma35d1_jpeg_dec_run(&_vc8000_inst[vidx], &_jpeg_ctx[hw_idx], in, in_size, out);
        *remain = 0;
        break;

    default:
        break;
    }

exit_VC8000_Decode:

    vde_os_unlock();

    return ret;
}

int VC8000_Close(int handle)
{
    int  ret, vidx, hw_idx;

    vde_os_lock();

    ret = is_valid_handle(handle);
    if (ret)
        goto exit_VC8000_Close;

    vidx = handle - HANDLE_BASE;
    hw_idx = _vc8000_inst[vidx].hw_data_index;

    switch (_vc8000_inst[vidx].codec)
    {
    case VC8000_CODEC_H264:
        ma35d1_h264_dec_exit(&_vc8000_inst[vidx], &_h264_ctx[hw_idx]);
        _h264_ctx_used[hw_idx] = 0;
        break;

    case VC8000_CODEC_JPEG:
        ma35d1_jpeg_dec_exit(&_vc8000_inst[vidx], &_jpeg_ctx[hw_idx]);
        _jpeg_ctx_used[hw_idx] = 0;
        break;

    default:
        break;
    }

    _vc8000_inst_used[vidx] = 0;

exit_VC8000_Close:

    vde_os_unlock();

    return ret;
}
