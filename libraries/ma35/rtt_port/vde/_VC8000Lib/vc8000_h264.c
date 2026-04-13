/*************************************************************************//**
 * @file     vc8000_h264_glue.c
 * @brief    Perform H264 bit stream decoding and provide interface for
 *           integration with VC8000 library export functions.
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

#define PIC_MSG(...)
//#define PIC_MSG    sysprintf

static void printDecodeReturn(struct h264_ctx *hctx, i32 retval);
static void printH264PicCodingType(struct h264_ctx *hctx, u32 *picType);

static int  h264_pp_init(struct vc8000_inst *inst, struct h264_ctx *hctx);
static int  h264_pp_exit(struct vc8000_inst *inst, struct h264_ctx *hctx);
static void h264_pp_in_config(struct vc8000_inst *inst, struct h264_ctx *hctx);
static int  h264_pp_out_config(struct vc8000_inst *inst, struct h264_ctx *hctx);

/*
 *  For monitoring frame rate
 */
static uint64_t _fps_check_jiffy;
static int      _decode_cnt, _collect_cnt, _report_times;

void  dump_buff_hex(uint8_t *pucBuff, int nBytes)
{
    int nIdx;

    nIdx = 0;
    while (nBytes > 0)
    {
        PIC_MSG("0x%04X  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", nIdx,
                pucBuff[nIdx], pucBuff[nIdx + 1], pucBuff[nIdx + 2], pucBuff[nIdx + 3], pucBuff[nIdx + 4], pucBuff[nIdx + 5],
                pucBuff[nIdx + 6], pucBuff[nIdx + 7], pucBuff[nIdx + 8], pucBuff[nIdx + 9], pucBuff[nIdx + 10],
                pucBuff[nIdx + 11], pucBuff[nIdx + 12], pucBuff[nIdx + 13], pucBuff[nIdx + 14], pucBuff[nIdx + 15]);
        nIdx += 16;
        nBytes -= 16;
        PIC_MSG("\n");
    }
    PIC_MSG("\n");
}

int vc8000_h264_dec_init(struct vc8000_inst *inst, struct h264_ctx *hctx)
{
    int err;

    hctx->picDecodeNumber = 1;
    hctx->picDisplayNumber = 1;
    hctx->header_parsed = false;

    err = H264DecInit(&hctx->decInst,
                      0,  //DISABLE_OUTPUT_REORDER
                      DEC_EC_PICTURE_FREEZE, // USE_VIDEO_FREEZE_CONCEALMENT
                      0,  //USE_DISPLAY_SMOOTHING
                      DEC_REF_FRM_TILED_DEFAULT,
                      0,
                      0,
                      0);
    if (err != H264DEC_OK)
    {
        PIC_MSG("H264 DECODER INITIALIZATION FAILED (%d)\n", err);
        return VC8000_ERR_DEC_INIT;
    }

    SetDecRegister(((decContainer_t *) hctx->decInst)->h264Regs, HWIF_DEC_LATENCY,
                   DEC_X170_LATENCY_COMPENSATION);
    SetDecRegister(((decContainer_t *) hctx->decInst)->h264Regs, HWIF_DEC_CLK_GATE_E,
                   DEC_X170_INTERNAL_CLOCK_GATING);
    SetDecRegister(((decContainer_t *) hctx->decInst)->h264Regs, HWIF_DEC_OUT_TILED_E,
                   DEC_X170_OUTPUT_FORMAT);
    SetDecRegister(((decContainer_t *) hctx->decInst)->h264Regs, HWIF_DEC_OUT_ENDIAN,
                   DEC_X170_OUTPUT_PICTURE_ENDIAN);
    SetDecRegister(((decContainer_t *) hctx->decInst)->h264Regs, HWIF_DEC_MAX_BURST,
                   DEC_X170_BUS_BURST_LENGTH);
    SetDecRegister(((decContainer_t *) hctx->decInst)->h264Regs, HWIF_DEC_DATA_DISC_E,
                   0);
    SetDecRegister(((decContainer_t *) hctx->decInst)->h264Regs, HWIF_SERV_MERGE_DIS,
                   DEC_X170_SERVICE_MERGE_DISABLE);

    _fps_check_jiffy = jiffies;
    _report_times = 0;
    _collect_cnt = 0;
    _decode_cnt = 0;
    return 0;
}

void ma35d1_h264_dec_exit(struct vc8000_inst *inst, struct h264_ctx *hctx)
{
    if (inst->enable_pp)
    {
        h264_pp_exit(inst, hctx);
    }
    H264DecRelease(hctx->decInst);
}

int ma35d1_h264_dec_run(struct vc8000_inst *inst, struct h264_ctx *hctx, uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t *remain)
{
    struct pp_params *pp = &inst->pp_ctx;
    int       ret, error;
    uint32_t in_remain, in_offset;

    if (!inst->enable_pp && !out)
        return VC8000_ERR_OUT_REQUIRED;

    if (!hctx->decInst)
    {
        PIC_MSG("%s - decInst NULL, H264 decoder not inited or aborted!\n", __func__);
        return VC8000_ERR_DEC_INIT;
    }

    in_remain = in_size;
    in_offset = 0;

    do
    {

        hctx->decInput.pStream = nc_ptr(in + in_offset);
        hctx->decInput.streamBusAddress = ptr_to_u32(in + in_offset);
        hctx->decInput.dataLen = in_remain;
        hctx->decInput.picId = hctx->picDecodeNumber;

        if ((hctx->header_parsed == true) && inst->pp_changed)
        {
            //PIC_MSG("run-time pp changed.\n");
            inst->pp_changed = 0;
            h264_pp_out_config(inst, hctx);
            PPSetConfig(hctx->ppInst, &hctx->ppConfig);
        }

        ret = H264DecDecode(hctx->decInst, &hctx->decInput, &hctx->decOutput);
        //printDecodeReturn(hctx, ret);
        error = 0;
        switch (ret)
        {
        case H264DEC_HDRS_RDY:
        {
            H264DecRet decRet = H264DecGetInfo(hctx->decInst, &hctx->decInfo);
            if (decRet == H264DEC_OK)
            {
                PIC_MSG("Width %d Height %d\n", hctx->decInfo.picWidth, hctx->decInfo.picHeight);
                PIC_MSG("Cropping params: (%d, %d) %dx%d\n",
                        hctx->decInfo.cropParams.cropLeftOffset,
                        hctx->decInfo.cropParams.cropTopOffset,
                        hctx->decInfo.cropParams.cropOutWidth,
                        hctx->decInfo.cropParams.cropOutHeight);
                PIC_MSG("MonoChrome = %d\n", hctx->decInfo.monoChrome);
                PIC_MSG("Interlaced = %d\n", hctx->decInfo.interlacedSequence);
                PIC_MSG("DPB mode   = %d\n", hctx->decInfo.dpbMode);
                PIC_MSG("Pictures in DPB = %d\n", hctx->decInfo.picBuffSize);
                PIC_MSG("Pictures in Multibuffer PP = %d\n", hctx->decInfo.multiBuffPpSize);

                switch (hctx->decInfo.outputFormat)
                {
                case H264DEC_TILED_YUV420:
                {
                    PIC_MSG("Output format = H264DEC_TILED_YUV420\n");
                }
                break;
                case H264DEC_YUV400:
                {
                    PIC_MSG("Output format = H264DEC_YUV400\n");
                }
                break;
                case H264DEC_SEMIPLANAR_YUV420:
                {
                    PIC_MSG("Output format = H264DEC_SEMIPLANAR_YUV420\n");
                }
                break;
                default:
                    break;
                }

                if (inst->enable_pp)
                {
                    PIC_MSG("Init PP %d x %d ==> PP output %d x %d\n",
                            hctx->decInfo.picWidth,
                            hctx->decInfo.picHeight,
                            inst->pp_ctx.img_out_w,
                            inst->pp_ctx.img_out_h);

#if 1
                    // PP to fit frame buffer size.
                    if (inst->pp_ctx.img_out_w > hctx->decInfo.picWidth)
                    {
                        inst->pp_ctx.img_out_w = hctx->decInfo.picWidth;

                        if (inst->pp_ctx.frame_buf_w > hctx->decInfo.picWidth)
                        {
                            inst->pp_ctx.img_out_x = (inst->pp_ctx.frame_buf_w - hctx->decInfo.picWidth) / 2;
                        }
                    }

                    if (inst->pp_ctx.img_out_h > hctx->decInfo.picHeight)
                    {
                        inst->pp_ctx.img_out_h = hctx->decInfo.picHeight;

                        if (inst->pp_ctx.frame_buf_h > hctx->decInfo.picHeight)
                        {
                            inst->pp_ctx.img_out_y = (inst->pp_ctx.frame_buf_h - hctx->decInfo.picHeight) / 2;
                        }
                    }
#endif
                    inst->pp_changed = 0;
                    ret = h264_pp_init(inst, hctx);
                    if (ret != 0)
                    {
                        PIC_MSG("%s %d - h264_pp_init failed! 0x%x\n", __func__, __LINE__, ret);
                        ret = VC8000_ERR_PP_INIT;
                        goto exit_ma35d1_h264_dec_run;
                    }
                    h264_pp_in_config(inst, hctx);
                    ret = PPSetConfig(hctx->ppInst, &hctx->ppConfig);
                    if (ret != PP_OK)
                    {
                        PIC_MSG("%s %d - PPSetConfig failed! %d\n", __func__, __LINE__, ret);
                        ret = VC8000_ERR_PP_INIT;
                        goto exit_ma35d1_h264_dec_run;
                    }
                }
                else
                {
                    PIC_MSG("PP is not enabled!!\n");
                }

                hctx->header_parsed = true;

            }
            else
            {
                hctx->header_parsed = false;

                PIC_MSG("Failed to H264DecGetInfo.\n");
            }
        }
        break;

        case H264DEC_PIC_DECODED:
        {
            H264DecRet decret;
            PPResult ppret;

            hctx->picDecodeNumber++;

            /* if output in display order is preferred, the decoder shall be forced
             * to output pictures remaining in decoded picture buffer. Use function
             * H264DecNextPicture() to obtain next picture in display order. Function
             * is called until no more images are ready for display. Second parameter
             * for the function is set to '1' to indicate that this is end of the
             * stream and all pictures shall be output
             */
            do
            {
                uint32_t next_buf, next_buf_ch;
                next_buf = inst->pp_buffers.ppOutputBuffers[((hctx->picDisplayNumber % inst->pp_buffers.nbrOfBuffers))].bufferBusAddr;
                next_buf_ch = inst->pp_buffers.ppOutputBuffers[((hctx->picDisplayNumber % inst->pp_buffers.nbrOfBuffers))].bufferChromaBusAddr;

                if (inst->enable_pp)
                {

                    /* Switch new farm. */
                    hctx->ppConfig.ppOutImg.bufferBusAddr = next_buf;
                    hctx->ppConfig.ppOutImg.bufferChromaBusAddr = next_buf_ch;

                    /* Refresh */
                    PPSetConfig(hctx->ppInst, &hctx->ppConfig);
                }

                if (((decret = H264DecNextPicture(hctx->decInst, &hctx->decPicture, hctx->eos)) == H264DEC_PIC_RDY))
                {

                    if (hctx->decPicture.nbrOfErrMBs)
                        PIC_MSG("concealed %d macroblocks\n", hctx->decPicture.nbrOfErrMBs);

                    //printH264PicCodingType(hctx, hctx->decPicture.picCodingType);
                    PIC_MSG("decoded buffer= %08x view=%d pic=%d\n",
                            nc_ptr(hctx->decPicture.pOutputPicture),
                            hctx->decPicture.viewId,
                            hctx->decPicture.picId);

                    if (inst->pic_flush)
                    {
                        VDE_FLUSH_CXT sFlushCxt = {0};

                        if (inst->enable_pp)
                        {
                            uint32_t u32FarmSize = 0;

                            // Flush PPed frame buffer.
                            switch (inst->pp_ctx.img_out_fmt)
                            {
                            case VC8000_PP_F_RGB888:
                                u32FarmSize = pp->frame_buf_w * pp->frame_buf_h * 4;
                                break;

                            case VC8000_PP_F_RGB565:
                            case VC8000_PP_F_YUV422:
                                u32FarmSize = pp->frame_buf_w * pp->frame_buf_h * 2;
                                break;

                            case VC8000_PP_F_NV12:
                                u32FarmSize = pp->frame_buf_w * pp->frame_buf_h * 3 / 2;
                                break;
                            }

                            sFlushCxt.u32ImgWidth = pp->frame_buf_w;
                            sFlushCxt.u32ImgHeight = pp->frame_buf_h;
                            sFlushCxt.u32FrameBufAddr = (uint32_t)ptr_s(next_buf);
                            sFlushCxt.u32FrameBufSize = u32FarmSize;
                        }
                        else
                        {
                            // Flush decoded frame buffer.
                            sFlushCxt.u32ImgWidth = hctx->decPicture.picWidth ;
                            sFlushCxt.u32ImgHeight = hctx->decPicture.picHeight ;
                            sFlushCxt.u32FrameBufAddr = (uint32_t)nc_ptr(hctx->decPicture.pOutputPicture);
                            sFlushCxt.u32FrameBufSize = hctx->decInfo.picWidth * hctx->decInfo.picHeight * 3 / 2;
                        }
                        inst->pic_flush(&sFlushCxt) ;
                    }
                    else if (!out)
                    {
                        /* Will miss good pictures, just only last frame will be copied. */
                        uint32_t sizeimage = hctx->decInfo.picWidth * hctx->decInfo.picHeight * 3 / 2;
                        memcpy(nc_ptr(out), nc_ptr(hctx->decPicture.pOutputPicture), sizeimage);
                    }

                    /* Increment display number for every displayed picture */
                    hctx->picDisplayNumber++;
                }
            }
            while (decret == H264DEC_PIC_RDY);
        }
        break;

        case H264DEC_BUF_EMPTY:
        case H264DEC_NONREF_PIC_SKIPPED:
        case H264DEC_STRM_ERROR:
            PIC_MSG("ERROR\n");
            error = 1;
            break;

        case H264DEC_STRM_PROCESSED:
        case H264DEC_PENDING_FLUSH:
        case H264DEC_OK:
            /*  Used to indicate that picture decoding needs to finalized prior to corrupting next picture
             * picRdy = 0;
             */
            break;

        case H264DEC_HW_TIMEOUT:
            PIC_MSG("H264 HW Timeout\n");
            break;

        default:
            PIC_MSG(" %s - unhandled decode return code 0x%x !\n", __func__, ret);
            ret = VC8000_ERR_DECODE;
            goto exit_ma35d1_h264_dec_run;
        }

        if (error)
        {
            ret = VC8000_ERR_DECODE;
            goto exit_ma35d1_h264_dec_run;
        }
        //PIC_MSG("DataLeft: %d\n", hctx->decOutput.dataLeft);

        PIC_MSG("Decoding:%d, Display:%d,    %ld / %ld\n",
                hctx->picDecodeNumber,
                hctx->picDisplayNumber,
                in_offset,
                in_remain);

        in_remain = hctx->decOutput.dataLeft;
        in_offset = in_size - in_remain;
    }
    while ((ret != H264DEC_STRM_PROCESSED) && (in_remain > 1));

    ret = 0;

exit_ma35d1_h264_dec_run:

    if (remain)
    {
        *remain = hctx->decOutput.dataLeft;
    }

    return ret;
}

/*------------------------------------------------------------------------------

    Function name:  bsdDecodeReturn

    Purpose: Print out decoder return value

------------------------------------------------------------------------------*/
static void printDecodeReturn(struct h264_ctx *hctx, i32 retval)
{
    PIC_MSG(" >>> H264DecDecode returned: ");
    switch (retval)
    {
    case H264DEC_OK:
        PIC_MSG("H264DEC_OK\n");
        break;
    case H264DEC_NONREF_PIC_SKIPPED:
        PIC_MSG("H264DEC_NONREF_PIC_SKIPPED\n");
        break;
    case H264DEC_STRM_PROCESSED:
        PIC_MSG("H264DEC_STRM_PROCESSED\n");
        break;
    case H264DEC_BUF_EMPTY:
        PIC_MSG("H264DEC_BUF_EMPTY\n");
        break;
    case H264DEC_PIC_RDY:
        PIC_MSG("H264DEC_PIC_RDY\n");
        break;
    case H264DEC_PIC_DECODED:
        PIC_MSG("H264DEC_PIC_DECODED\n");
        break;
    case H264DEC_ADVANCED_TOOLS:
        PIC_MSG("H264DEC_ADVANCED_TOOLS\n");
        break;
    case H264DEC_HDRS_RDY:
        PIC_MSG("H264DEC_HDRS_RDY\n");
        break;
    case H264DEC_STREAM_NOT_SUPPORTED:
        PIC_MSG("H264DEC_STREAM_NOT_SUPPORTED\n");
        break;
    case H264DEC_DWL_ERROR:
        PIC_MSG("H264DEC_DWL_ERROR\n");
        break;
    case H264DEC_HW_TIMEOUT:
        PIC_MSG("H264DEC_HW_TIMEOUT\n");
        break;
    case H264DEC_PENDING_FLUSH:
        PIC_MSG("H264DEC_PENDING_FLUSH\n");
        break;
    default:
        PIC_MSG("Other %d\n", retval);
        break;
    }
}

static void printH264PicCodingType(struct h264_ctx *hctx, u32 *picType)
{
    PIC_MSG("Coding type ");
    switch (picType[0])
    {
    case DEC_PIC_TYPE_I:
        PIC_MSG("[I:");
        break;
    case DEC_PIC_TYPE_P:
        PIC_MSG("[P:");
        break;
    case DEC_PIC_TYPE_B:
        PIC_MSG("[B:");
        break;
    default:
        PIC_MSG("[Other %d:", picType[0]);
        break;
    }

    switch (picType[1])
    {
    case DEC_PIC_TYPE_I:
        PIC_MSG("I]");
        break;
    case DEC_PIC_TYPE_P:
        PIC_MSG("P]");
        break;
    case DEC_PIC_TYPE_B:
        PIC_MSG("B]");
        break;
    default:
        PIC_MSG("Other %d]", picType[1]);
        break;
    }

    PIC_MSG("\n");
}

static int h264_pp_init(struct vc8000_inst *inst, struct h264_ctx *hctx)
{
    int   ret;

    if (!hctx->decInst)
    {
        PIC_MSG("%s - H264 not inited!\n", __func__);
        return -1;
    }

    //for (i = 60; i <= 100; i++)
    //  vc8k_write_swreg(0, i);

    ret = PPInit(&hctx->ppInst);
    if (ret != PP_OK)
    {
        PIC_MSG("%s - failed to create PP\n", __func__);
        return -1;
    }

    ret = PPDecCombinedModeEnable(hctx->ppInst, hctx->decInst, PP_PIPELINED_DEC_TYPE_H264);
    if (ret != PP_OK)
    {
        PIC_MSG("%s - failed to enable combined mode\n", __func__);
        goto cleanup_pp;
    }

    // get the current default PP config
    memset(&hctx->ppConfig, 0, sizeof(hctx->ppConfig));
    ret = PPGetConfig(hctx->ppInst, &hctx->ppConfig);
    if (ret != PP_OK)
    {
        PIC_MSG("%s - failed to get default PP config\n", __func__);
        goto cleanup_combined;
    }

//  ret = h264_pp_out_config(inst, hctx);
//  if (ret != 0)
//      goto cleanup_combined;

    return 0;

cleanup_combined:
    PPDecCombinedModeDisable(hctx->ppInst, hctx->decInst);

cleanup_pp:
    PPRelease(hctx->ppInst);
    return ret;
}

static int h264_pp_exit(struct vc8000_inst *inst, struct h264_ctx *hctx)
{
    PPDecCombinedModeDisable(hctx->ppInst, hctx->decInst);
    PPRelease(hctx->ppInst);
    return 0;
}

static int h264_pp_out_config(struct vc8000_inst *inst, struct h264_ctx *hctx)
{
    int i;
    uint32_t u32FarmSize, u32FarmPieces;

    struct pp_params *pp = &(inst->pp_ctx);

    hctx->ppConfig.ppInRotation.rotation = pp->rotation;
    hctx->ppConfig.ppOutImg.width = pp->img_out_w;
    hctx->ppConfig.ppOutImg.height = pp->img_out_h;

    if ((pp->img_out_x != 0) || (pp->img_out_y != 0) ||
            (pp->img_out_w != pp->frame_buf_w) || (pp->img_out_h != pp->frame_buf_h))
    {
        hctx->ppConfig.ppOutFrmBuffer.enable = 1;
        hctx->ppConfig.ppOutFrmBuffer.writeOriginX = pp->img_out_x;
        hctx->ppConfig.ppOutFrmBuffer.writeOriginY = pp->img_out_y;
        hctx->ppConfig.ppOutFrmBuffer.frameBufferWidth = pp->frame_buf_w;
        hctx->ppConfig.ppOutFrmBuffer.frameBufferHeight = pp->frame_buf_h;
    }

    if (pp->img_out_fmt == VC8000_PP_F_NV12)
    {
        hctx->ppConfig.ppOutImg.pixFormat = PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;
    }
    else
    {
        /*
         * PP output RGB format
         */
        hctx->ppConfig.ppOutRgb.rgbTransform = PP_YCBCR2RGB_TRANSFORM_CUSTOM;
        hctx->ppConfig.ppOutRgb.alpha = 0xFF;
        hctx->ppConfig.ppOutRgb.rgbTransformCoeffs.a = 298;
        hctx->ppConfig.ppOutRgb.rgbTransformCoeffs.b = 409;
        hctx->ppConfig.ppOutRgb.rgbTransformCoeffs.c = 208;
        hctx->ppConfig.ppOutRgb.rgbTransformCoeffs.d = 100;
        hctx->ppConfig.ppOutRgb.rgbTransformCoeffs.e = 516;
        hctx->ppConfig.ppOutRgb.ditheringEnable = 1;

        if (pp->img_out_fmt == VC8000_PP_F_RGB565)
        {
            hctx->ppConfig.ppOutImg.pixFormat = PP_PIX_FMT_BGR16_5_6_5;
        }
        else
        {
            /* should be RGB888, no need to check */
            hctx->ppConfig.ppOutImg.pixFormat  = PP_PIX_FMT_RGB32;
        }
    }

    if ((pp->img_out_fmt == VC8000_PP_F_RGB888))
    {
        u32FarmSize = pp->frame_buf_w * pp->frame_buf_h * 4;
    }
    else
    {
        // VC8000_PP_F_RGB565
        // VC8000_PP_F_NV12
        // VC8000_PP_F_YUV422
        u32FarmSize = pp->frame_buf_w * pp->frame_buf_h * 2;
    }

    u32FarmPieces = pp->pp_out_dst_bufsize / u32FarmSize;
    inst->pp_buffers.nbrOfBuffers = u32FarmPieces;

    for (i = 0; i < inst->pp_buffers.nbrOfBuffers; i++)
    {
        inst->pp_buffers.ppOutputBuffers[i].bufferBusAddr = pp->pp_out_dst + i * u32FarmSize;

        if (pp->img_out_fmt == VC8000_PP_F_NV12)
        {
            inst->pp_buffers.ppOutputBuffers[i].bufferChromaBusAddr = inst->pp_buffers.ppOutputBuffers[i].bufferBusAddr + pp->frame_buf_w * pp->frame_buf_h;
        }
        else
        {
            inst->pp_buffers.ppOutputBuffers[i].bufferChromaBusAddr = 0;
        }

    }

    hctx->ppConfig.ppOutImg.bufferBusAddr = inst->pp_buffers.ppOutputBuffers[0].bufferBusAddr;
    hctx->ppConfig.ppOutImg.bufferChromaBusAddr = inst->pp_buffers.ppOutputBuffers[0].bufferChromaBusAddr;

    return 0;
}

static void h264_pp_in_config(struct vc8000_inst *inst, struct h264_ctx *hctx)
{
    h264_pp_out_config(inst, hctx);

    hctx->ppConfig.ppInCrop.enable = 0;   /* crop is not supported in current release */
    hctx->ppConfig.ppInImg.videoRange = 1;
    hctx->ppConfig.ppInImg.width = hctx->decInfo.picWidth;
    hctx->ppConfig.ppInImg.height = hctx->decInfo.picHeight;
    hctx->ppConfig.ppInImg.pixFormat = PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;
}

