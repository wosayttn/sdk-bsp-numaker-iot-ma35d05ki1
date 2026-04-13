/*************************************************************************//**
 * @file     vc8000_jpeg.c
 * @brief    Perform JPEG bit stream decoding and provide interface for
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
#include "regdrv.h"
#include "jpegdecapi.h"
#include "dwl.h"
#include "jpegdeccontainer.h"
#include "ppinternal.h"
#include "vc8000_glue.h"
#include "vc8000_lib.h"

//#define DBG_MSG      sysprintf
#define DBG_MSG(...)

static JpegDecInst    jpegInst;
static JpegDecInput   jpegIn;
static JpegDecOutput  jpegOut;

static uint32_t  streamTotalLen;
static uint32_t  streamInFile;
static uint32_t  streamSeekLen;
static uint32_t  imageInfoLength;

static uint32_t  thumbInStream = 0;
static uint32_t  onlyFullResolution = 1;
static uint32_t  mode = 0;    /* 1: THUMBNAIL; 0: FULL RESOLUTION */
static uint32_t  amountOfMCUs = 0;
static uint32_t  mcuInRow = 0;
static uint32_t  mcuSizeDivider = 0;
static uint32_t  progressive = 0;
static uint32_t  nonInterleaved = 0;
static uint32_t  ThumbDone = 0;
static uint32_t  slicedOutputUsed = 0;
static int       fullSliceCounter;
static uint32_t  frameReady = 0;
static uint32_t  sizeLuma = 0;
static uint32_t  sizeChroma = 0;
static uint32_t  sliceToUser = 0;
static uint32_t  sliceSize = 0;
static uint32_t  nbrOfImagesToOut = 0;
static uint32_t  scanCounter = 0;
//static uint32_t  planarOutput = 0;
static JpegDecLinearMem outputAddressY;
static JpegDecLinearMem outputAddressCbCr;
static u8   *OutImagePtr_Y, *OutImagePtr_U, *OutImagePtr_V;
static int  WriteOutImageBytesCnt;

void  dump_buff_hex(uint8_t *pucBuff, int nBytes);

/*
 *  For monitoring frame rate
 */
static uint64_t  _fps_check_jiffy;
static int       _decode_cnt, _collect_cnt, _report_times;

static PPInst    ppInst = NULL;
static PPConfig  ppConfig;

static int jpeg_pp_init(struct vc8000_inst *inst, struct jpeg_ctx *jctx);
static int jpeg_pp_exit(struct vc8000_inst *inst, struct jpeg_ctx *jctx);
static int jpeg_pp_in_config(struct vc8000_inst *inst, struct jpeg_ctx *jctx, JpegDecImageInfo *imageInfo);
static int jpeg_pp_out_config(struct vc8000_inst *inst, struct jpeg_ctx *jctx);
static void WriteOutput(u8 *dataLuma, uint32_t picSizeLuma, u8 *dataChroma, uint32_t picSizeChroma, uint32_t picMode);
//static void WriteFullOutput(uint32_t picMode);
static void WriteProgressiveOutput(uint32_t sizeLuma, uint32_t sizeChroma, uint32_t mode, u8 *dataLuma, u8 *dataCb, u8 *dataCr);
static uint32_t FindImageInfoEnd(u8 *pStream, uint32_t streamLength, uint32_t *pOffset);
static void calcSize(JpegDecImageInfo *imageInfo, uint32_t picMode);
static void handleSlicedOutput(struct vc8000_inst *inst, struct jpeg_ctx *jctx, JpegDecImageInfo *imageInfo, JpegDecInput *jpegIn, JpegDecOutput *jpegOut);
static void PrintJpegRet(struct vc8000_inst *inst, struct jpeg_ctx *jctx, JpegDecRet *pJpegRet);
static void PrintGetImageInfo(struct vc8000_inst *inst, struct jpeg_ctx *jctx, JpegDecImageInfo *imageInfo);

int vc8000_jpeg_dec_init(struct vc8000_inst *inst, struct jpeg_ctx *jctx)
{
    JpegDecRet  ret;

    /* Initialize variables */
    thumbInStream = 0;
    onlyFullResolution = 1;
    mode = 0;
    amountOfMCUs = 0;
    mcuInRow = 0;
    mcuSizeDivider = 0;
    progressive = 0;
    nonInterleaved = 0;
    ThumbDone = 0;
    slicedOutputUsed = 0;
    fullSliceCounter = -1;
    frameReady = 0;
    sizeLuma = 0;
    sizeChroma = 0;
    sliceToUser = 0;
    nbrOfImagesToOut = 0;
    scanCounter = 0;

    memset(&jpegInst, 0, sizeof(jpegInst));
    memset(&jpegIn, 0, sizeof(jpegIn));
    memset(&jpegOut, 0, sizeof(jpegOut));

    ret = JpegDecInit(&jpegInst);
    if (ret != JPEGDEC_OK)
    {
        /* Handle here the error situation */
        // PrintJpegRet(inst, jctx, &ret);
        return -1;
    }

    /* NOTE: The registers should not be used outside decoder SW for other
     * than compile time setting test purposes */
    SetDecRegister(((JpegDecContainer *) jpegInst)->jpegRegs, HWIF_DEC_LATENCY, DEC_X170_LATENCY_COMPENSATION);
    SetDecRegister(((JpegDecContainer *) jpegInst)->jpegRegs, HWIF_DEC_CLK_GATE_E, DEC_X170_INTERNAL_CLOCK_GATING);
    SetDecRegister(((JpegDecContainer *) jpegInst)->jpegRegs, HWIF_DEC_OUT_ENDIAN, DEC_X170_OUTPUT_PICTURE_ENDIAN);
    SetDecRegister(((JpegDecContainer *) jpegInst)->jpegRegs, HWIF_DEC_MAX_BURST, DEC_X170_BUS_BURST_LENGTH);
    SetDecRegister(((JpegDecContainer *) jpegInst)->jpegRegs, HWIF_SERV_MERGE_DIS, DEC_X170_SERVICE_MERGE_DISABLE);

    _fps_check_jiffy = jiffies;
    _report_times = 0;
    _collect_cnt = 0;
    _decode_cnt = 0;
    return 0;
}

void ma35d1_jpeg_dec_exit(struct vc8000_inst *inst, struct jpeg_ctx *jctx)
{
    if (inst->enable_pp)
        jpeg_pp_exit(inst, jctx);
    JpegDecRelease(jpegInst);
}

int ma35d1_jpeg_dec_run(struct vc8000_inst *inst, struct jpeg_ctx *jctx, uint8_t *in, uint32_t in_size, uint8_t *out)
{
    uint8_t   *byteStrmStart;
    JpegDecImageInfo imageInfo;
    uint32_t  data_len, len;
    int ret;

    byteStrmStart = nc_ptr(in);
    data_len = len = in_size;

    /*-----------------------------------------------------------------*/
    /*  PHASE 2: OPEN/READ FILE                                        */
    /*-----------------------------------------------------------------*/

    jpegIn.bufferSize = 0;
    streamTotalLen = len;
    streamInFile = streamTotalLen;
    streamSeekLen = 0;

    /* initialize JpegDecDecode input structure */
    jpegIn.streamBuffer.busAddress = addr_s(in);
    jpegIn.streamBuffer.pVirtualAddress = (u32 *)byteStrmStart;
    jpegIn.streamLength = len;

    DBG_MSG("%s - jpeg len = %d, 0x%x 0x%x 0x%x 0x%x\n", __func__, len, in[0], in[1], in[in_size - 2], in[in_size - 1]);

    /*-----------------------------------------------------------------*/
    /*  PHASE 3: GET IMAGE INFO                                        */
    /*-----------------------------------------------------------------*/
    ret = FindImageInfoEnd(byteStrmStart, len, &imageInfoLength);
    if (ret != 0)
    {
        DBG_MSG("%s - FindImageInfoEnd failed! (%d)\n", __func__, ret);
        return ret;
    }

    /* Get image information of the JFIF and decode JFIF header */
    ret = JpegDecGetImageInfo(jpegInst, &jpegIn, &imageInfo);
    if (ret != JPEGDEC_OK)
    {
        DBG_MSG("%s - JpegDecGetImageInfo failed! (%d)\n", __func__, ret);
        return ret;
    }

    PrintGetImageInfo(inst, jctx, &imageInfo);

    if (inst->enable_pp)
    {
        if (inst->pp_changed)
        {
            ret = jpeg_pp_init(inst, jctx);
            if (ret != 0)
            {
                DBG_MSG("%s - jpeg_pp_init failed! (%d)\n", __func__, ret);
                return ret;
            }
            jpeg_pp_in_config(inst, jctx, &imageInfo);
            inst->pp_changed = 0;
        }
    }
    else
    {
        /* YUV420_SP, NV12 */
        OutImagePtr_Y = nc_ptr(out);
        OutImagePtr_U = OutImagePtr_Y + imageInfo.outputWidth * imageInfo.outputHeight;
        OutImagePtr_V = OutImagePtr_U + imageInfo.outputWidth * imageInfo.outputHeight / 2;

        //rt_kprintf("Y: %08x, U: %08x, V:%08x\n", OutImagePtr_Y, OutImagePtr_U, OutImagePtr_V);
        //rt_kprintf("dispW: %d, dispH: %d\n", imageInfo.displayWidth, imageInfo.displayHeight);
        jpegIn.pictureBufferY.pVirtualAddress = (u32 *)OutImagePtr_Y;
        jpegIn.pictureBufferY.busAddress = (g1_addr_t)ptr_s(OutImagePtr_Y);
        jpegIn.pictureBufferCbCr.pVirtualAddress = (u32 *)OutImagePtr_U;
        jpegIn.pictureBufferCbCr.busAddress = (g1_addr_t)ptr_s(OutImagePtr_U);
        jpegIn.pictureBufferCr.pVirtualAddress = (u32 *)OutImagePtr_V;
        jpegIn.pictureBufferCr.busAddress = (g1_addr_t)ptr_s(OutImagePtr_V);

        WriteOutImageBytesCnt = 0;
    }

    /*  ******************** THUMBNAIL **************************** */
    /* Select if Thumbnail or full resolution image will be decoded */
    if (imageInfo.thumbnailType == JPEGDEC_THUMBNAIL_JPEG)
    {
        /* if all thumbnails processed (MJPEG) */
        if (!ThumbDone)
            jpegIn.decImageType = JPEGDEC_THUMBNAIL;
        else
            jpegIn.decImageType = JPEGDEC_IMAGE;

        thumbInStream = 1;
    }
    else if (imageInfo.thumbnailType == JPEGDEC_NO_THUMBNAIL)
        jpegIn.decImageType = JPEGDEC_IMAGE;
    else if (imageInfo.thumbnailType == JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT)
        jpegIn.decImageType = JPEGDEC_IMAGE;

    /* check if forced to decode only full resolution images  ==> discard thumbnail */
    if (onlyFullResolution)
    {
        /* decode only full resolution image */
        // DBG_MSG("\n\tNOTE! FORCED BY USER TO DECODE ONLY FULL RESOLUTION IMAGE\n");
        jpegIn.decImageType = JPEGDEC_IMAGE;
    }

    DBG_MSG("PHASE 3: GET IMAGE INFO successful\n");

    /*-----------------------------------------------------------------*/
    /*  Decode FRAME                           */
    /*-----------------------------------------------------------------*/
    /* TB SPECIFIC == LOOP IF THUMBNAIL IN JFIF */
    /* Decode JFIF */
    if (jpegIn.decImageType == JPEGDEC_THUMBNAIL)
        mode = 1; /* TODO KIMA */
    else
        mode = 0;

    /* no slice mode supported in progressive || non-interleaved ==> force to full mode */
    if ((jpegIn.decImageType == JPEGDEC_THUMBNAIL &&
            imageInfo.codingModeThumb == JPEGDEC_PROGRESSIVE) ||
            (jpegIn.decImageType == JPEGDEC_IMAGE &&
             imageInfo.codingMode == JPEGDEC_PROGRESSIVE))
        jpegIn.sliceMbSet = 0;

    /******** PHASE 4 ********/
    /* Image mode to decode */
    //if (mode)
    //  DBG_MSG("\nPHASE 4: DECODE FRAME: THUMBNAIL\n");
    //else
    //  DBG_MSG("\nPHASE 4: DECODE FRAME: FULL RESOLUTION\n");

    /* if input (only full, not tn) > 4096 MCU  */
    /* ==> force to slice mode                  */
    if (mode == 0)
    {
        DBG_MSG(" outputFormat = 0x%x, outputWidth=%d, outputHeight=%d\n", imageInfo.outputFormat, imageInfo.outputWidth, imageInfo.outputHeight);

        /* calculate MCU's */
        if (imageInfo.outputFormat == JPEGDEC_YCbCr400 ||
                imageInfo.outputFormat == JPEGDEC_YCbCr444_SEMIPLANAR)
        {
            amountOfMCUs = ((imageInfo.outputWidth * imageInfo.outputHeight) / 64);
            mcuInRow = (imageInfo.outputWidth / 8);
        }
        else if (imageInfo.outputFormat == JPEGDEC_YCbCr420_SEMIPLANAR)
        {
            /* 265 is the amount of luma samples in MB for 4:2:0 */
            amountOfMCUs = ((imageInfo.outputWidth * imageInfo.outputHeight) / 256);
            mcuInRow = (imageInfo.outputWidth / 16);
        }
        else if (imageInfo.outputFormat == JPEGDEC_YCbCr422_SEMIPLANAR)
        {
            /* 128 is the amount of luma samples in MB for 4:2:2 */
            amountOfMCUs = ((imageInfo.outputWidth * imageInfo.outputHeight) / 128);
            mcuInRow = (imageInfo.outputWidth / 16);
        }
        else if (imageInfo.outputFormat == JPEGDEC_YCbCr440)
        {
            /* 128 is the amount of luma samples in MB for 4:4:0 */
            amountOfMCUs = ((imageInfo.outputWidth * imageInfo.outputHeight) / 128);
            mcuInRow = (imageInfo.outputWidth / 8);
        }
        else if (imageInfo.outputFormat == JPEGDEC_YCbCr411_SEMIPLANAR)
        {
            amountOfMCUs = ((imageInfo.outputWidth * imageInfo.outputHeight) / 256);
            mcuInRow = (imageInfo.outputWidth / 32);
        }

        /* set mcuSizeDivider for slice size count */
        if (imageInfo.outputFormat == JPEGDEC_YCbCr400 ||
                imageInfo.outputFormat == JPEGDEC_YCbCr440 ||
                imageInfo.outputFormat == JPEGDEC_YCbCr444_SEMIPLANAR)
            mcuSizeDivider = 2;
        else
            mcuSizeDivider = 1;

#if 0
        /* over max MCU ==> force to slice mode */
        if ((jpegIn.sliceMbSet == 0) &&
                (amountOfMCUs > JPEGDEC_MAX_SLICE_SIZE))
        {
            do
            {
                jpegIn.sliceMbSet++;
            }
            while (((jpegIn.sliceMbSet * (mcuInRow / mcuSizeDivider)) +
                    (mcuInRow / mcuSizeDivider)) <
                    JPEGDEC_MAX_SLICE_SIZE);
            // DBG_MSG("Force to slice mode ==> Decoder Slice MB Set %d\n", jpegIn.sliceMbSet);
        }
#else
        /* 8190 and over 16M ==> force to slice mode */
        if ((jpegIn.sliceMbSet == 0) &&
                ((imageInfo.outputWidth * imageInfo.outputHeight) >
                 JPEGDEC_MAX_PIXEL_AMOUNT))
        {
            do
            {
                jpegIn.sliceMbSet++;
            }
            while (((jpegIn.sliceMbSet * (mcuInRow / mcuSizeDivider)) +
                    (mcuInRow / mcuSizeDivider)) <
                    JPEGDEC_MAX_SLICE_SIZE_8190);
            // DBG_MSG("Force to slice mode (over 16M) ==> Decoder Slice MB Set %d\n", jpegIn.sliceMbSet);
        }
#endif
    }

    if (jpegIn.sliceMbSet)
    {
        DBG_MSG("%s - jpegIn.sliceMbSet = %d\n", __func__, jpegIn.sliceMbSet);
    }

    /* decode */
    do
    {
        ret = JpegDecDecode(jpegInst, &jpegIn, &jpegOut);
        if (ret == JPEGDEC_FRAME_READY)
        {
            // DBG_MSG("\t-JPEG: JPEGDEC_FRAME_READY\n");

            /* check if progressive ==> planar output */
            if (((imageInfo.codingMode == JPEGDEC_PROGRESSIVE) && (mode == 0)) ||
                    ((imageInfo.codingModeThumb == JPEGDEC_PROGRESSIVE) && (mode == 1)))
            {
                progressive = 1;
            }

            if (((imageInfo.codingMode == JPEGDEC_NONINTERLEAVED) && (mode == 0))
                    || ((imageInfo.codingModeThumb == JPEGDEC_NONINTERLEAVED) && (mode == 1)))
                nonInterleaved = 1;
            else
                nonInterleaved = 0;

            if (jpegIn.sliceMbSet && fullSliceCounter == -1)
                slicedOutputUsed = 1;

            /* info to handleSlicedOutput */
            frameReady = 1;

            if (inst->pic_flush)
            {
                VDE_FLUSH_CXT sFlushCxt = {0};

                if (inst->enable_pp)
                {
                    struct pp_params *pp = &inst->pp_ctx;
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
                    sFlushCxt.u32FrameBufAddr = (uint32_t)ptr_s(OutImagePtr_Y);
                    sFlushCxt.u32FrameBufSize = u32FarmSize;
                }
                else
                {
                    // Flush decoded frame buffer. YUV420SP, NV12
                    sFlushCxt.u32ImgWidth = imageInfo.outputWidth;
                    sFlushCxt.u32ImgHeight = imageInfo.outputHeight;
                    sFlushCxt.u32FrameBufAddr = (uint32_t)ptr_s(OutImagePtr_Y);
                    sFlushCxt.u32FrameBufSize = imageInfo.outputWidth * imageInfo.outputHeight * 3 / 2;
                }

                inst->pic_flush(&sFlushCxt) ;
            }

            if (!mode)
                nbrOfImagesToOut++;
        }
        else if (ret == JPEGDEC_SCAN_PROCESSED)
        {
            /* TODO! Progressive scan ready... */
            DBG_MSG("\t-JPEG: JPEGDEC_SCAN_PROCESSED\n");

            /* progressive ==> planar output */
            if (imageInfo.codingMode == JPEGDEC_PROGRESSIVE)
                progressive = 1;

            /* info to handleSlicedOutput */
            DBG_MSG("SCAN %d READY\n", scanCounter);

            if (imageInfo.codingMode == JPEGDEC_PROGRESSIVE)
            {
                /* calculate size for output */
                calcSize(&imageInfo, mode);
                // DBG_MSG("sizeLuma %d and sizeChroma %d\n", sizeLuma, sizeChroma);
                WriteProgressiveOutput(sizeLuma, sizeChroma, mode,
                                       (u8 *)jpegOut.outputPictureY.
                                       pVirtualAddress,
                                       (u8 *)jpegOut.outputPictureCbCr.
                                       pVirtualAddress,
                                       (u8 *)jpegOut.outputPictureCr.
                                       pVirtualAddress);

                scanCounter++;
            }
            /* update/reset */
            progressive = 0;

        }
        else if (ret == JPEGDEC_SLICE_READY)
        {
            DBG_MSG("\t-JPEG: JPEGDEC_SLICE_READY\n");
            slicedOutputUsed = 1;
            /* calculate/write output of slice
             * and update output budder in case of
             * user allocated memory */
            if (jpegOut.outputPictureY.pVirtualAddress != NULL)
                handleSlicedOutput(inst, jctx, &imageInfo, &jpegIn, &jpegOut);
            scanCounter++;

        }
        else if (ret == JPEGDEC_STRM_PROCESSED)
        {
            DBG_MSG("\t-JPEG: JPEGDEC_STRM_PROCESSED ==> Load input buffer\n");
            DBG_MSG("%s - JPEGDEC_STRM_PROCESSED not a complete JPEG image!\n",   __func__);
            return VC8000_ERR_BIT_STREAM;
        }
        else if (ret == JPEGDEC_STRM_ERROR)
        {
            DBG_MSG("%s %d - JPEGDEC_STRM_ERROR!\n", __func__, __LINE__);

            //strm_error:
            if (jpegIn.sliceMbSet && (fullSliceCounter == -1))
                slicedOutputUsed = 1;

            /* calculate/write output of slice and update output budder in case of user allocated memory */
            if (slicedOutputUsed && (jpegOut.outputPictureY.pVirtualAddress != NULL))
                handleSlicedOutput(inst, jctx, &imageInfo, &jpegIn, &jpegOut);

            /* info to handleSlicedOutput */
            frameReady = 1;
            slicedOutputUsed = 0;

            /* Handle here the error situation */
            PrintJpegRet(inst, jctx, (JpegDecRet *)&ret);
            if (mode == 1)
                break;
            else
                return VC8000_ERR_BIT_STREAM;  // goto error;
        }
        else
        {
            /* Handle here the error situation */
            PrintJpegRet(inst, jctx, (JpegDecRet *)&ret);
            DBG_MSG("%s %d - unhandled ret code 0x%x!\n", __func__, __LINE__,    ret);
            return VC8000_ERR_DECODE;
        }
    }
    while (ret != JPEGDEC_FRAME_READY);

    /* calculate/write output of slice */
    if (slicedOutputUsed && jpegOut.outputPictureY.pVirtualAddress != NULL)
    {
        handleSlicedOutput(inst, jctx, &imageInfo, &jpegIn, &jpegOut);
        slicedOutputUsed = 0;
    }

    if (jpegOut.outputPictureY.pVirtualAddress != NULL)
    {
        /* calculate size for output */
        calcSize(&imageInfo, mode);

        /* Thumbnail || full resolution */
        //if (!mode)
        //  DBG_MSG("\n\t-JPEG: ++++++++++ FULL RESOLUTION ++++++++++\n");
        //else
        //  DBG_MSG("\t-JPEG: ++++++++++ THUMBNAIL ++++++++++\n");
        DBG_MSG("\t-JPEG: Instance %x\n", (JpegDecContainer *) jpegInst);
        DBG_MSG("\t-JPEG: Luma size: %d\n", sizeLuma);
        DBG_MSG("\t-JPEG: Chroma size: %d\n", sizeChroma);
        DBG_MSG("\t-JPEG: Luma output  bus: 0x%x\n", (int)jpegOut.outputPictureY.busAddress);
        DBG_MSG("\t-JPEG: Chroma output bus: 0x%x\n", (int)jpegOut.outputPictureCbCr.busAddress);
    }

    DBG_MSG("PHASE 4: DECODE FRAME successful\n");

    _decode_cnt++;
    if (jiffies - _fps_check_jiffy >= 1000)
    {
        _report_times++;
        _collect_cnt += _decode_cnt;
        if (_report_times == 10)
        {
            DBG_MSG("FPS: %d, Average %d.%d\n", _decode_cnt, _collect_cnt / 10, _collect_cnt % 10);
            _report_times = 0;
            _collect_cnt = 0;
        }
        else
        {
            DBG_MSG("FPS: %d\n", _decode_cnt);
        }
        _decode_cnt = 0;
        _fps_check_jiffy = jiffies;
    }

    // if (inst->enable_pp)
    return 0;

    /******** PHASE 5 ********/
    DBG_MSG("\nPHASE 5: WRITE OUTPUT\n");

    if (imageInfo.outputFormat)
    {
        switch (imageInfo.outputFormat)
        {
        case JPEGDEC_YCbCr400:
            DBG_MSG("\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr400\n");
            break;
        case JPEGDEC_YCbCr420_SEMIPLANAR:
            DBG_MSG("\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr420_SEMIPLANAR\n");
            break;
        case JPEGDEC_YCbCr422_SEMIPLANAR:
            DBG_MSG("\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr422_SEMIPLANAR\n");
            break;
        case JPEGDEC_YCbCr440:
            DBG_MSG("\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr440\n");
            break;
        case JPEGDEC_YCbCr411_SEMIPLANAR:
            DBG_MSG("\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr411_SEMIPLANAR\n");
            break;
        case JPEGDEC_YCbCr444_SEMIPLANAR:
            DBG_MSG("\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr444_SEMIPLANAR\n");
            break;
        }
    }
    if (imageInfo.codingMode == JPEGDEC_PROGRESSIVE)
        progressive = 1;

    /* write output */
    if (jpegIn.sliceMbSet)
    {
        if (imageInfo.outputFormat != JPEGDEC_YCbCr400)
            DBG_MSG("To do: WriteFullOutput!!\n");  // WriteFullOutput(mode);
    }
    else
    {
        if (imageInfo.codingMode != JPEGDEC_PROGRESSIVE)
        {
            WriteOutput(((u8 *) jpegOut.outputPictureY.pVirtualAddress),
                        sizeLuma, ((u8 *) jpegOut.outputPictureCbCr.pVirtualAddress), sizeChroma, mode);
        }
        else
        {
            /* calculate size for output */
            calcSize(&imageInfo, mode);

            // DBG_MSG("sizeLuma %d and sizeChroma %d\n", sizeLuma, sizeChroma);

            WriteProgressiveOutput(sizeLuma, sizeChroma, mode,
                                   (u8 *)jpegOut.outputPictureY.pVirtualAddress,
                                   (u8 *)jpegOut.outputPictureCbCr.pVirtualAddress,
                                   (u8 *)jpegOut.outputPictureCr.pVirtualAddress);
        }
    }
#if 0
    if (crop)
        WriteCroppedOutput(&imageInfo,
                           (u8 *)jpegOut.outputPictureY.pVirtualAddress,
                           (u8 *)jpegOut.outputPictureCbCr.pVirtualAddress,
                           (u8 *)jpegOut.outputPictureCr.pVirtualAddress);
#endif
    progressive = 0;

    // DBG_MSG("PHASE 5: WRITE OUTPUT successful. bytes %d written\n", WriteOutImageBytesCnt);
    return 0;
}

/*-----------------------------------------------------------------------------

    Function name:  FindImageInfoEnd

    Purpose:
        Finds 0xFFC4 from the stream and pOffset includes number of bytes to
        this marker. In case of an error returns != 0
        (i.e., the marker not found).

-----------------------------------------------------------------------------*/
static uint32_t FindImageInfoEnd(u8 *pStream, uint32_t streamLength, uint32_t *pOffset)
{
    uint32_t i;

    for (i = 0; i < streamLength; ++i)
    {
        if (0xFF == pStream[i])
        {
            if (((i + 1) < streamLength) && 0xC4 == pStream[i + 1])
            {
                *pOffset = i;
                return 0;
            }
        }
    }
    return -1;
}

/*------------------------------------------------------------------------------

    Function name:  calcSize

    Purpose:
        Calculate size

------------------------------------------------------------------------------*/
void calcSize(JpegDecImageInfo *imageInfo, uint32_t picMode)
{
    uint32_t format;

    sizeLuma = 0;
    sizeChroma = 0;

    format = (picMode == 0) ?
             imageInfo->outputFormat : imageInfo->outputFormatThumb;

    /* if slice interrupt not given to user */
    if (!sliceToUser)
    {
        if (picMode == 0)      /* full */
        {
            sizeLuma = (imageInfo->outputWidth * imageInfo->outputHeight);
        }
        else        /* thumbnail */
        {
            sizeLuma = (imageInfo->outputWidthThumb * imageInfo->outputHeightThumb);
        }
    }
    else
    {
        if (picMode == 0)     /* full */
        {
            sizeLuma = (imageInfo->outputWidth * sliceSize);
        }
        else        /* thumbnail */
        {
            sizeLuma = (imageInfo->outputWidthThumb * sliceSize);
        }
    }

    if (format != JPEGDEC_YCbCr400)
    {
        if ((format == JPEGDEC_YCbCr420_SEMIPLANAR) ||
                (format == JPEGDEC_YCbCr411_SEMIPLANAR))
        {
            sizeChroma = (sizeLuma / 2);
        }
        else if (format == JPEGDEC_YCbCr444_SEMIPLANAR)
        {
            sizeChroma = sizeLuma * 2;
        }
        else
        {
            sizeChroma = sizeLuma;
        }
    }
}

/*------------------------------------------------------------------------------

Function name:  WriteOutput

Purpose:
    Write picture pointed by data to file. Size of the
    picture in pixels is indicated by picSize.

------------------------------------------------------------------------------*/
static void WriteOutput(u8 *dataLuma, uint32_t picSizeLuma, u8 *dataChroma,
                        uint32_t picSizeChroma, uint32_t picMode)
{
    if (picMode == 1)
    {
        return;
    }
    if (!dataLuma || !dataChroma)
    {
        return;
    }

    memcpy(OutImagePtr_Y, dataLuma, picSizeLuma);
    OutImagePtr_Y += picSizeLuma;
    WriteOutImageBytesCnt += picSizeLuma;

    if (!nonInterleaved)
    {
        /* progressive ==> planar */
        if (!progressive)
        {
            int   i;

            for (i = 0; i < picSizeChroma / 2; i++)
            {
                *OutImagePtr_U++ = dataChroma[i * 2];
                *OutImagePtr_V++ = dataChroma[i * 2 + 1];
            }
            WriteOutImageBytesCnt += picSizeChroma;
        }
        else        //  is progressive
        {
            int   i;

            for (i = 0; i < picSizeChroma / 2; i++)
            {
                *OutImagePtr_U++ = dataChroma[i * 2];
                *OutImagePtr_V++ = dataChroma[i * 2 + 1];
            }
            WriteOutImageBytesCnt += picSizeChroma;
        }  // if (!progressive)

    }
    else       // if (!nonInterleaved)
    {
        //for (i = 0; i < picSizeChroma; i++)
        //    f_write(&foutput, pYuvOut + (1 * i), 1, &ff_rw);
        memcpy(OutImagePtr_U, dataChroma, picSizeChroma / 2);
        memcpy(OutImagePtr_V, dataChroma + picSizeChroma / 2, picSizeChroma / 2);
        WriteOutImageBytesCnt += picSizeChroma;
    }
}

/*------------------------------------------------------------------------------

    Function name:  handleSlicedOutput

    Purpose:
        Calculates size for slice and writes sliced output

------------------------------------------------------------------------------*/
void handleSlicedOutput(struct vc8000_inst *inst, struct jpeg_ctx *jctx, JpegDecImageInfo *imageInfo,
                        JpegDecInput *jpegIn, JpegDecOutput *jpegOut)
{
    /* for output name */
    fullSliceCounter++;

    /******** PHASE X ********/
    if (jpegIn->sliceMbSet)
        ; // dev_info(ctx->dev->dev, "\nPHASE SLICE: HANDLE SLICE %d\n", fullSliceCounter);

    /* save start pointers for whole output */
    if (fullSliceCounter == 0)
    {
        /* virtual address */
        outputAddressY.pVirtualAddress = jpegOut->outputPictureY.pVirtualAddress;
        outputAddressCbCr.pVirtualAddress = jpegOut->outputPictureCbCr.pVirtualAddress;

        /* bus address */
        outputAddressY.busAddress = jpegOut->outputPictureY.busAddress;
        outputAddressCbCr.busAddress = jpegOut->outputPictureCbCr.busAddress;
    }

    /* if not PP direct to fbdev, write output to V4L2 buffer */
    if (inst->enable_pp == false)
    {
        /******** PHASE 5 ********/
        // dev_info(ctx->dev->dev, "\nPHASE 5: WRITE OUTPUT\n");

        if (imageInfo->outputFormat)
        {
            if (!frameReady)
            {
                sliceSize = jpegIn->sliceMbSet * 16;
            }
            else
            {
                if (mode == 0)
                    sliceSize = (imageInfo->outputHeight - ((fullSliceCounter) * (sliceSize)));
                else
                    sliceSize = (imageInfo->outputHeightThumb - ((fullSliceCounter) * (sliceSize)));
            }
        }

        /* slice interrupt from decoder */
        sliceToUser = 1;

        /* calculate size for output */
        calcSize(imageInfo, mode);

        DBG_MSG("\t-JPEG: ++++++++++ SLICE INFORMATION ++++++++++\n");
        DBG_MSG("\t-JPEG: Luma output: 0x%llx size: %d\n",
                jpegOut->outputPictureY.pVirtualAddress, sizeLuma);
        DBG_MSG("\t-JPEG: Chroma output: 0x%llx size: %d\n",
                jpegOut->outputPictureCbCr.pVirtualAddress, sizeChroma);
        DBG_MSG("\t-JPEG: Luma output bus: 0x%llx\n",
                (u8 *) jpegOut->outputPictureY.busAddress);
        DBG_MSG("\t-JPEG: Chroma output bus: 0x%llx\n",
                (u8 *) jpegOut->outputPictureCbCr.busAddress);

        /* write slice output */
        WriteOutput(((u8 *) jpegOut->outputPictureY.pVirtualAddress),
                    sizeLuma,
                    ((u8 *) jpegOut->outputPictureCbCr.pVirtualAddress),
                    sizeChroma, mode);
        DBG_MSG("PHASE 5: WRITE OUTPUT successful\n");
    }

    if (frameReady)
    {
        /* give start pointers for whole output write */

        /* virtual address */
        jpegOut->outputPictureY.pVirtualAddress = outputAddressY.pVirtualAddress;
        jpegOut->outputPictureCbCr.pVirtualAddress = outputAddressCbCr.pVirtualAddress;

        /* bus address */
        jpegOut->outputPictureY.busAddress = outputAddressY.busAddress;
        jpegOut->outputPictureCbCr.busAddress = outputAddressCbCr.busAddress;
    }

    if (frameReady)
    {
        frameReady = 0;
        sliceToUser = 0;
        /******** PHASE X ********/
        if (jpegIn->sliceMbSet)
            ; // DBG_MSG("\nPHASE SLICE: HANDLE SLICE %d successful\n", fullSliceCounter);

        fullSliceCounter = -1;
    }
    else
    {
        /******** PHASE X ********/
        if (jpegIn->sliceMbSet)
            ; // DBG_MSG("\nPHASE SLICE: HANDLE SLICE %d successful\n", fullSliceCounter);
    }

}

void WriteProgressiveOutput(uint32_t sizeLuma, uint32_t sizeChroma, uint32_t mode,
                            u8 *dataLuma, u8 *dataCb, u8 *dataCr)
{
    memcpy(OutImagePtr_Y, dataLuma, sizeLuma);
    OutImagePtr_Y += sizeLuma;
    memcpy(OutImagePtr_U,   dataCb, sizeChroma / 2);
    OutImagePtr_U   += sizeChroma / 2;
    memcpy(OutImagePtr_V,   dataCr, sizeChroma / 2);
    OutImagePtr_V   += sizeChroma / 2;
    WriteOutImageBytesCnt += sizeLuma + sizeChroma;
}

/*-----------------------------------------------------------------------------

Print JPEG api return value

-----------------------------------------------------------------------------*/
static void PrintJpegRet(struct vc8000_inst *inst, struct jpeg_ctx *jctx, JpegDecRet *pJpegRet)
{
    switch (*pJpegRet)
    {
    case JPEGDEC_FRAME_READY:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_FRAME_READY\n");
        break;
    case JPEGDEC_OK:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_OK\n");
        break;
    case JPEGDEC_ERROR:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_ERROR\n");
        break;
    case JPEGDEC_DWL_HW_TIMEOUT:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_HW_TIMEOUT\n");
        break;
    case JPEGDEC_UNSUPPORTED:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_UNSUPPORTED\n");
        break;
    case JPEGDEC_PARAM_ERROR:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_PARAM_ERROR\n");
        break;
    case JPEGDEC_MEMFAIL:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_MEMFAIL\n");
        break;
    case JPEGDEC_INITFAIL:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_INITFAIL\n");
        break;
    case JPEGDEC_HW_BUS_ERROR:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_HW_BUS_ERROR\n");
        break;
    case JPEGDEC_SYSTEM_ERROR:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_SYSTEM_ERROR\n");
        break;
    case JPEGDEC_DWL_ERROR:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_DWL_ERROR\n");
        break;
    case JPEGDEC_INVALID_STREAM_LENGTH:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_INVALID_STREAM_LENGTH\n");
        break;
    case JPEGDEC_STRM_ERROR:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_STRM_ERROR\n");
        break;
    case JPEGDEC_INVALID_INPUT_BUFFER_SIZE:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_INVALID_INPUT_BUFFER_SIZE\n");
        break;
    case JPEGDEC_INCREASE_INPUT_BUFFER:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_INCREASE_INPUT_BUFFER\n");
        break;
    case JPEGDEC_SLICE_MODE_UNSUPPORTED:
        DBG_MSG("TB: jpeg API    returned : JPEGDEC_SLICE_MODE_UNSUPPORTED\n");
        break;
    default:
        DBG_MSG("TB: jpeg API    returned unknown status\n");
        break;
    }
}

/*-----------------------------------------------------------------------------

Print JpegDecGetImageInfo values

-----------------------------------------------------------------------------*/
static void PrintGetImageInfo(struct vc8000_inst *inst, struct jpeg_ctx *jctx, JpegDecImageInfo *imageInfo)
{
    /* Select if Thumbnail or full resolution image will be decoded */
    if (imageInfo->thumbnailType == JPEGDEC_THUMBNAIL_JPEG)
    {
        /* decode thumbnail */
        DBG_MSG("\t-JPEG THUMBNAIL IN    STREAM\n");
        DBG_MSG("\t-JPEG THUMBNAIL INFO\n");
        DBG_MSG("\t\t-JPEG thumbnail width: %d\n",
                imageInfo->outputWidthThumb);
        DBG_MSG("\t\t-JPEG thumbnail height: %d\n",
                imageInfo->outputHeightThumb);

        /* stream type */
        switch (imageInfo->codingModeThumb)
        {
        case JPEGDEC_BASELINE:
            DBG_MSG("\t\t-JPEG: STREAM TYPE: JPEGDEC_BASELINE\n");
            break;
        case JPEGDEC_PROGRESSIVE:
            DBG_MSG("\t\t-JPEG: STREAM TYPE: JPEGDEC_PROGRESSIVE\n");
            break;
        case JPEGDEC_NONINTERLEAVED:
            DBG_MSG("\t\t-JPEG: STREAM TYPE: JPEGDEC_NONINTERLEAVED\n");
            break;
        }

        if (imageInfo->outputFormatThumb)
        {
            switch (imageInfo->outputFormatThumb)
            {
            case JPEGDEC_YCbCr400:
                DBG_MSG("\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr400\n");
                break;
            case JPEGDEC_YCbCr420_SEMIPLANAR:
                DBG_MSG("\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr420_SEMIPLANAR\n");
                break;
            case JPEGDEC_YCbCr422_SEMIPLANAR:
                DBG_MSG("\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr422_SEMIPLANAR\n");
                break;
            case JPEGDEC_YCbCr440:
                DBG_MSG("\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr440\n");
                break;
            case JPEGDEC_YCbCr411_SEMIPLANAR:
                DBG_MSG("\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr411_SEMIPLANAR\n");
                break;
            case JPEGDEC_YCbCr444_SEMIPLANAR:
                DBG_MSG("\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr444_SEMIPLANAR\n");
                break;
            }
        }
    }
    else if (imageInfo->thumbnailType == JPEGDEC_NO_THUMBNAIL)
    {
        /* decode full image */
        DBG_MSG("\t-NO THUMBNAIL IN STREAM ==> Decode    full resolution image\n");
    }
    else if (imageInfo->thumbnailType == JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT)
    {
        /* decode full image */
        DBG_MSG("\tNOT SUPPORTED THUMBNAIL IN    STREAM ==> Decode full resolution image\n");
    }

    DBG_MSG("\t-JPEG FULL    RESOLUTION INFO\n");
    DBG_MSG("\t\t-JPEG width: %d\n", imageInfo->outputWidth);
    DBG_MSG("\t\t-JPEG height: %d\n", imageInfo->outputHeight);
    if (imageInfo->outputFormat)
    {
        switch (imageInfo->outputFormat)
        {
        case JPEGDEC_YCbCr400:
            DBG_MSG("\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr400\n");
            break;
        case JPEGDEC_YCbCr420_SEMIPLANAR:
            DBG_MSG("\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr420_SEMIPLANAR\n");
            break;
        case JPEGDEC_YCbCr422_SEMIPLANAR:
            DBG_MSG("\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr422_SEMIPLANAR\n");
            break;
        case JPEGDEC_YCbCr440:
            DBG_MSG("\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr440\n");
            break;
        case JPEGDEC_YCbCr411_SEMIPLANAR:
            DBG_MSG("\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr411_SEMIPLANAR\n");
            break;
        case JPEGDEC_YCbCr444_SEMIPLANAR:
            DBG_MSG("\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr444_SEMIPLANAR\n");
            break;
        }
    }

    /* stream type */
    switch (imageInfo->codingMode)
    {
    case JPEGDEC_BASELINE:
        DBG_MSG("\t\t-JPEG: STREAM TYPE: JPEGDEC_BASELINE\n");
        break;
    case JPEGDEC_PROGRESSIVE:
        DBG_MSG("\t\t-JPEG: STREAM TYPE: JPEGDEC_PROGRESSIVE\n");
        break;
    case JPEGDEC_NONINTERLEAVED:
        DBG_MSG("\t\t-JPEG: STREAM TYPE: JPEGDEC_NONINTERLEAVED\n");
        break;
    }

    if (imageInfo->thumbnailType == JPEGDEC_THUMBNAIL_JPEG)
    {
        DBG_MSG("\t-JPEG ThumbnailType: JPEG\n");
    }
    else if (imageInfo->thumbnailType == JPEGDEC_NO_THUMBNAIL)
        DBG_MSG("\t-JPEG ThumbnailType: NO THUMBNAIL\n");
    else if (imageInfo->thumbnailType == JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT)
        DBG_MSG("\t-JPEG ThumbnailType: NOT SUPPORTED    THUMBNAIL\n");
}

static int jpeg_pp_init(struct vc8000_inst *inst, struct jpeg_ctx *jctx)
{
    int   ret;

    if (!jpegInst)
    {
        DBG_MSG("%s - JPEG not inited!\n", __func__);
        return -1;
    }

    //for (i = 60; i <= 100; i++)
    //  vc8k_write_swreg(0, i);

    ret = PPInit(&jctx->ppInst);
    if (ret != PP_OK)
    {
        DBG_MSG("%s - failed to create PP\n", __func__);
        return -1;
    }

    ret = PPDecCombinedModeEnable(jctx->ppInst, jpegInst, PP_PIPELINED_DEC_TYPE_JPEG);
    if (ret != PP_OK)
    {
        DBG_MSG("%s - failed to enable combined mode\n", __func__);
        goto cleanup_pp;
    }

    // get the current default PP config
    memset(&jctx->ppConfig, 0, sizeof(jctx->ppConfig));
    ret = PPGetConfig(jctx->ppInst, &jctx->ppConfig);
    if (ret != PP_OK)
    {
        DBG_MSG("%s - failed to get default PP config\n", __func__);
        goto cleanup_combined;
    }

    if (inst->enable_pp)
    {
        ret = jpeg_pp_out_config(inst, jctx);
        if (ret != 0)
            goto cleanup_combined;
    }
    return 0;

cleanup_combined:
    PPDecCombinedModeDisable(jctx->ppInst, jpegInst);

cleanup_pp:
    PPRelease(jctx->ppInst);
    return ret;
}

static int jpeg_pp_exit(struct vc8000_inst *inst, struct jpeg_ctx *jctx)
{
    PPDecCombinedModeDisable(jctx->ppInst, jpegInst);
    PPRelease(jctx->ppInst);
    return 0;
}

static int jpeg_pp_out_config(struct vc8000_inst *inst, struct jpeg_ctx *jctx)
{
    struct pp_params *pp = &(inst->pp_ctx);
    PPConfig *ppConfig = &jctx->ppConfig;

    //For pp output to frame buffer(ultrafb/overlay)
    ppConfig->ppInRotation.rotation = pp->rotation;

    //Wayne if ((pp->pp_out_dst == 0) || (pp->pp_out_dst == 1))
    {
        if ((pp->img_out_x != 0) || (pp->img_out_y != 0) ||
                (pp->img_out_w != pp->frame_buf_w) || (pp->img_out_h != pp->frame_buf_h))
        {
            ppConfig->ppOutFrmBuffer.enable = 1;
            ppConfig->ppOutFrmBuffer.writeOriginX = pp->img_out_x;
            ppConfig->ppOutFrmBuffer.writeOriginY = pp->img_out_y;
            ppConfig->ppOutFrmBuffer.frameBufferWidth = pp->frame_buf_w;
            ppConfig->ppOutFrmBuffer.frameBufferHeight = pp->frame_buf_h;
        }
    }

    ppConfig->ppOutImg.width = pp->img_out_w;
    ppConfig->ppOutImg.height = pp->img_out_h;

    if (pp->img_out_fmt == VC8000_PP_F_NV12)
    {
        ppConfig->ppOutImg.pixFormat = PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;
    }
    else if (pp->img_out_fmt == VC8000_PP_F_YUV422)
    {
        ppConfig->ppOutImg.pixFormat = PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED;
    }
    else
    {
        /*
         * PP output RGB format
         */
        ppConfig->ppOutRgb.rgbTransform = PP_YCBCR2RGB_TRANSFORM_CUSTOM;
        ppConfig->ppOutRgb.alpha = 0xFF;
        ppConfig->ppOutRgb.rgbTransformCoeffs.a = 298;
        ppConfig->ppOutRgb.rgbTransformCoeffs.b = 409;
        ppConfig->ppOutRgb.rgbTransformCoeffs.c = 208;
        ppConfig->ppOutRgb.rgbTransformCoeffs.d = 100;
        ppConfig->ppOutRgb.rgbTransformCoeffs.e = 516;
        ppConfig->ppOutRgb.ditheringEnable = 1;

        if (pp->img_out_fmt == VC8000_PP_F_RGB565)
        {
            ppConfig->ppOutImg.pixFormat = PP_PIX_FMT_RGB16_5_6_5;
        }
        else
        {
            /*
             * should be RGB888, no need to check
             */
            ppConfig->ppOutImg.pixFormat  = PP_PIX_FMT_RGB32;
        }
    }

#if 0
    //Wayne
    if (pp->pp_out_dst == VC8000_PP_OUT_DST_DISPLAY)
        ppConfig->ppOutImg.bufferBusAddr = DISP->FrameBufferAddress0;
    else if (pp->pp_out_dst == VC8000_PP_OUT_DST_OVERLAY)
        ppConfig->ppOutImg.bufferBusAddr = DISP->OverlayAddress0;
    else // VC8000_PP_OUT_DST_USER
#endif
        ppConfig->ppOutImg.bufferBusAddr = pp->pp_out_dst;

    ppConfig->ppOutImg.bufferChromaBusAddr = ppConfig->ppOutImg.bufferBusAddr +
            pp->frame_buf_w * pp->frame_buf_h;

    return 0;
}

static int jpeg_pp_in_config(struct vc8000_inst *inst, struct jpeg_ctx *jctx, JpegDecImageInfo *imageInfo)
{
    PPConfig *ppConfig = &jctx->ppConfig;
    int   ret;

    ppConfig->ppInCrop.enable = 0;   /* crop is not supported in current release */

    ppConfig->ppInImg.videoRange = 1;
    ppConfig->ppInImg.width = imageInfo->outputWidth;
    ppConfig->ppInImg.height = imageInfo->outputHeight;
    ppConfig->ppInImg.pixFormat = PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;

    // DBG_MSG("jpeg_pp_in_config: %d x %d\n", imageInfo->outputWidth, imageInfo->outputHeight);

    if (imageInfo->outputFormat)
    {
        switch (imageInfo->outputFormat)
        {
        case JPEGDEC_YCbCr400:
            ppConfig->ppInImg.pixFormat = PP_PIX_FMT_YCBCR_4_0_0;
            break;
        case JPEGDEC_YCbCr420_SEMIPLANAR:
            ppConfig->ppInImg.pixFormat = PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;
            break;
        case JPEGDEC_YCbCr422_SEMIPLANAR:
            ppConfig->ppInImg.pixFormat = PP_PIX_FMT_YCBCR_4_2_2_SEMIPLANAR;
            break;
        case JPEGDEC_YCbCr440:
            ppConfig->ppInImg.pixFormat = PP_PIX_FMT_YCBCR_4_4_0;
            break;
        case JPEGDEC_YCbCr411_SEMIPLANAR:
            ppConfig->ppInImg.pixFormat = PP_PIX_FMT_YCBCR_4_1_1_SEMIPLANAR;
            break;
        case JPEGDEC_YCbCr444_SEMIPLANAR:
            ppConfig->ppInImg.pixFormat = PP_PIX_FMT_YCBCR_4_4_4_SEMIPLANAR;
            break;
        }
    }

    // and finally set the PP config to the post-proc
    ret = PPSetConfig(jctx->ppInst, &jctx->ppConfig);
    if (ret != PP_OK)
    {
        DBG_MSG("%s - PPSetConfig failed! %d\n", __func__, ret);
        return -1;
    }
    return 0;
}
