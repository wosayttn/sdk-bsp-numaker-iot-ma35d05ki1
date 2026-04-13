/**************************************************************************//**
 * @file     vc8000_glue.h
 * @brief    VC8000 library header file
 *
 * SPDX-License-Identifier: Apache-2.0
 * @copyright (C) 2023 Nuvoton Technology Corp. All rights reserved.
 ******************************************************************************/
#ifndef  _VC8000_GLUE_H_
#define  _VC8000_GLUE_H_

#include "vc8000_lib.h"

#define H264_MAX_CTX        4        /* maximum number of H264 context created at the same time */
#define JPEG_MAX_CTX        1        /* maximum number of JPEG context created at the same time */
#define VC8000_MAX_INST     (H264_MAX_CTX + JPEG_MAX_CTX)  /* maximum instance created at the same time */
#define HANDLE_BASE       100

#define jiffies             (vde_os_gettick())

// For OS porting
void vde_os_unlock(void);
void vde_os_lock(void);
uint32_t vde_os_gettick(void);
void vde_os_wait_event(uint32_t wait_ms);
void vde_os_signal_event(void);

/*
 *  PP parameters of H264/JPEG context
 */
struct h264_ctx
{
    H264DecInfo     decInfo;
    H264DecInst     decInst;   /* is typdef (void *) */
    H264DecInput    decInput;
    H264DecOutput   decOutput;
    H264DecPicture  decPicture;
    int             eos;
    int             picDecodeNumber;
    int             picDisplayNumber;
    PPInst          ppInst;
    PPConfig        ppConfig;
    bool            header_parsed;
};

struct jpeg_ctx
{
    uint32_t        y_addr;
    uint32_t        u_addr;
    uint32_t        v_addr;
    PPInst          ppInst;
    PPConfig        ppConfig;
};

struct vc8000_inst
{
    E_VC8000_CODEC  codec;
    uint32_t        hw_data_index;
    int             enable_pp;
    uint32_t        pp_out_addr;
    int             pp_changed;
    int             pp_wait_vsync;  /* 0 ~ 33; 0 is no wait */
    struct pp_params pp_ctx;
    PPOutputBuffers pp_buffers;
    pfnPicFlush     pic_flush;
};

int vc8000_mem_init(uint32_t mem_base, uint32_t mem_size);
int vc8000_h264_dec_init(struct vc8000_inst *inst, struct h264_ctx *hctx);
int ma35d1_h264_dec_run(struct vc8000_inst *inst, struct h264_ctx *hctx, uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t *remain);
void ma35d1_h264_dec_exit(struct vc8000_inst *inst, struct h264_ctx *hctx);
int vc8000_jpeg_dec_init(struct vc8000_inst *inst, struct jpeg_ctx *jctx);
void ma35d1_jpeg_dec_exit(struct vc8000_inst *inst, struct jpeg_ctx *jctx);
int ma35d1_jpeg_dec_run(struct vc8000_inst *inst, struct jpeg_ctx *jctx, uint8_t *in, uint32_t in_size, uint8_t *out);

#endif  /* _VC8000_GLUE_H_ */
