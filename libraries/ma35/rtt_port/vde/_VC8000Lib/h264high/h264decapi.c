/*------------------------------------------------------------------------------
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--        only as expressly authorized by a licensing agreement from          --
--                                                                            --
--                            Hantro Products Oy.                             --
--                                                                            --
--                   (C) COPYRIGHT 2011 HANTRO PRODUCTS OY                    --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--                 The entire notice above must be reproduced                 --
--                  on all copies and should not be removed.                  --
--                                                                            --
--------------------------------------------------------------------------------
--
--  Abstract : Application Programming Interface (API) functions
--
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "version.h"
#include "h264hwd_container.h"
#include "h264decapi.h"
#include "h264hwd_decoder.h"
#include "h264hwd_util.h"
#include "h264hwd_exports.h"
#include "h264hwd_dpb.h"
#include "h264hwd_neighbour.h"
#include "h264hwd_asic.h"
#include "h264hwd_regdrv.h"
#include "h264hwd_byte_stream.h"
#include "deccfg.h"
#include "h264_pp_multibuffer.h"
#include "tiledref.h"
#include "workaround.h"
#include "errorhandling.h"
#include "commonconfig.h"

#include "dwl.h"
#include "h264decmc_internals.h"
/*------------------------------------------------------------------------------
       Version Information - DO NOT CHANGE!
------------------------------------------------------------------------------*/

#define H264DEC_MAJOR_VERSION 1
#define H264DEC_MINOR_VERSION 1

/*
 * H264DEC_TRACE         Trace H264 Decoder API function calls. H264DecTrace
 *                       must be implemented externally.
 * H264DEC_EVALUATION    Compile evaluation version, restricts number of frames
 *                       that can be decoded
 */

#ifndef TRACE_PP_CTRL
    #define TRACE_PP_CTRL(...)          do{}while(0)
#else
    #undef TRACE_PP_CTRL
    #define TRACE_PP_CTRL(...)          sysprintf(__VA_ARGS__)
#endif

#ifdef H264DEC_TRACE
    #define DEC_API_TRC(str)    sysprintf(str)
#else
    #define DEC_API_TRC(str)    do{}while(0)
#endif


#ifdef USE_RANDOM_TEST
    #include "string.h"
    #include "stream_corrupt.h"
#endif

static void h264UpdateAfterPictureDecode(decContainer_t *pDecCont);
static u32 h264SpsSupported(const decContainer_t *pDecCont);
static u32 h264PpsSupported(const decContainer_t *pDecCont);
static u32 h264StreamIsBaseline(const decContainer_t *pDecCont);

static u32 h264AllocateResources(decContainer_t *pDecCont);
static void bsdDecodeReturn(u32 retval);
extern void h264InitPicFreezeOutput(decContainer_t *pDecCont, u32 fromOldDpb);

static void h264GetSarInfo(const storage_t *pStorage,
                           u32 *sar_width, u32 *sar_height);
extern void h264PreparePpRun(decContainer_t *pDecCont);

static void h264CheckReleasePpAndHw(decContainer_t *pDecCont);

#ifdef USE_OUTPUT_RELEASE
static H264DecRet H264DecNextPicture_INTERNAL(H264DecInst decInst,
        H264DecPicture *pOutput,
        u32 endOfStream);
#endif

#ifdef USE_EXTERNAL_BUFFER
    static void H264SetExternalBufferInfo(H264DecInst decInst, storage_t *storage) ;
    static void H264SetMVCExternalBufferInfo(H264DecInst decInst, storage_t *storage);
#endif
#ifdef USE_OUTPUT_RELEASE
    static void H264EnterAbortState(decContainer_t *pDecCont);
    static void H264ExistAbortState(decContainer_t *pDecCont);
#endif

#define DEC_DPB_NOT_INITIALIZED      -1

/*------------------------------------------------------------------------------

    Function: H264DecInit()

        Functional description:
            Initialize decoder software. Function reserves memory for the
            decoder instance and calls h264bsdInit to initialize the
            instance data.

        Inputs:
            noOutputReordering  flag to indicate decoder that it doesn't have
                                to try to provide output pictures in display
                                order, saves memory
            errorHandling
                                Flag to determine which error concealment
                                method to use.
            useDisplaySmooothing
                                flag to enable extra buffering in DPB output
                                so that application may read output pictures
                                one by one
        Outputs:
            decInst             pointer to initialized instance is stored here

        Returns:
            H264DEC_OK        successfully initialized the instance
            H264DEC_INITFAIL  initialization failed
            H264DEC_PARAM_ERROR invalid parameters
            H264DEC_MEMFAIL   memory allocation failed
            H264DEC_DWL_ERROR error initializing the system interface
------------------------------------------------------------------------------*/

H264DecRet H264DecInit(H264DecInst *pDecInst,
#ifdef USE_EXTERNAL_BUFFER
    const void *dwl,
#endif
                       u32 noOutputReordering,
                       DecErrorHandling errorHandling,
                       u32 useDisplaySmoothing,
                       DecDpbFlags dpbFlags,
                       u32 useAdaptiveBuffers,
                       u32 nGuardSize,
                       u32 useSecureMode)
{

    /*@null@ */ decContainer_t *pDecCont;
#ifndef USE_EXTERNAL_BUFFER
    /*@null@ */ const void *dwl;

    DWLInitParam_t dwlInit;
#endif

    DWLHwConfig_t hwCfg;
    u32 asicID;
    u32 referenceFrameFormat;

    DEC_API_TRC("H264DecInit#\n");

    /* check that right shift on negative numbers is performed signed */
    /*lint -save -e* following check causes multiple lint messages */
#if (((-1) >> 1) != (-1))
#error Right bit-shifting (>>) does not preserve the sign
#endif
    /*lint -restore */

    if (pDecInst == NULL)
    {
        DEC_API_TRC("H264DecInit# ERROR: pDecInst == NULL");
        return (H264DEC_PARAM_ERROR);
    }

    *pDecInst = NULL;   /* return NULL instance for any error */

    /* check for proper hardware */
    asicID = DWLReadAsicID();

    if ((asicID >> 16) < 0x8170U &&
            ((asicID >> 16) != 0x6731U && (asicID >> 16) != 0x6e64U))
    {
        DEC_API_TRC("H264DecInit# ERROR: HW not recognized/unsupported!\n");
        return H264DEC_FORMAT_NOT_SUPPORTED;
    }

    /* check that H.264 decoding supported in HW */
    (void) DWLmemset(&hwCfg, 0, sizeof(DWLHwConfig_t));
    DWLReadAsicConfig(&hwCfg);
    if (!hwCfg.h264Support)
    {
        DEC_API_TRC("H264DecInit# ERROR: H264 not supported in HW\n");
        return H264DEC_FORMAT_NOT_SUPPORTED;
    }

    if (!hwCfg.addr64Support && sizeof(void *) == 8)
    {
        DEC_API_TRC("H264DecInit# ERROR: HW not support 64bit address!\n");
        // ychuang - ignore this // return (H264DEC_PARAM_ERROR);
    }

#ifndef USE_EXTERNAL_BUFFER
    dwlInit.clientType = DWL_CLIENT_TYPE_H264_DEC;

    dwl = DWLInit(&dwlInit);

    if (dwl == NULL)
    {
        DEC_API_TRC("H264DecInit# ERROR: DWL Init failed\n");
        return (H264DEC_DWL_ERROR);
    }
#endif

    pDecCont = (decContainer_t *) DWLmalloc(sizeof(decContainer_t));

    if (pDecCont == NULL)
    {
#ifndef USE_EXTERNAL_BUFFER
        (void) DWLRelease(dwl);

        DEC_API_TRC("H264DecInit# ERROR: Memory allocation failed\n");
#endif
        return (H264DEC_MEMFAIL);
    }

    (void) DWLmemset(pDecCont, 0, sizeof(decContainer_t));
    pDecCont->dwl = dwl;

    h264bsdInit(&pDecCont->storage, noOutputReordering,
                useDisplaySmoothing);

    pDecCont->decStat = H264DEC_INITIALIZED;

    SetDecRegister(pDecCont->h264Regs, HWIF_DEC_MODE, DEC_X170_MODE_H264);

    SetCommonConfigRegs(pDecCont->h264Regs);

    /* Set prediction filter taps */
    SetDecRegister(pDecCont->h264Regs, HWIF_PRED_BC_TAP_0_0, 1);
    SetDecRegister(pDecCont->h264Regs, HWIF_PRED_BC_TAP_0_1, (u32)(-5));
    SetDecRegister(pDecCont->h264Regs, HWIF_PRED_BC_TAP_0_2, 20);
    //pthread_mutex_init(&pDecCont->protect_mutex, NULL);

    /* save HW version so we dont need to check it all the time when deciding the control stuff */
    pDecCont->is8190 = (asicID >> 16) != 0x8170U ? 1 : 0;
    pDecCont->h264ProfileSupport = hwCfg.h264Support;

    if ((asicID >> 16)  == 0x8170U)
        errorHandling = 0;

    /* save ref buffer support status */
    pDecCont->refBufSupport = hwCfg.refBufSupport;
    referenceFrameFormat = dpbFlags & DEC_REF_FRM_FMT_MASK;
    if (referenceFrameFormat == DEC_REF_FRM_TILED_DEFAULT)
    {
        /* Assert support in HW before enabling.. */
        if (!hwCfg.tiledModeSupport)
        {
            //pthread_mutex_destroy(&pDecCont->protect_mutex);
            DWLfree(pDecCont);
#ifndef USE_EXTERNAL_BUFFER
            (void) DWLRelease(dwl);
#endif
            return H264DEC_FORMAT_NOT_SUPPORTED;
        }
        pDecCont->tiledModeSupport = hwCfg.tiledModeSupport;
    }
    else
        pDecCont->tiledModeSupport = 0;

    /* Custom DPB modes require tiled support >= 2 */
    pDecCont->allowDpbFieldOrdering = 0;
    pDecCont->dpbMode = DEC_DPB_NOT_INITIALIZED;
    if (dpbFlags & DEC_DPB_ALLOW_FIELD_ORDERING)
    {
        pDecCont->allowDpbFieldOrdering = hwCfg.fieldDpbSupport;
    }
    pDecCont->storage.intraFreeze = errorHandling == DEC_EC_VIDEO_FREEZE;
#ifndef _DISABLE_PIC_FREEZE
    if (errorHandling == DEC_EC_PARTIAL_FREEZE)
        pDecCont->storage.partialFreeze = 1;
    else if (errorHandling == DEC_EC_PARTIAL_IGNORE)
        pDecCont->storage.partialFreeze = 2;
#endif
    pDecCont->storage.pictureBroken = HANTRO_FALSE;

    pDecCont->maxDecPicWidth = hwCfg.maxDecPicWidth;    /* max decodable picture width */

    pDecCont->checksum = pDecCont;  /* save instance as a checksum */

#ifdef _ENABLE_2ND_CHROMA
    pDecCont->storage.enable2ndChroma = 1;
#endif

    InitWorkarounds(DEC_X170_MODE_H264, &pDecCont->workarounds);
    if (pDecCont->workarounds.h264.frameNum)
        pDecCont->frameNumMask = 0x1000;

    /*  default single core */
    pDecCont->nCores = 1;

    /* Init frame buffer list */
    InitList(&pDecCont->fbList);

    pDecCont->storage.dpbs[0]->fbList = &pDecCont->fbList;
    pDecCont->storage.dpbs[1]->fbList = &pDecCont->fbList;

    pDecCont->useAdaptiveBuffers = useAdaptiveBuffers;
    pDecCont->nGuardSize = nGuardSize;

    pDecCont->secureMode = useSecureMode;
    if (pDecCont->secureMode)
    {
        pDecCont->refBufSupport = 0;
    }

#ifdef USE_RANDOM_TEST
    /*********************************************************/
    /** Developers can change below parameters to generate  **/
    /** different kinds of error stream.                    **/
    /*********************************************************/
    pDecCont->errorParams.seed = 66;
    strcpy(pDecCont->errorParams.truncateStreamOdds, "1 : 6");
    strcpy(pDecCont->errorParams.swapBitOdds, "1 : 100000");
    strcpy(pDecCont->errorParams.packetLossOdds, "1 : 6");
    /*********************************************************/

    if (strcmp(pDecCont->errorParams.swapBitOdds, "0") != 0)
        pDecCont->errorParams.swapBitsInStream = 0;

    if (strcmp(pDecCont->errorParams.packetLossOdds, "0") != 0)
        pDecCont->errorParams.losePackets = 1;

    if (strcmp(pDecCont->errorParams.truncateStreamOdds, "0") != 0)
        pDecCont->errorParams.truncateStream = 1;

    pDecCont->ferrorStream = ff_fopen("random_error.h264", "wb");
    if (pDecCont->ferrorStream == NULL)
    {
        DEBUG_PRINT(("Unable to open file error.h264\n"));
        //pthread_mutex_destroy(&pDecCont->protect_mutex);
        DWLfree(pDecCont);
#ifndef USE_EXTERNAL_BUFFER
        (void) DWLRelease(dwl);
#endif
        return H264DEC_MEMFAIL;
    }

    if (pDecCont->errorParams.swapBitsInStream ||
            pDecCont->errorParams.losePackets ||
            pDecCont->errorParams.truncateStream)
    {
        pDecCont->errorParams.randomErrorEnabled = 1;
        InitializeRandom(pDecCont->errorParams.seed);
    }
#endif

    *pDecInst = (H264DecInst) pDecCont;

    DEC_API_TRC("H264DecInit# OK\n");

    return (H264DEC_OK);
}

/*------------------------------------------------------------------------------

    Function: H264DecGetInfo()

        Functional description:
            This function provides read access to decoder information. This
            function should not be called before H264DecDecode function has
            indicated that headers are ready.

        Inputs:
            decInst     decoder instance

        Outputs:
            pDecInfo    pointer to info struct where data is written

        Returns:
            H264DEC_OK            success
            H264DEC_PARAM_ERROR     invalid parameters
            H264DEC_HDRS_NOT_RDY  information not available yet

------------------------------------------------------------------------------*/
H264DecRet H264DecGetInfo(H264DecInst decInst, H264DecInfo *pDecInfo)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;
#ifdef USE_EXTERNAL_BUFFER
    u32 maxDpbSize, noReorder;
#endif
    storage_t *pStorage;

    DEC_API_TRC("H264DecGetInfo#");

    if (decInst == NULL || pDecInfo == NULL)
    {
        DEC_API_TRC("H264DecGetInfo# ERROR: decInst or pDecInfo is NULL\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecGetInfo# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }

    pStorage = &pDecCont->storage;

    if (pStorage->activeSps == NULL || pStorage->activePps == NULL)
    {
        DEC_API_TRC("H264DecGetInfo# ERROR: Headers not decoded yet\n");
        return (H264DEC_HDRS_NOT_RDY);
    }

    /* h264bsdPicWidth and -Height return dimensions in macroblock units,
     * picWidth and -Height in pixels */
    pDecInfo->picWidth = h264bsdPicWidth(pStorage) << 4;
    pDecInfo->picHeight = h264bsdPicHeight(pStorage) << 4;
    pDecInfo->videoRange = h264bsdVideoRange(pStorage);
    pDecInfo->matrixCoefficients = h264bsdMatrixCoefficients(pStorage);
    pDecInfo->monoChrome = h264bsdIsMonoChrome(pStorage);
    pDecInfo->interlacedSequence = pStorage->activeSps->frameMbsOnlyFlag == 0 ? 1 : 0;
#ifndef USE_EXTERNAL_BUFFER
    pDecInfo->picBuffSize = pStorage->dpb->dpbSize + 1;
#else
    if (pStorage->noReordering ||
            pStorage->activeSps->picOrderCntType == 2 ||
            (pStorage->activeSps->vuiParametersPresentFlag &&
             pStorage->activeSps->vuiParameters->bitstreamRestrictionFlag &&
             !pStorage->activeSps->vuiParameters->numReorderFrames))
        noReorder = HANTRO_TRUE;
    else
        noReorder = HANTRO_FALSE;
    if (pStorage->view == 0)
        maxDpbSize = pStorage->activeSps->maxDpbSize;
    else
    {
        /* stereo view dpb size at least equal to base view size (to make sure
         * that base view pictures get output in correct display order) */
        maxDpbSize = MAX(pStorage->activeSps->maxDpbSize,
                         pStorage->activeViewSps[0]->maxDpbSize);
    }
    /* restrict max dpb size of mvc (stereo high) streams, make sure that
     * base address 15 is available/restricted for inter view reference use */
    if (pStorage->mvcStream)
        maxDpbSize = MIN(maxDpbSize, 8);
    if (noReorder)
        pDecInfo->picBuffSize = MAX(pStorage->activeSps->numRefFrames, 1) + 1;
    else
        pDecInfo->picBuffSize = maxDpbSize + 1;
#endif
    pDecInfo->multiBuffPpSize =  pStorage->dpb->noReordering ? 2 : pDecInfo->picBuffSize;
    pDecInfo->dpbMode = pDecCont->dpbMode;

    if (pStorage->mvc)
        pDecInfo->multiBuffPpSize *= 2;

    h264GetSarInfo(pStorage, &pDecInfo->sarWidth, &pDecInfo->sarHeight);

    h264bsdCroppingParams(pStorage, &pDecInfo->cropParams);

    if (pDecCont->tiledModeSupport)
    {
        if (pDecInfo->interlacedSequence &&
                (pDecInfo->dpbMode != DEC_DPB_INTERLACED_FIELD))
        {
            if (pDecInfo->monoChrome)
                pDecInfo->outputFormat = H264DEC_YUV400;
            else
                pDecInfo->outputFormat = H264DEC_SEMIPLANAR_YUV420;
        }
        else
            pDecInfo->outputFormat = H264DEC_TILED_YUV420;
    }
    else if (pDecInfo->monoChrome)
        pDecInfo->outputFormat = H264DEC_YUV400;
    else
        pDecInfo->outputFormat = H264DEC_SEMIPLANAR_YUV420;

    DEC_API_TRC("H264DecGetInfo# OK\n");

    return (H264DEC_OK);
}

#ifdef USE_EXTERNAL_BUFFER
u32 IsDpbRealloc(decContainer_t *pDecCont)
{
    storage_t *pStorage = &pDecCont->storage;
    dpbStorage_t *dpb = pStorage->dpb;
    seqParamSet_t *pSps = pStorage->activeSps;
    u32 isHighSupported = (pDecCont->h264ProfileSupport == H264_HIGH_PROFILE) ? 1 : 0;
    u32 nCores = pDecCont->nCores;
    u32 maxDpbSize, newPicSizeInMbs = 0, newPicSize, newTotBuffers, dpbSize, maxRefFrames;
    u32 noReorder;
    struct dpbInitParams dpbParams;

    if (!pDecCont->useAdaptiveBuffers)
        return 1;

    if (pDecCont->bMVC == 0)
        newPicSizeInMbs = pSps->picWidthInMbs * pSps->picHeightInMbs;
    else if (pDecCont->bMVC == 1)
    {
        if (pStorage->sps[1] != 0)
            newPicSizeInMbs = MAX(pStorage->sps[0]->picWidthInMbs * pStorage->sps[0]->picHeightInMbs,
                                  pStorage->sps[1]->picWidthInMbs * pStorage->sps[1]->picHeightInMbs);
        else
            newPicSizeInMbs = pStorage->sps[0]->picWidthInMbs * pStorage->sps[0]->picHeightInMbs;

    }

    /* dpb output reordering disabled if
     * 1) application set noReordering flag
     * 2) POC type equal to 2
     * 3) num_reorder_frames in vui equal to 0 */
    if (pStorage->noReordering ||
            pSps->picOrderCntType == 2 ||
            (pSps->vuiParametersPresentFlag &&
             pSps->vuiParameters->bitstreamRestrictionFlag &&
             !pSps->vuiParameters->numReorderFrames))
        noReorder = HANTRO_TRUE;
    else
        noReorder = HANTRO_FALSE;

    if (pStorage->view == 0)
        maxDpbSize = pSps->maxDpbSize;
    else
    {
        /* stereo view dpb size at least equal to base view size (to make sure
         * that base view pictures get output in correct display order) */
        maxDpbSize = MAX(pSps->maxDpbSize, pStorage->activeViewSps[0]->maxDpbSize);
    }
    /* restrict max dpb size of mvc (stereo high) streams, make sure that
     * base address 15 is available/restricted for inter view reference use */
    if (pStorage->mvcStream)
        maxDpbSize = MIN(maxDpbSize, 8);

    dpbParams.picSizeInMbs = newPicSizeInMbs;
    dpbParams.dpbSize = maxDpbSize;
    dpbParams.maxRefFrames = pSps->numRefFrames;
    dpbParams.maxFrameNum = pSps->maxFrameNum;
    dpbParams.noReordering = noReorder;
    dpbParams.displaySmoothing = pStorage->useSmoothing;
    dpbParams.monoChrome = pSps->monoChrome;
    dpbParams.isHighSupported = isHighSupported;
    dpbParams.enable2ndChroma = pStorage->enable2ndChroma && !pSps->monoChrome;
    dpbParams.multiBuffPp = pStorage->multiBuffPp;
    dpbParams.nCores = nCores;
    dpbParams.mvcView = pStorage->view;

    if (dpbParams.isHighSupported)
    {
        /* yuv picture + direct mode motion vectors */
        newPicSize = dpbParams.picSizeInMbs * ((dpbParams.monoChrome ? 256 : 384) + 64);

        /* allocate 32 bytes for multicore status fields */
        /* locate it after picture and direct MV */
        newPicSize += 32;
    }
    else
    {
        newPicSize = dpbParams.picSizeInMbs * 384;
    }
    if (dpbParams.enable2ndChroma && !dpbParams.monoChrome)
    {
        newPicSize += newPicSizeInMbs * 128;
    }

    dpb->nNewPicSize = newPicSize;
    maxRefFrames = MAX(dpbParams.maxRefFrames, 1);

    if (dpbParams.noReordering)
        dpbSize = maxRefFrames;
    else
        dpbSize = dpbParams.dpbSize;

    /* max DPB size is (16 + 1) buffers */
    newTotBuffers = dpbSize + 1;

    /* figure out extra buffers for smoothing, pp, multicore, etc... */
    if (nCores == 1)
    {
        /* single core configuration */
        if (pStorage->useSmoothing)
            newTotBuffers += noReorder ? 1 : dpbSize + 1;
        else if (pStorage->multiBuffPp)
            newTotBuffers++;
    }
    else
    {
        /* multi core configuration */

        if (pStorage->useSmoothing && !noReorder)
        {
            /* at least double buffers for smooth output */
            if (newTotBuffers > nCores)
            {
                newTotBuffers *= 2;
            }
            else
            {
                newTotBuffers += nCores;
            }
        }
        else
        {
            /* one extra buffer for each core */
            /* do not allocate twice for multiview */
            if (!pStorage->view)
                newTotBuffers += nCores;
        }
    }

    if ((dpb->nNewPicSize <= pDecCont->nExtBufSize) &&
            newTotBuffers + dpb->nGuardSize <= dpb->totBuffers)
        return 0;
    return 1;
}
#endif

/*------------------------------------------------------------------------------

    Function: H264DecRelease()

        Functional description:
            Release the decoder instance. Function calls h264bsdShutDown to
            release instance data and frees the memory allocated for the
            instance.

        Inputs:
            decInst     Decoder instance

        Outputs:
            none

        Returns:
            none

------------------------------------------------------------------------------*/

void H264DecRelease(H264DecInst decInst)
{

    decContainer_t *pDecCont = (decContainer_t *) decInst;
    const void *dwl;

    DEC_API_TRC("H264DecRelease#\n");

    if (pDecCont == NULL)
    {
        DEC_API_TRC("H264DecRelease# ERROR: decInst == NULL\n");
        return;
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecRelease# ERROR: Decoder not initialized\n");
        return;
    }

#ifdef USE_RANDOM_TEST
    if (pDecCont->ferrorStream)
        fclose(pDecCont->ferrorStream);
#endif


    /* PP instance must be already disconnected at this point */
    ASSERT(pDecCont->pp.ppInstance == NULL);

    dwl = pDecCont->dwl;

    /* make sure all in sync in multicore mode, hw idle, output empty */
    //if(pDecCont->bMC)
    //{
    //    h264MCWaitPicReadyAll(pDecCont);
    //}
    //else
    {
        u32 i;
        const dpbStorage_t *dpb = pDecCont->storage.dpb;

        /* Empty the output list. This is just so that fbList does not
         * complaint about still referenced pictures
         */
        for (i = 0; i < dpb->totBuffers; i++)
        {
            if (IsBufferOutput(&pDecCont->fbList, dpb->picBuffID[i]))
            {
                ClearOutput(&pDecCont->fbList, dpb->picBuffID[i]);
            }
        }
    }

    if (pDecCont->asicRunning)
    {
        /* stop HW */
        SetDecRegister(pDecCont->h264Regs, HWIF_DEC_IRQ_STAT, 0);
        SetDecRegister(pDecCont->h264Regs, HWIF_DEC_IRQ, 0);
        SetDecRegister(pDecCont->h264Regs, HWIF_DEC_E, 0);
        DWLDisableHW(pDecCont->dwl, pDecCont->coreID, 4 * 1,
                     pDecCont->h264Regs[1] | DEC_IRQ_DISABLE);
        DWLReleaseHw(dwl, pDecCont->coreID);  /* release HW lock */
        pDecCont->asicRunning = 0;

        /* Decrement usage for DPB buffers */
        DecrementDPBRefCount(&pDecCont->storage.dpb[1]);
    }
    else if (pDecCont->keepHwReserved)
        DWLReleaseHw(dwl, pDecCont->coreID);  /* release HW lock */
    //pthread_mutex_destroy(&pDecCont->protect_mutex);

    h264bsdShutdown(&pDecCont->storage);

    h264bsdFreeDpb(
#ifndef USE_EXTERNAL_BUFFER
        dwl,
#endif
        pDecCont->storage.dpbs[0]);
    if (pDecCont->storage.dpbs[1]->dpbSize)
        h264bsdFreeDpb(
#ifndef USE_EXTERNAL_BUFFER
            dwl,
#endif
            pDecCont->storage.dpbs[1]);

    ReleaseAsicBuffers(dwl, pDecCont->asicBuff);

    ReleaseList(&pDecCont->fbList);
    pDecCont->checksum = NULL;
    DWLfree(pDecCont);
#ifndef USE_EXTERNAL_BUFFER
    (void) DWLRelease(dwl);

    DEC_API_TRC("H264DecRelease# OK\n");
#endif

    return;
}

/*------------------------------------------------------------------------------

    Function: H264DecDecode

        Functional description:
            Decode stream data. Calls h264bsdDecode to do the actual decoding.

        Input:
            decInst     decoder instance
            pInput      pointer to input struct

        Outputs:
            pOutput     pointer to output struct

        Returns:
            H264DEC_NOT_INITIALIZED   decoder instance not initialized yet
            H264DEC_PARAM_ERROR         invalid parameters

            H264DEC_STRM_PROCESSED    stream buffer decoded
            H264DEC_HDRS_RDY    headers decoded, stream buffer not finished
            H264DEC_PIC_DECODED decoding of a picture finished
            H264DEC_STRM_ERROR  serious error in decoding, no valid parameter
                                sets available to decode picture data
            H264DEC_PENDING_FLUSH   next invocation of H264DecDecode() function
                                    flushed decoded picture buffer, application
                                    needs to read all output pictures (using
                                    H264DecNextPicture function) before calling
                                    H264DecDecode() again. Used only when
                                    useDisplaySmoothing was enabled in init.

            H264DEC_HW_BUS_ERROR    decoder HW detected a bus error
            H264DEC_SYSTEM_ERROR    wait for hardware has failed
            H264DEC_MEMFAIL         decoder failed to allocate memory
            H264DEC_DWL_ERROR   System wrapper failed to initialize
            H264DEC_HW_TIMEOUT  HW timeout
            H264DEC_HW_RESERVED HW could not be reserved
------------------------------------------------------------------------------*/
H264DecRet H264DecDecode(H264DecInst decInst, const H264DecInput *input,
                         H264DecOutput *pOutput)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;

    // input structure may be updated in secure mode,
    // a workaround to use temporary structure instead.
    H264DecInput tmpInput;
    H264DecInput *pInput = &tmpInput;
    (void) DWLmemcpy(pInput, input, sizeof(H264DecInput));

    u32 strmLen;
    u32 inputDataLen;  // used to generate error stream
    const u8 *tmpStream;
    u32 index = 0;
    const u8 *refData = NULL;
    H264DecRet returnValue = H264DEC_STRM_PROCESSED;

    DEC_API_TRC("H264DecDecode#\n");
    /* Check that function input parameters are valid */
    if (pInput == NULL || pOutput == NULL || decInst == NULL)
    {
        DEC_API_TRC("H264DecDecode# ERROR: NULL arg(s)\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecDecode# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }

    inputDataLen = pInput->dataLen;

#ifdef USE_OUTPUT_RELEASE
    if (pDecCont->abort)
    {
        return (H264DEC_ABORTED);
    }
#endif

    // parse secure info(16bytes) if secure mode is enabled.
    if (pDecCont->secureMode)
    {
        ASSERT(inputDataLen >= 16);
        strmData_t strmTmp;

        strmTmp.pStrmBuffStart = pInput->pStream;
        strmTmp.pStrmCurrPos = pInput->pStream;
        strmTmp.bitPosInWord = 0;
        strmTmp.strmBuffSize = inputDataLen;
        strmTmp.strmBuffReadBits = 0;
        strmTmp.removeEmul3Byte = 1;
        strmTmp.emulByteCount = 0;

        h264bsdExtractSecureInfo(&strmTmp,
                                 &pDecCont->secureDataBusAddress,
                                 &pDecCont->secureDataLen);

        pInput->pStream += 16;
        pInput->streamBusAddress += 16;
        pInput->dataLen -= 16;
        inputDataLen = pInput->dataLen;
    }

#ifdef USE_RANDOM_TEST
    if (pDecCont->errorParams.randomErrorEnabled)
    {
        // error type: lose packets;
        if (pDecCont->errorParams.losePackets && !pDecCont->streamNotConsumed)
        {
            u8 losePacket = 0;
            if (RandomizePacketLoss(pDecCont->errorParams.packetLossOdds,
                                    &losePacket))
            {
                DEBUG_PRINT(("Packet loss simulator error (wrong config?)\n"));
            }
            if (losePacket)
            {
                inputDataLen = 0;
                pOutput->dataLeft = 0;
                pDecCont->streamNotConsumed = 0;
                return H264DEC_STRM_PROCESSED;
            }
        }

        // error type: truncate stream(random len for random packet);
        if (pDecCont->errorParams.truncateStream && !pDecCont->streamNotConsumed)
        {
            u8 truncateStream = 0;
            if (RandomizePacketLoss(pDecCont->errorParams.truncateStreamOdds,
                                    &truncateStream))
            {
                DEBUG_PRINT(("Truncate stream simulator error (wrong config?)\n"));
            }
            if (truncateStream)
            {
                u32 randomSize = inputDataLen;
                if (RandomizeU32(&randomSize))
                {
                    DEBUG_PRINT(("Truncate randomizer error (wrong config?)\n"));
                }
                inputDataLen = randomSize;
            }

            pDecCont->prevInputLen = inputDataLen;

            if (inputDataLen == 0)
            {
                pOutput->dataLeft = 0;
                pDecCont->streamNotConsumed = 0;
                return H264DEC_STRM_PROCESSED;
            }
        }

        /*  stream is truncated but not consumed at first time, the same truncated length
            at the second time */
        if (pDecCont->errorParams.truncateStream && pDecCont->streamNotConsumed)
            inputDataLen = pDecCont->prevInputLen;

        // error type: swap bits;
        if (pDecCont->errorParams.swapBitsInStream && !pDecCont->streamNotConsumed)
        {
            u8 *pTmp = (u8 *)pInput->pStream;
            if (RandomizeBitSwapInStream(pTmp, inputDataLen,
                                         pDecCont->errorParams.swapBitOdds))
            {
                DEBUG_PRINT(("Bitswap randomizer error (wrong config?)\n"));
            }
        }
    }
#endif

    if (pInput->dataLen == 0 ||
            pInput->dataLen > DEC_X170_MAX_STREAM ||
            X170_CHECK_VIRTUAL_ADDRESS(pInput->pStream) ||
            X170_CHECK_BUS_ADDRESS(pInput->streamBusAddress))
    {
        DEC_API_TRC("H264DecDecode# ERROR: Invalid arg value\n");
        return H264DEC_PARAM_ERROR;
    }

#ifdef H264DEC_EVALUATION
    if (pDecCont->picNumber > H264DEC_EVALUATION)
    {
        DEC_API_TRC("H264DecDecode# H264DEC_EVALUATION_LIMIT_EXCEEDED\n");
        return H264DEC_EVALUATION_LIMIT_EXCEEDED;
    }
#endif
    pDecCont->streamPosUpdated = 0;
    pOutput->pStrmCurrPos = NULL;
    pDecCont->hwStreamStartBus = pInput->streamBusAddress;
    pDecCont->pHwStreamStart = pInput->pStream;
    strmLen = pDecCont->hwLength = inputDataLen;
    tmpStream = pInput->pStream;
    pDecCont->streamConsumedCallback.pStrmBuff = pInput->pStream;
    pDecCont->streamConsumedCallback.pUserData = pInput->pUserData;

    pDecCont->skipNonReference = pInput->skipNonReference;

    pDecCont->forceNalMode = 0;

    /* Switch to RLC mode, i.e. sw performs entropy decoding */
    if (pDecCont->reallocate)
    {
        DEBUG_PRINT(("H264DecDecode: Reallocate\n"));
        pDecCont->rlcMode = 1;
        pDecCont->reallocate = 0;

        /* Reallocate only once */
        if (pDecCont->asicBuff->mbCtrl.virtualAddress == NULL)
        {
            if (h264AllocateResources(pDecCont) != 0)
            {
                /* signal that decoder failed to init parameter sets */
                pDecCont->storage.activePpsId = MAX_NUM_PIC_PARAM_SETS;
                DEC_API_TRC("H264DecDecode# ERROR: Reallocation failed\n");
                return H264DEC_MEMFAIL;
            }

            h264bsdResetStorage(&pDecCont->storage);
        }

    }

    /* get PP pipeline status at the beginning of each new picture */
    if (pDecCont->pp.ppInstance != NULL &&
            pDecCont->storage.picStarted == HANTRO_FALSE)
    {
        /* store old multibuffer status to compare with new */
        const u32 oldMulti = pDecCont->pp.ppInfo.multiBuffer;
        u32 maxId = pDecCont->storage.dpb->noReordering ? 1 :
                    pDecCont->storage.dpb->dpbSize;

        ASSERT(pDecCont->pp.PPConfigQuery != NULL);
        pDecCont->pp.ppInfo.tiledMode =
            pDecCont->tiledReferenceEnable;
        pDecCont->pp.PPConfigQuery(pDecCont->pp.ppInstance,
                                   &pDecCont->pp.ppInfo);
        /* Make sure pipeline turned off for interlaced streams (there is a
         * possibility for having prog pictures in the stream --> these are
         * not run in pipeline anymore) */
        if (pDecCont->storage.activeSps &&
                !pDecCont->storage.activeSps->frameMbsOnlyFlag)
            pDecCont->pp.ppInfo.pipelineAccepted = 0;

        TRACE_PP_CTRL("H264DecDecode: PP pipelineAccepted = %d\n",
                      pDecCont->pp.ppInfo.pipelineAccepted);
        TRACE_PP_CTRL("H264DecDecode: PP multiBuffer = %d\n",
                      pDecCont->pp.ppInfo.multiBuffer);

        /* increase number of buffers if multiview decoding */
        if (pDecCont->storage.numViews)
        {
            maxId += pDecCont->storage.dpb->noReordering ? 1 :
                     pDecCont->storage.dpb->dpbSize + 1;
            maxId = MIN(maxId, 16);
        }

        if (oldMulti != pDecCont->pp.ppInfo.multiBuffer)
        {
            h264PpMultiInit(pDecCont, maxId);
        }
        /* just increase amount of buffers */
        else if (maxId > pDecCont->pp.multiMaxId)
            h264PpMultiMvc(pDecCont, maxId);
    }

    do
    {
        u32 decResult;
        u32 numReadBytes = 0;
        storage_t *pStorage = &pDecCont->storage;

        DEBUG_PRINT(("H264DecDecode: mode is %s\n",
                     pDecCont->rlcMode ? "RLC" : "VLC"));

        if (pDecCont->decStat == H264DEC_NEW_HEADERS)
        {
            decResult = H264BSD_HDRS_RDY;
            pDecCont->decStat = H264DEC_INITIALIZED;
        }
        else if (pDecCont->decStat == H264DEC_BUFFER_EMPTY)
        {
            DEBUG_PRINT(("H264DecDecode: Skip h264bsdDecode\n"));
            DEBUG_PRINT(("H264DecDecode: Jump to H264BSD_PIC_RDY\n"));
            /* update stream pointers */
            strmData_t *strm = pDecCont->storage.strm;
            strm->pStrmBuffStart = tmpStream;
            strm->strmBuffSize = strmLen;

            /* NAL start prefix in stream start is 0 0 0 or 0 0 1 */
            if (!(*strm->pStrmBuffStart + * (strm->pStrmBuffStart + 1)) && strmLen >= 3)
            {
                if (*(strm->pStrmBuffStart + 2) < 2)
                {
                    strm->pStrmBuffStart += 3;
                    strm->strmBuffSize -= 3;
                }
            }


            decResult = H264BSD_PIC_RDY;
        }
#ifdef USE_EXTERNAL_BUFFER
        else if (pDecCont->decStat == H264DEC_WAITING_FOR_BUFFER)
        {
            decResult = H264BSD_BUFFER_NOT_READY;
        }
#endif
        else
        {
            decResult = h264bsdDecode(pDecCont, tmpStream, strmLen,
                                      pInput->picId, &numReadBytes);

            ASSERT(numReadBytes <= strmLen);

            bsdDecodeReturn(decResult);
        }

        tmpStream += numReadBytes;
        strmLen -= numReadBytes;
        switch (decResult)
        {
#ifndef USE_EXTERNAL_BUFFER
        case H264BSD_HDRS_RDY:
        {
            h264CheckReleasePpAndHw(pDecCont);

            pDecCont->storage.multiBuffPp =
                pDecCont->pp.ppInstance != NULL &&
                pDecCont->pp.ppInfo.multiBuffer;

            if (pStorage->dpb->flushed && pStorage->dpb->numOut)
            {
                /* output first all DPB stored pictures */
                pStorage->dpb->flushed = 0;
                pDecCont->decStat = H264DEC_NEW_HEADERS;
                /* if display smoothing used -> indicate that all pictures
                * have to be read out */
                if ((pStorage->dpb->totBuffers >
                        pStorage->dpb->dpbSize + 1) &&
                        !pDecCont->storage.multiBuffPp)
                {
                    returnValue = H264DEC_PENDING_FLUSH;
                }
                else
                {
                    returnValue = H264DEC_PIC_DECODED;
                }

                /* base view -> flush another view dpb */
                if (pDecCont->storage.numViews &&
                        pDecCont->storage.view == 0)
                {
                    h264bsdFlushDpb(pStorage->dpbs[1]);
                }

                DEC_API_TRC
                ("H264DecDecode# H264DEC_PIC_DECODED (DPB flush caused by new SPS)\n");
                strmLen = 0;

                break;
            }
            else if ((pStorage->dpb->totBuffers >
                      pStorage->dpb->dpbSize + 1) && pStorage->dpb->numOut)
            {
                /* flush buffered output for display smoothing */
                pStorage->dpb->delayedOut = 0;
                pStorage->secondField = 0;
                pDecCont->decStat = H264DEC_NEW_HEADERS;
                returnValue = H264DEC_PENDING_FLUSH;
                strmLen = 0;

                break;
            }
            else if (pStorage->pendingFlush)
            {
                pStorage->pendingFlush = 0;
                pDecCont->decStat = H264DEC_NEW_HEADERS;
                returnValue = H264DEC_PENDING_FLUSH;
                strmLen = 0;

                //if(pDecCont->bMC)
                //{
                //    h264MCWaitPicReadyAll(pDecCont);
                //    h264bsdFlushDpb(pStorage->dpb);
                //}
#ifdef USE_OUTPUT_RELEASE
                if (pDecCont->pp.ppInstance == NULL)
                {
                    WaitListNotInUse(&pDecCont->fbList);
                    h264bsdFlushDpb(pStorage->dpb);
                }
#endif
                DEC_API_TRC
                ("H264DecDecode# H264DEC_PIC_DECODED (DPB flush caused by new SPS)\n");

                break;
            }

            /* Make sure that all frame buffers are not in use before
            * reseting DPB (i.e. all HW cores are idle and all output
            * processed) */
            //if(pDecCont->bMC)
            //    h264MCWaitPicReadyAll(pDecCont);
#ifdef USE_OUTPUT_RELEASE
            if (pDecCont->pp.ppInstance == NULL)
                WaitListNotInUse(&pDecCont->fbList);
#endif

            if (!h264SpsSupported(pDecCont))
            {
                pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;
                pStorage->activeViewSpsId[0] =
                    pStorage->activeViewSpsId[1] =
                        MAX_NUM_SEQ_PARAM_SETS;
                pStorage->picStarted = HANTRO_FALSE;
                pDecCont->decStat = H264DEC_INITIALIZED;
                pStorage->prevBufNotFinished = HANTRO_FALSE;
                pOutput->dataLeft = 0;

                //if(pDecCont->bMC)
                //{
                //    /* release buffer fully processed by SW */
                //    if(pDecCont->streamConsumedCallback.fn)
                //        pDecCont->streamConsumedCallback.fn((u8*)pInput->pStream,
                //                    (void*)pDecCont->streamConsumedCallback.pUserData);
                //}
                DEC_API_TRC
                ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED\n");
#ifdef USE_RANDOM_TEST
                ff_fwrite(pInput->pStream, 1, inputDataLen, pDecCont->ferrorStream);
                pDecCont->streamNotConsumed = 0;
#endif
                return H264DEC_STREAM_NOT_SUPPORTED;
            }
            else if ((h264bsdAllocateSwResources(pDecCont->dwl,
                                                 pStorage,
                                                 (pDecCont->
                                                  h264ProfileSupport == H264_HIGH_PROFILE) ? 1 :
                                                 0,
                                                 pDecCont->nCores) != 0) ||
                     (h264AllocateResources(pDecCont) != 0))
            {
                /* signal that decoder failed to init parameter sets */
                /* TODO: what about views? */
                pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;

                /* reset strmLen to force memfail return also for secondary
                 * view */
                strmLen = 0;

                returnValue = H264DEC_MEMFAIL;
                DEC_API_TRC("H264DecDecode# H264DEC_MEMFAIL\n");
            }
            else
            {
                if ((pStorage->activePps->numSliceGroups != 1) &&
                        (h264StreamIsBaseline(pDecCont) == 0))
                {
                    pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                    pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;

                    returnValue = H264DEC_STREAM_NOT_SUPPORTED;
                    DEC_API_TRC
                    ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED, FMO in Main/High Profile\n");
                }

                pDecCont->asicBuff->enableDmvAndPoc = 0;
                pStorage->dpb->interlaced = (pStorage->activeSps->frameMbsOnlyFlag == 0) ? 1 : 0;

                /* FMO always decoded in rlc mode */
                if ((pStorage->activePps->numSliceGroups != 1) &&
                        (pDecCont->rlcMode == 0))
                {
                    /* set to uninit state */
                    pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                    pStorage->activeViewSpsId[0] =
                        pStorage->activeViewSpsId[1] =
                            MAX_NUM_SEQ_PARAM_SETS;
                    pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;
                    pStorage->picStarted = HANTRO_FALSE;
                    pDecCont->decStat = H264DEC_INITIALIZED;

                    pDecCont->rlcMode = 1;
                    pStorage->prevBufNotFinished = HANTRO_FALSE;
                    DEC_API_TRC
                    ("H264DecDecode# H264DEC_ADVANCED_TOOLS, FMO\n");

                    returnValue = H264DEC_ADVANCED_TOOLS;
                }
                else
                {
                    u32 maxId = pDecCont->storage.dpb->noReordering ? 1 :
                                pDecCont->storage.dpb->dpbSize;

                    /* enable direct MV writing and POC tables for
                     * high/main streams.
                     * enable it also for any "baseline" stream which have
                     * main/high tools enabled */
                    if ((pStorage->activeSps->profileIdc > 66 &&
                            pStorage->activeSps->constrained_set0_flag == 0) ||
                            (h264StreamIsBaseline(pDecCont) == 0))
                    {
                        pDecCont->asicBuff->enableDmvAndPoc = 1;
                    }

                    /* increase number of buffers if multiview decoding */
                    if (pDecCont->storage.numViews)
                    {
                        maxId += pDecCont->storage.dpb->noReordering ? 1 :
                                 pDecCont->storage.dpb->dpbSize + 1;
                        maxId = MIN(maxId, 16);
                    }

                    /* reset multibuffer status */
                    if (pStorage->view == 0)
                        h264PpMultiInit(pDecCont, maxId);
                    else if (maxId > pDecCont->pp.multiMaxId)
                        h264PpMultiMvc(pDecCont, maxId);

                    DEC_API_TRC("H264DecDecode# H264DEC_HDRS_RDY\n");
                    returnValue = H264DEC_HDRS_RDY;
                }
            }

            if (!pStorage->view)
            {
                /* reset strmLen only for base view -> no HDRS_RDY to
                 * application when param sets activated for stereo view */
                strmLen = 0;
                pDecCont->storage.secondField = 0;
            }

            /* Initialize DPB mode */
            if (!pDecCont->storage.activeSps->frameMbsOnlyFlag &&
                    pDecCont->allowDpbFieldOrdering)
                pDecCont->dpbMode = DEC_DPB_INTERLACED_FIELD;
            else
                pDecCont->dpbMode = DEC_DPB_FRAME;

            /* Initialize tiled mode */
            if (pDecCont->tiledModeSupport &&
                    DecCheckTiledMode(pDecCont->tiledModeSupport,
                                      pDecCont->dpbMode,
                                      !pDecCont->storage.activeSps->frameMbsOnlyFlag) !=
                    HANTRO_OK)
            {
                returnValue = H264DEC_PARAM_ERROR;
                DEC_API_TRC
                ("H264DecDecode# H264DEC_PARAM_ERROR, tiled reference "\
                 "mode invalid\n");
            }

            /* reset reference addresses, this is important for multicore
             * as we use this list to track ref picture usage
             */
            DWLmemset(pDecCont->asicBuff->refPicList, 0,
                      sizeof(pDecCont->asicBuff->refPicList));

            break;
        }
#else
        case H264BSD_HDRS_RDY:
        {
            u32 dpbRealloc;
            pDecCont->storage.dpb->bUpdated = 0;
            pDecCont->storage.dpb->nExtBufSizeAdded = pDecCont->nExtBufSize;
            pDecCont->storage.dpb->useAdaptiveBuffers = pDecCont->useAdaptiveBuffers;
            pDecCont->storage.dpb->nGuardSize = pDecCont->nGuardSize;
            pDecCont->picNumber = 0;

            dpbRealloc = IsDpbRealloc(pDecCont);

            h264CheckReleasePpAndHw(pDecCont);

            pDecCont->storage.multiBuffPp =
                pDecCont->pp.ppInstance != NULL &&
                pDecCont->pp.ppInfo.multiBuffer;

            if (pStorage->dpb->flushed && pStorage->dpb->numOut)
            {
                /* output first all DPB stored pictures */
                pStorage->dpb->flushed = 0;
                pDecCont->decStat = H264DEC_NEW_HEADERS;
                /* if display smoothing used -> indicate that all pictures
                * have to be read out */
                if ((pStorage->dpb->totBuffers >
                        pStorage->dpb->dpbSize + 1) &&
                        !pDecCont->storage.multiBuffPp)
                {
                    returnValue = H264DEC_PENDING_FLUSH;
                }
                else
                {
                    returnValue = H264DEC_PIC_DECODED;
                }

                /* base view -> flush another view dpb */
                if (pDecCont->storage.numViews &&
                        pDecCont->storage.view == 0)
                {
                    h264bsdFlushDpb(pStorage->dpbs[1]);
                }
                DEC_API_TRC
                ("H264DecDecode# H264DEC_PIC_DECODED (DPB flush caused by new SPS)\n");
                strmLen = 0;

                break;
            }
            else if ((pStorage->dpb->totBuffers >
                      pStorage->dpb->dpbSize + 1) && pStorage->dpb->numOut)
            {
                /* flush buffered output for display smoothing */
                pStorage->dpb->delayedOut = 0;
                pStorage->secondField = 0;
                pDecCont->decStat = H264DEC_NEW_HEADERS;
                returnValue = H264DEC_PENDING_FLUSH;
                strmLen = 0;

                break;
            }
            else if (pStorage->pendingFlush)
            {
                pStorage->pendingFlush = 0;
                pDecCont->decStat = H264DEC_NEW_HEADERS;
                returnValue = H264DEC_PENDING_FLUSH;
                strmLen = 0;

                //if(pDecCont->bMC)
                //{
                //    h264MCWaitPicReadyAll(pDecCont);
                //    h264bsdFlushDpb(pStorage->dpb);
                //}
#ifdef USE_OUTPUT_RELEASE
                if (pDecCont->pp.ppInstance == NULL)
                {
                    WaitListNotInUse(&pDecCont->fbList);
                    h264bsdFlushDpb(pStorage->dpb);
                }
#endif
                DEC_API_TRC
                ("H264DecDecode# H264DEC_PIC_DECODED (DPB flush caused by new SPS)\n");

                break;
            }

            if (dpbRealloc)
            {
                /* Make sure that all frame buffers are not in use before
                * reseting DPB (i.e. all HW cores are idle and all output
                * processed) */
                //if(pDecCont->bMC)
                //    h264MCWaitPicReadyAll(pDecCont);
#ifdef USE_OUTPUT_RELEASE
                if (pDecCont->pp.ppInstance == NULL)
                {
#if defined(USE_EXTERNAL_BUFFER) && !defined(H264_EXT_BUF_SAFE_RELEASE)
                    int i;
                    pthread_mutex_lock(&pDecCont->fbList.ref_count_mutex);
                    for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++)
                        pDecCont->fbList.fbStat[i].nRefCount = 0;
                    pthread_mutex_unlock(&pDecCont->fbList.ref_count_mutex);
#endif
                    WaitListNotInUse(&pDecCont->fbList);
                    WaitOutputEmpty(&pDecCont->fbList);
                }
#endif
            }

            if (!h264SpsSupported(pDecCont))
            {
                pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;
                pStorage->activeViewSpsId[0] =
                    pStorage->activeViewSpsId[1] =
                        MAX_NUM_SEQ_PARAM_SETS;
                pStorage->picStarted = HANTRO_FALSE;
                pDecCont->decStat = H264DEC_INITIALIZED;
                pStorage->prevBufNotFinished = HANTRO_FALSE;
                pOutput->dataLeft = 0;

                //if(pDecCont->bMC)
                //{
                //    /* release buffer fully processed by SW */
                //    if(pDecCont->streamConsumedCallback.fn)
                //        pDecCont->streamConsumedCallback.fn((u8*)pInput->pStream,
                //                    (void*)pDecCont->streamConsumedCallback.pUserData);
                //}

#ifdef USE_RANDOM_TEST
                ff_fwrite(pInput->pStream, 1, inputDataLen, pDecCont->ferrorStream);
                pDecCont->streamNotConsumed = 0;
#endif

                DEC_API_TRC
                ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED\n");
                return H264DEC_STREAM_NOT_SUPPORTED;
            }
            else if (dpbRealloc)
            {
                if (pDecCont->bMVC == 0)
                    H264SetExternalBufferInfo(pDecCont, pStorage);
                else if (pDecCont->bMVC == 1)
                {
                    H264SetMVCExternalBufferInfo(pDecCont, pStorage);
                }
                decResult = H264BSD_BUFFER_NOT_READY;
                pDecCont->decStat = H264DEC_WAITING_FOR_BUFFER;
                strmLen = 0;
                PushOutputPic(&pDecCont->fbList, NULL, -2);
                returnValue = H264DEC_HDRS_RDY;
            }
            else
            {
                decResult = H264BSD_BUFFER_NOT_READY;
                pDecCont->decStat = H264DEC_WAITING_FOR_BUFFER;
                /* Need to exit the loop give a chance to call FinalizeOutputAll() */
                /* to output all the pending frames even when there is no need to */
                /* re-allocate external buffers. */
                strmLen = 0;
                returnValue = H264DEC_STRM_PROCESSED;
                break;
            }

            if (!pDecCont->storage.activeSps->frameMbsOnlyFlag &&
                    pDecCont->allowDpbFieldOrdering)
                pDecCont->dpbMode = DEC_DPB_INTERLACED_FIELD;
            else
                pDecCont->dpbMode = DEC_DPB_FRAME;
            break;
        }
        case H264BSD_BUFFER_NOT_READY:
        {
            u32 ret = HANTRO_OK;
            if (pDecCont->bMVC == 0)
                ret = h264bsdAllocateSwResources(pStorage,
                                                 (pDecCont->
                                                  h264ProfileSupport == H264_HIGH_PROFILE) ? 1 :
                                                 0,
                                                 pDecCont->nCores);
            else if (pDecCont->bMVC == 1)
            {
                ret = h264bsdMVCAllocateSwResources(pStorage,
                                                    (pDecCont->
                                                     h264ProfileSupport == H264_HIGH_PROFILE) ? 1 :
                                                    0,
                                                    pDecCont->nCores);
                pDecCont->bMVC = 2;
            }
            if (ret != HANTRO_OK) goto RESOURCE_NOT_READY;
            ret =  h264AllocateResources(pDecCont);
            if (ret != HANTRO_OK) goto RESOURCE_NOT_READY;

RESOURCE_NOT_READY:
            if (ret)
            {
                if (ret == H264DEC_WAITING_FOR_BUFFER)
                {
                    pDecCont->bufferIndex[0] = pDecCont->bufferIndex[1] = 0;
                    returnValue = ret;
                    strmLen = 0;
                    break;
                }
                else
                {
                    /* signal that decoder failed to init parameter sets */
                    /* TODO: what about views? */
                    pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                    pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;

                    /* reset strmLen to force memfail return also for secondary
                     * view */
                    strmLen = 0;

                    returnValue = H264DEC_MEMFAIL;
                    DEC_API_TRC("H264DecDecode# H264DEC_MEMFAIL\n");
                }
                strmLen = 0;
            }
            else
            {
                pDecCont->asicBuff->enableDmvAndPoc = 0;
                pStorage->dpb->interlaced = (pStorage->activeSps->frameMbsOnlyFlag == 0) ? 1 : 0;

                if ((pStorage->activePps->numSliceGroups != 1) &&
                        (h264StreamIsBaseline(pDecCont) == 0))
                {
                    pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                    pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;

                    returnValue = H264DEC_STREAM_NOT_SUPPORTED;
                    DEC_API_TRC
                    ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED, FMO in Main/High Profile\n");
                }
                else if ((pStorage->activePps->numSliceGroups != 1) && pDecCont->secureMode)
                {
                    pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                    pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;

                    returnValue = H264DEC_STRM_ERROR;
                    DEC_API_TRC
                    ("H264DecDecode# H264DEC_STREAM_ERROR, FMO in Secure Mode\n");
                }
                /* FMO always decoded in rlc mode */
                else if ((pStorage->activePps->numSliceGroups != 1) &&
                         (pDecCont->rlcMode == 0))
                {
                    /* set to uninit state */
                    pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                    pStorage->activeViewSpsId[0] =
                        pStorage->activeViewSpsId[1] =
                            MAX_NUM_SEQ_PARAM_SETS;
                    pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;
                    pStorage->picStarted = HANTRO_FALSE;
                    pDecCont->decStat = H264DEC_INITIALIZED;

                    pDecCont->rlcMode = 1;
                    pStorage->prevBufNotFinished = HANTRO_FALSE;
                    DEC_API_TRC
                    ("H264DecDecode# H264DEC_ADVANCED_TOOLS, FMO\n");

                    returnValue = H264DEC_ADVANCED_TOOLS;
                }
                else
                {
                    u32 maxId = pDecCont->storage.dpb->noReordering ? 1 :
                                pDecCont->storage.dpb->dpbSize;

                    /* enable direct MV writing and POC tables for
                     * high/main streams.
                     * enable it also for any "baseline" stream which have
                     * main/high tools enabled */
                    if ((pStorage->activeSps->profileIdc > 66 &&
                            pStorage->activeSps->constrained_set0_flag == 0) ||
                            (h264StreamIsBaseline(pDecCont) == 0))
                    {
                        pDecCont->asicBuff->enableDmvAndPoc = 1;
                    }

                    /* increase number of buffers if multiview decoding */
                    if (pDecCont->storage.numViews)
                    {
                        maxId += pDecCont->storage.dpb->noReordering ? 1 :
                                 pDecCont->storage.dpb->dpbSize + 1;
                        maxId = MIN(maxId, 16);
                    }

                    /* reset multibuffer status */
                    if (pStorage->view == 0)
                        h264PpMultiInit(pDecCont, maxId);
                    else if (maxId > pDecCont->pp.multiMaxId)
                        h264PpMultiMvc(pDecCont, maxId);

                    DEC_API_TRC("H264DecDecode# H264DEC_HDRS_RDY\n");
                }
            }

            if (!pStorage->view)
            {
                /* reset strmLen only for base view -> no HDRS_RDY to
                 * application when param sets activated for stereo view */
                strmLen = 0;
                pDecCont->storage.secondField = 0;
            }

            /* Initialize DPB mode */
            if (!pDecCont->storage.activeSps->frameMbsOnlyFlag &&
                    pDecCont->allowDpbFieldOrdering)
                pDecCont->dpbMode = DEC_DPB_INTERLACED_FIELD;
            else
                pDecCont->dpbMode = DEC_DPB_FRAME;

            /* Initialize tiled mode */
            if (pDecCont->tiledModeSupport &&
                    DecCheckTiledMode(pDecCont->tiledModeSupport,
                                      pDecCont->dpbMode,
                                      !pDecCont->storage.activeSps->frameMbsOnlyFlag) !=
                    HANTRO_OK)
            {
                returnValue = H264DEC_PARAM_ERROR;
                DEC_API_TRC
                ("H264DecDecode# H264DEC_PARAM_ERROR, tiled reference "\
                 "mode invalid\n");
            }

            /* reset reference addresses, this is important for multicore
             * as we use this list to track ref picture usage
             */
            DWLmemset(pDecCont->asicBuff->refPicList, 0,
                      sizeof(pDecCont->asicBuff->refPicList));
            pDecCont->decStat = H264DEC_INITIALIZED;
            break;
        }
#endif
        case H264BSD_PIC_RDY:
        {
            u32 asic_status;
            u32 pictureBroken;
            DecAsicBuffers_t *pAsicBuff = pDecCont->asicBuff;

            pictureBroken = (pStorage->pictureBroken && pStorage->intraFreeze &&
                             !IS_IDR_NAL_UNIT(pStorage->prevNalUnit));

            /* frame number workaround */
            if (!pDecCont->rlcMode && pDecCont->workarounds.h264.frameNum &&
                    !pDecCont->secureMode)
            {
                u32 tmp;
                pDecCont->forceNalMode =
                    h264bsdFixFrameNum((u8 *)tmpStream - numReadBytes,
                                       strmLen + numReadBytes,
                                       pStorage->sliceHeader->frameNum,
                                       pStorage->activeSps->maxFrameNum, &tmp);

                /* stuff skipped before slice start code */
                if (pDecCont->forceNalMode && tmp > 0)
                {
                    pDecCont->pHwStreamStart += tmp;
                    pDecCont->hwStreamStartBus += tmp;
                    pDecCont->hwLength -= tmp;
                }
            }

            if (pDecCont->decStat != H264DEC_BUFFER_EMPTY && !pictureBroken)
            {
                /* setup the reference frame list; just at picture start */
                dpbStorage_t *dpb = pStorage->dpb;
                dpbPicture_t *buffer = dpb->buffer;

                /* list in reorder */
                u32 i;

                if (!h264PpsSupported(pDecCont))
                {
                    pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                    pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;

                    returnValue = H264DEC_STREAM_NOT_SUPPORTED;
                    DEC_API_TRC
                    ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED, Main/High Profile tools detected\n");
                    goto end;
                }

                if ((pDecCont->is8190 == 0) && (pDecCont->rlcMode == 0))
                {
                    for (i = 0; i < dpb->dpbSize; i++)
                    {
                        pAsicBuff->refPicList[i] =
                            buffer[dpb->list[i]].data->busAddress;
                    }
                }
                else
                {
                    for (i = 0; i < dpb->dpbSize; i++)
                    {
                        pAsicBuff->refPicList[i] =
                            buffer[i].data->busAddress;
                    }
                }

                /* Multicore: increment usage for DPB buffers */
                IncrementDPBRefCount(dpb);

                pAsicBuff->maxRefFrm = dpb->maxRefFrames;
                pAsicBuff->outBuffer = pStorage->currImage->data;

                pAsicBuff->chromaQpIndexOffset =
                    pStorage->activePps->chromaQpIndexOffset;
                pAsicBuff->chromaQpIndexOffset2 =
                    pStorage->activePps->chromaQpIndexOffset2;
                pAsicBuff->filterDisable = 0;

                h264bsdDecodePicOrderCnt(pStorage->poc,
                                         pStorage->activeSps,
                                         pStorage->sliceHeader,
                                         pStorage->prevNalUnit);

#ifdef ENABLE_DPB_RECOVER
                if (IS_IDR_NAL_UNIT(pStorage->prevNalUnit))
                    pStorage->dpb->tryRecoverDpb = HANTRO_FALSE;

                if (pStorage->dpb->tryRecoverDpb
                        /* && pStorage->sliceHeader->firstMbInSlice */
                        && IS_I_SLICE(pStorage->sliceHeader->sliceType)
                        && 2 * pStorage->dpb->maxRefFrames <= pStorage->dpb->maxFrameNum)
                    h264DpbRecover(pStorage->dpb, pStorage->sliceHeader->frameNum,
                                   MIN(pStorage->poc->picOrderCnt[0], pStorage->poc->picOrderCnt[1]));
#endif
                if (pDecCont->rlcMode)
                {
                    if (pStorage->numConcealedMbs == pStorage->picSizeInMbs)
                    {
                        pAsicBuff->wholePicConcealed = 1;
                        pAsicBuff->filterDisable = 1;
                        pAsicBuff->chromaQpIndexOffset = 0;
                        pAsicBuff->chromaQpIndexOffset2 = 0;
                    }
                    else
                    {
                        pAsicBuff->wholePicConcealed = 0;
                    }

                    PrepareIntra4x4ModeData(pStorage, pAsicBuff);
                    PrepareMvData(pStorage, pAsicBuff);
                    PrepareRlcCount(pStorage, pAsicBuff);
                }
                else
                {
                    H264SetupVlcRegs(pDecCont);
                }

                H264ErrorRecover(pDecCont);

                DEBUG_PRINT(("Save DPB status\n"));
                /* we trust our memcpy; ignore return value */
                (void) DWLmemcpy(&pStorage->dpb[1], &pStorage->dpb[0],
                                 sizeof(*pStorage->dpb));

                DEBUG_PRINT(("Save POC status\n"));
                (void) DWLmemcpy(&pStorage->poc[1], &pStorage->poc[0],
                                 sizeof(*pStorage->poc));

                h264bsdCroppingParams(pStorage,
                                      &pStorage->dpb->currentOut->crop);

                /* create output picture list */
                h264UpdateAfterPictureDecode(pDecCont);

                /* enable output writing by default */
                pDecCont->asicBuff->disableOutWriting = 0;

                /* prepare PP if needed */
                h264PreparePpRun(pDecCont);
            }
            else
            {
                pDecCont->decStat = H264DEC_INITIALIZED;
            }

            /* disallow frame-mode DPB and tiled mode when decoding interlaced
             * content */
            if (pDecCont->dpbMode == DEC_DPB_FRAME &&
                    pDecCont->storage.activeSps &&
                    !pDecCont->storage.activeSps->frameMbsOnlyFlag &&
                    pDecCont->tiledReferenceEnable)
            {
                DEC_API_TRC("DPB mode does not support tiled reference "\
                            "pictures");
                return H264DEC_STRM_ERROR;
            }

            if (pDecCont->storage.partialFreeze)
            {
                PreparePartialFreeze((u8 *)pStorage->currImage->data->virtualAddress,
                                     h264bsdPicWidth(&pDecCont->storage),
                                     h264bsdPicHeight(&pDecCont->storage));
            }

            /* run asic and react to the status */
            if (!pictureBroken)
            {
                asic_status = H264RunAsic(pDecCont, pAsicBuff);
            }
            else
            {
                if (pDecCont->storage.picStarted)
                {
                    if (!pStorage->sliceHeader->fieldPicFlag ||
                            !pStorage->secondField)
                    {
                        h264InitPicFreezeOutput(pDecCont, 0);
                        h264UpdateAfterPictureDecode(pDecCont);
                    }
                }
                asic_status = DEC_8190_IRQ_ERROR;
            }

            if (pStorage->view)
                pStorage->nonInterViewRef = 0;

            /* Handle system error situations */
            if (asic_status == X170_DEC_TIMEOUT)
            {
                /* This timeout is DWL(software/os) generated */
                DEC_API_TRC
                ("H264DecDecode# H264DEC_HW_TIMEOUT, SW generated\n");
                return H264DEC_HW_TIMEOUT;
            }
            else if (asic_status == X170_DEC_SYSTEM_ERROR)
            {
                DEC_API_TRC("H264DecDecode# H264DEC_SYSTEM_ERROR\n");
                return H264DEC_SYSTEM_ERROR;
            }
            else if (asic_status == X170_DEC_HW_RESERVED)
            {
                DEC_API_TRC("H264DecDecode# H264DEC_HW_RESERVED\n");
                return H264DEC_HW_RESERVED;
            }

            /* Handle possible common HW error situations */
            if (asic_status & DEC_8190_IRQ_BUS)
            {
                pOutput->pStrmCurrPos = (u8 *) pInput->pStream;
                pOutput->strmCurrBusAddress = pInput->streamBusAddress;
                pOutput->dataLeft = inputDataLen;

#ifdef USE_RANDOM_TEST
                pDecCont->streamNotConsumed = 1;
#endif

                DEC_API_TRC("H264DecDecode# H264DEC_HW_BUS_ERROR\n");
                return H264DEC_HW_BUS_ERROR;
            }
            else if (asic_status &
                     (DEC_8190_IRQ_TIMEOUT | DEC_8190_IRQ_ABORT))
            {
                /* This timeout is HW generated */
                DEBUG_PRINT(("IRQ: HW %s\n",
                             (asic_status & DEC_8190_IRQ_TIMEOUT) ?
                             "TIMEOUT" : "ABORT"));

#ifdef H264_TIMEOUT_ASSERT
                ASSERT(0);
#endif
                if (pDecCont->packetDecoded != HANTRO_TRUE)
                {
                    DEBUG_PRINT(("reset picStarted\n"));
                    pDecCont->storage.picStarted = HANTRO_FALSE;
                }

                /* for concealment after a HW error report we use the saved reference list */
                if (pDecCont->storage.partialFreeze)
                {
                    dpbStorage_t *dpbPartial = &pDecCont->storage.dpb[1];
                    do
                    {
                        refData = h264bsdGetRefPicDataVlcMode(dpbPartial,
                                                              dpbPartial->list[index], 0);
                        index++;
                    }
                    while (index < 16 && refData == NULL);
                }

                if (!pDecCont->storage.partialFreeze ||
                        !ProcessPartialFreeze((u8 *)pStorage->currImage->data->virtualAddress,
                                              refData,
                                              h264bsdPicWidth(&pDecCont->storage),
                                              h264bsdPicHeight(&pDecCont->storage),
                                              pDecCont->storage.partialFreeze == 1))
                {
                    pDecCont->storage.pictureBroken = HANTRO_TRUE;
                    h264InitPicFreezeOutput(pDecCont, 1);
                }
                else
                {
                    asic_status &= ~DEC_8190_IRQ_TIMEOUT;
                    asic_status |= DEC_8190_IRQ_RDY;
                    pDecCont->storage.pictureBroken = HANTRO_FALSE;
                }

                /* PP has to run again for the concealed picture */
                if (pDecCont->pp.ppInstance != NULL)
                {
                    TRACE_PP_CTRL
                    ("H264DecDecode: Concealed picture, PP should run again\n");
                    pDecCont->pp.decPpIf.ppStatus = DECPP_IDLE;

                    if (pDecCont->pp.ppInfo.multiBuffer != 0)
                    {
                        if (pDecCont->pp.decPpIf.usePipeline != 0)
                        {
                            /* remove pipelined pic from PP list */
                            h264PpMultiRemovePic(pDecCont, pStorage->currImage->data);
                        }

                        if (pDecCont->pp.queuedPicToPp[pStorage->view] ==
                                pStorage->currImage->data)
                        {
                            /* current picture cannot be in the queue */
                            pDecCont->pp.queuedPicToPp[pStorage->view] =
                                NULL;
                        }
                    }
                }

                if (!pDecCont->rlcMode)
                {
                    strmData_t *strm = pDecCont->storage.strm;
                    const u8 *next =
                        h264bsdFindNextStartCode(strm->pStrmBuffStart,
                                                 strm->strmBuffSize);

                    if (next != NULL)
                    {
                        u32 consumed;

                        tmpStream -= numReadBytes;
                        strmLen += numReadBytes;

                        consumed = (u32)(next - tmpStream);
                        tmpStream += consumed;
                        strmLen -= consumed;
                    }
                }

                pDecCont->streamPosUpdated = 0;
                pDecCont->picNumber++;

                pDecCont->packetDecoded = HANTRO_FALSE;
                pStorage->skipRedundantSlices = HANTRO_TRUE;

                /* Remove this NAL unit from stream */
                strmLen = 0;
                DEC_API_TRC("H264DecDecode# H264DEC_PIC_DECODED\n");
                returnValue = H264DEC_PIC_DECODED;
                break;
            }

            if (pDecCont->rlcMode)
            {
                if (asic_status & DEC_8190_IRQ_ERROR)
                {
                    DEBUG_PRINT
                    (("H264DecDecode# IRQ_STREAM_ERROR in RLC mode)!\n"));
                }

                /* It was rlc mode, but switch back to vlc when allowed */
                if (pDecCont->tryVlc)
                {
                    pStorage->prevBufNotFinished = HANTRO_FALSE;
                    DEBUG_PRINT(("H264DecDecode: RLC mode used, but try VLC again\n"));
                    /* start using VLC mode again */
                    pDecCont->rlcMode = 0;
                    pDecCont->tryVlc = 0;
                    pDecCont->modeChange = 0;
                }

                pDecCont->picNumber++;

#ifdef FFWD_WORKAROUND
                pStorage->prevIdrPicReady =
                    IS_IDR_NAL_UNIT(pStorage->prevNalUnit);
#endif /* FFWD_WORKAROUND */



                DEC_API_TRC("H264DecDecode# H264DEC_PIC_DECODED\n");
                returnValue = H264DEC_PIC_DECODED;
                strmLen = 0;

                break;
            }

            /* from down here we handle VLC mode */

            /* in High/Main streams if HW model returns ASO interrupt, it
             * really means that there is a generic stream error. */
            if ((asic_status & DEC_8190_IRQ_ASO) &&
                    (pAsicBuff->enableDmvAndPoc != 0 ||
                     (h264StreamIsBaseline(pDecCont) == 0)))
            {
                DEBUG_PRINT(("ASO received in High/Main stream => STREAM_ERROR\n"));
                asic_status &= ~DEC_8190_IRQ_ASO;
                asic_status |= DEC_8190_IRQ_ERROR;
            }

            /* in Secure mode if HW model returns ASO interrupt, decoder treat
               it as error */
            if ((asic_status & DEC_8190_IRQ_ASO) && pDecCont->secureMode)
            {
                DEBUG_PRINT(("ASO received in secure mode => STREAM_ERROR\n"));
                asic_status &= ~DEC_8190_IRQ_ASO;
                asic_status |= DEC_8190_IRQ_ERROR;
            }

            /* Check for CABAC zero words here */
            if (asic_status & DEC_8190_IRQ_BUFFER)
            {
                if (pDecCont->storage.activePps->entropyCodingModeFlag &&
                        !pDecCont->secureMode)
                {
                    u32 tmp;
                    u32 checkWords = 1;
                    strmData_t strmTmp = *pDecCont->storage.strm;
                    tmp = pDecCont->pHwStreamStart - strmTmp.pStrmBuffStart;
                    strmTmp.pStrmCurrPos = pDecCont->pHwStreamStart;
                    strmTmp.strmBuffReadBits = 8 * tmp;
                    strmTmp.bitPosInWord = 0;
                    strmTmp.strmBuffSize = inputDataLen - (strmTmp.pStrmBuffStart - pInput->pStream);

                    tmp = GetDecRegister(pDecCont->h264Regs, HWIF_START_CODE_E);
                    /* In NAL unit mode, if NAL unit was of type
                     * "reserved" or sth other unsupported one, we need
                     * to skip zero word check. */
                    if (tmp == 0)
                    {
                        tmp = pInput->pStream[0] & 0x1F;
                        if (tmp != NAL_CODED_SLICE &&
                                tmp != NAL_CODED_SLICE_IDR)
                            checkWords = 0;
                    }

                    if (checkWords)
                    {
                        tmp = h264CheckCabacZeroWords(&strmTmp);
                        if (tmp != HANTRO_OK)
                        {
                            DEBUG_PRINT(("CABAC_ZERO_WORD error after packet => STREAM_ERROR\n"));
                        } /* cabacZeroWordError */
                    }
                }
            }

            /* Handle ASO */
            if (asic_status & DEC_8190_IRQ_ASO)
            {
                DEBUG_PRINT(("IRQ: ASO dedected\n"));
                ASSERT(pDecCont->rlcMode == 0);

                pDecCont->reallocate = 1;
                pDecCont->tryVlc = 1;
                pDecCont->modeChange = 1;

                /* restore DPB status */
                DEBUG_PRINT(("Restore DPB status\n"));

                /* remove any pictures marked for output */
                if (!pDecCont->storage.sei.bufferingPeriodInfo.existFlag || !pDecCont->storage.sei.picTimingInfo.existFlag)
                    RemoveTempOutputAll(&pDecCont->fbList);
                else
                    h264RemoveNoBumpOutput(&pStorage->dpb[0], (&pStorage->dpb[0])->numOut - (&pStorage->dpb[1])->numOut);

                /* we trust our memcpy; ignore return value */
                (void) DWLmemcpy(&pStorage->dpb[0], &pStorage->dpb[1],
                                 sizeof(dpbStorage_t));

                DEBUG_PRINT(("Restore POC status\n"));
                (void) DWLmemcpy(&pStorage->poc[0], &pStorage->poc[1],
                                 sizeof(*pStorage->poc));

                pStorage->skipRedundantSlices = HANTRO_FALSE;
                pStorage->asoDetected = 1;

                DEC_API_TRC("H264DecDecode# H264DEC_ADVANCED_TOOLS, ASO\n");
                returnValue = H264DEC_ADVANCED_TOOLS;

                /* PP has to run again for ASO picture */
                if (pDecCont->pp.ppInstance != NULL)
                {
                    TRACE_PP_CTRL
                    ("H264DecDecode: ASO detected, PP should run again\n");
                    pDecCont->pp.decPpIf.ppStatus = DECPP_IDLE;

                    if (pDecCont->pp.ppInfo.multiBuffer != 0)
                    {
                        if (pDecCont->pp.decPpIf.usePipeline != 0)
                        {
                            /* remove pipelined pic from PP list */
                            h264PpMultiRemovePic(pDecCont, pStorage->currImage->data);
                        }

                        if (pDecCont->pp.queuedPicToPp[pStorage->view] ==
                                pStorage->currImage->data)
                        {
                            /* current picture cannot be in the queue */
                            pDecCont->pp.queuedPicToPp[pStorage->view] =
                                NULL;
                        }
                    }
                }

                goto end;
            }
            else if (asic_status & DEC_8190_IRQ_BUFFER)
            {
                DEBUG_PRINT(("IRQ: BUFFER EMPTY\n"));

                /* a packet successfully decoded, don't reset picStarted flag if
                 * there is a need for rlc mode */
                pDecCont->decStat = H264DEC_BUFFER_EMPTY;
                pDecCont->packetDecoded = HANTRO_TRUE;
                pOutput->dataLeft = 0;

                if (pDecCont->forceNalMode)
                {
                    u32 tmp;
                    const u8 *next;

                    next =
                        h264bsdFindNextStartCode(pDecCont->pHwStreamStart,
                                                 pDecCont->hwLength);

                    if (next != NULL)
                    {
                        tmp = next - pDecCont->pHwStreamStart;
                        pDecCont->pHwStreamStart += tmp;
                        pDecCont->hwStreamStartBus += tmp;
                        pDecCont->hwLength -= tmp;
                        tmpStream = pDecCont->pHwStreamStart;
                        strmLen = pDecCont->hwLength;
                        continue;
                    }
                }

#ifdef USE_RANDOM_TEST
                ff_fwrite(pInput->pStream, 1, inputDataLen, pDecCont->ferrorStream);
                pDecCont->streamNotConsumed = 0;
#endif

                DEC_API_TRC
                ("H264DecDecode# H264DEC_STRM_PROCESSED, give more data\n");
                return H264DEC_BUF_EMPTY;
            }
            /* Handle stream error detected in HW */
            else if (asic_status & DEC_8190_IRQ_ERROR)
            {
                DEBUG_PRINT(("IRQ: STREAM ERROR detected\n"));
                if (pDecCont->packetDecoded != HANTRO_TRUE)
                {
                    DEBUG_PRINT(("reset picStarted\n"));
                    pDecCont->storage.picStarted = HANTRO_FALSE;
                }
                {
                    strmData_t *strm = pDecCont->storage.strm;
                    const u8 *next =
                        h264bsdFindNextStartCode(strm->pStrmBuffStart,
                                                 strm->strmBuffSize);

                    if (next != NULL)
                    {
                        u32 consumed;

                        tmpStream -= numReadBytes;
                        strmLen += numReadBytes;

                        consumed = (u32)(next - tmpStream);
                        tmpStream += consumed;
                        strmLen -= consumed;
                    }
                }

                /* REMEMBER TO UPDATE(RESET) STREAM POSITIONS */
                ASSERT(pDecCont->rlcMode == 0);

                /* for concealment after a HW error report we use the saved reference list */
                if (pDecCont->storage.partialFreeze)
                {
                    dpbStorage_t *dpbPartial = &pDecCont->storage.dpb[1];
                    do
                    {
                        refData = h264bsdGetRefPicDataVlcMode(dpbPartial,
                                                              dpbPartial->list[index], 0);
                        index++;
                    }
                    while (index < 16 && refData == NULL);
                }

                if (!pDecCont->storage.partialFreeze ||
                        !ProcessPartialFreeze((u8 *)pStorage->currImage->data->virtualAddress,
                                              refData,
                                              h264bsdPicWidth(&pDecCont->storage),
                                              h264bsdPicHeight(&pDecCont->storage),
                                              pDecCont->storage.partialFreeze == 1))
                {
                    pDecCont->storage.pictureBroken = HANTRO_TRUE;
                    h264InitPicFreezeOutput(pDecCont, 1);
                }
                else
                {
                    asic_status &= ~DEC_8190_IRQ_ERROR;
                    asic_status |= DEC_8190_IRQ_RDY;
                    pDecCont->storage.pictureBroken = HANTRO_FALSE;
                }

                /* PP has to run again for the concealed picture */
                if (pDecCont->pp.ppInstance != NULL)
                {

                    TRACE_PP_CTRL
                    ("H264DecDecode: Concealed picture, PP should run again\n");
                    pDecCont->pp.decPpIf.ppStatus = DECPP_IDLE;

                    if (pDecCont->pp.ppInfo.multiBuffer != 0)
                    {
                        if (pDecCont->pp.decPpIf.usePipeline != 0)
                        {
                            /* remove pipelined pic from PP list */
                            h264PpMultiRemovePic(pDecCont, pStorage->currImage->data);
                        }

                        if (pDecCont->pp.queuedPicToPp[pStorage->view] ==
                                pStorage->currImage->data)
                        {
                            /* current picture cannot be in the queue */
                            pDecCont->pp.queuedPicToPp[pStorage->view] =
                                NULL;
                        }
                    }
                }

                /* HW returned stream position is not valid in this case */
                pDecCont->streamPosUpdated = 0;
            }
            else /* OK in here */
            {
                if (IS_IDR_NAL_UNIT(pStorage->prevNalUnit))
                {
                    pDecCont->storage.pictureBroken = HANTRO_FALSE;
                }
            }

            if (pDecCont->storage.activePps->entropyCodingModeFlag &&
                    (asic_status & DEC_8190_IRQ_ERROR) == 0 && !pDecCont->secureMode)
            {
                u32 tmp;

                strmData_t strmTmp = *pDecCont->storage.strm;
                tmp = pDecCont->pHwStreamStart - strmTmp.pStrmBuffStart;
                strmTmp.pStrmCurrPos = pDecCont->pHwStreamStart;
                strmTmp.strmBuffReadBits = 8 * tmp;
                strmTmp.bitPosInWord = 0;
                strmTmp.strmBuffSize = inputDataLen - (strmTmp.pStrmBuffStart - pInput->pStream);
                tmp = h264CheckCabacZeroWords(&strmTmp);
                if (tmp != HANTRO_OK)
                {
                    DEBUG_PRINT(("Error decoding CABAC zero words\n"));
                    {
                        strmData_t *strm = pDecCont->storage.strm;
                        const u8 *next =
                            h264bsdFindNextStartCode(strm->pStrmBuffStart,
                                                     strm->strmBuffSize);

                        if (next != NULL)
                        {
                            u32 consumed;

                            tmpStream -= numReadBytes;
                            strmLen += numReadBytes;

                            consumed = (u32)(next - tmpStream);
                            tmpStream += consumed;
                            strmLen -= consumed;
                        }
                    }

                    ASSERT(pDecCont->rlcMode == 0);
                }
                else
                {
                    i32 remain = inputDataLen - (strmTmp.pStrmCurrPos - pInput->pStream);

                    /* byte stream format if starts with 0x000001 or 0x000000 */
                    if (remain > 3 && strmTmp.pStrmCurrPos[0] == 0x00 &&
                            strmTmp.pStrmCurrPos[1] == 0x00 &&
                            (strmTmp.pStrmCurrPos[2] & 0xFE) == 0x00)
                    {
                        const u8 *next =
                            h264bsdFindNextStartCode(strmTmp.pStrmCurrPos, remain);

                        u32 consumed;
                        if (next != NULL)
                        {
                            consumed = (u32)(next - pInput->pStream);
                        }
                        else
                        {
                            consumed = inputDataLen;
                        }

                        if (consumed != 0)
                        {
                            pDecCont->streamPosUpdated = 0;
                            tmpStream = pInput->pStream + consumed;
                        }
                    }
                }
            }

            if (pDecCont->secureMode)
            {
                strmData_t *strm = pDecCont->storage.strm;
                const u8 *next =
                    h264bsdFindNextStartCode(strm->pStrmBuffStart,
                                             strm->strmBuffSize);

                if (next != NULL)
                {
                    u32 consumed;

                    tmpStream -= numReadBytes;
                    strmLen += numReadBytes;

                    consumed = (u32)(next - tmpStream);
                    tmpStream += consumed;
                    strmLen -= consumed;
                }
                else
                {
                    tmpStream = pInput->pStream + inputDataLen;
                    strmLen = 0;
                }

                pDecCont->streamPosUpdated = 0;
            }

            /* For the switch between modes */
            /* this is a sign for RLC mode + mb error conceal NOT to reset picStarted flag */
            pDecCont->packetDecoded = HANTRO_FALSE;

            DEBUG_PRINT(("Skip redundant VLC\n"));
            pStorage->skipRedundantSlices = HANTRO_TRUE;
            pDecCont->picNumber++;

#ifdef FFWD_WORKAROUND
            pStorage->prevIdrPicReady =
                IS_IDR_NAL_UNIT(pStorage->prevNalUnit);
#endif /* FFWD_WORKAROUND */

            returnValue = H264DEC_PIC_DECODED;
            strmLen = 0;
            break;
        }
        case H264BSD_PARAM_SET_ERROR:
        {
            if (!h264bsdCheckValidParamSets(&pDecCont->storage) &&
                    strmLen == 0)
            {
                DEC_API_TRC
                ("H264DecDecode# H264DEC_STRM_ERROR, Invalid parameter set(s)\n");
                returnValue = H264DEC_STRM_ERROR;
            }

            /* update HW buffers if VLC mode */
            if (!pDecCont->rlcMode)
            {
                pDecCont->hwLength -= numReadBytes;
                pDecCont->hwStreamStartBus = pInput->streamBusAddress +
                                             (u32)(tmpStream - pInput->pStream);

                pDecCont->pHwStreamStart = tmpStream;
            }

            /* If no active sps, no need to check */
            if (pStorage->activeSps && !h264SpsSupported(pDecCont))
            {
                pStorage->activeSpsId = MAX_NUM_SEQ_PARAM_SETS;
                pStorage->activePpsId = MAX_NUM_PIC_PARAM_SETS;
                pStorage->activeViewSpsId[0] =
                    pStorage->activeViewSpsId[1] =
                        MAX_NUM_SEQ_PARAM_SETS;
                pStorage->picStarted = HANTRO_FALSE;
                pDecCont->decStat = H264DEC_INITIALIZED;
                pStorage->prevBufNotFinished = HANTRO_FALSE;

                //if(pDecCont->bMC)
                //{
                //    /* release buffer fully processed by SW */
                //    if(pDecCont->streamConsumedCallback.fn)
                //        pDecCont->streamConsumedCallback.fn((u8*)pInput->pStream,
                //                    (void*)pDecCont->streamConsumedCallback.pUserData);
                //}
                DEC_API_TRC
                ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED\n");
                returnValue = H264DEC_STREAM_NOT_SUPPORTED;
                goto end;
            }

            break;
        }
        case H264BSD_NEW_ACCESS_UNIT:
        {
            h264CheckReleasePpAndHw(pDecCont);

            pDecCont->streamPosUpdated = 0;

            pDecCont->storage.pictureBroken = HANTRO_TRUE;
            h264InitPicFreezeOutput(pDecCont, 0);

            h264UpdateAfterPictureDecode(pDecCont);

            /* PP will run in H264DecNextPicture() for this concealed picture */

            DEC_API_TRC("H264DecDecode# H264DEC_PIC_DECODED, NEW_ACCESS_UNIT\n");
            returnValue = H264DEC_PIC_DECODED;

            pDecCont->picNumber++;
            strmLen = 0;

            break;
        }
        case H264BSD_FMO:  /* If picture parameter set changed and FMO detected */
        {
            DEBUG_PRINT(("FMO detected\n"));

            ASSERT(pDecCont->rlcMode == 0);
            ASSERT(pDecCont->reallocate == 1);

            /* tmpStream = pInput->pStream; */

            DEC_API_TRC("H264DecDecode# H264DEC_ADVANCED_TOOLS, FMO\n");
            returnValue = H264DEC_ADVANCED_TOOLS;

            strmLen = 0;
            break;
        }
        case H264BSD_UNPAIRED_FIELD:
        {
            /* unpaired field detected and PP still running (wait after
             * second field decoded) -> wait here */
            h264CheckReleasePpAndHw(pDecCont);

            DEC_API_TRC("H264DecDecode# H264DEC_PIC_DECODED, UNPAIRED_FIELD\n");
            returnValue = H264DEC_PIC_DECODED;

            strmLen = 0;
            break;
        }
#ifdef USE_OUTPUT_RELEASE
        case H264BSD_ABORTED:
            pDecCont->decStat = H264DEC_ABORTED;
            return H264DEC_ABORTED;
#endif
        case H264BSD_NONREF_PIC_SKIPPED:
            returnValue = H264DEC_NONREF_PIC_SKIPPED;
        /* fall through */
        default:   /* case H264BSD_ERROR, H264BSD_RDY */
        {
            pDecCont->hwLength -= numReadBytes;
            pDecCont->hwStreamStartBus = pInput->streamBusAddress +
                                         (u32)(tmpStream - pInput->pStream);

            pDecCont->pHwStreamStart = tmpStream;
        }
        }
    }
    while (strmLen);

end:

    /*  If Hw decodes stream, update stream buffers from "storage" */
    if (pDecCont->streamPosUpdated)
    {
        if (pDecCont->secureMode)
            pOutput->dataLeft = 0;
        else
        {
            pOutput->pStrmCurrPos = (u8 *) pDecCont->pHwStreamStart;
            pOutput->strmCurrBusAddress = pDecCont->hwStreamStartBus;
            pOutput->dataLeft = pDecCont->hwLength;
        }
    }
    else
    {
        /* else update based on SW stream decode stream values */
        u32 data_consumed = (u32)(tmpStream - pInput->pStream);

        pOutput->pStrmCurrPos = (u8 *) tmpStream;
        pOutput->strmCurrBusAddress = pInput->streamBusAddress + data_consumed;

        pOutput->dataLeft = inputDataLen - data_consumed;
    }
    if (pDecCont->storage.sei.bufferingPeriodInfo.existFlag && pDecCont->storage.sei.picTimingInfo.existFlag)
    {
        if (returnValue == H264DEC_PIC_DECODED && pDecCont->decStat != H264DEC_NEW_HEADERS)
        {
            pDecCont->storage.sei.computeTimeInfo.accessUnitSize = pOutput->pStrmCurrPos - pInput->pStream;
            pDecCont->storage.sei.bumpingFlag = 1;
        }
    }

    /* Workaround for too big dataLeft value from error stream */
    if (pOutput->dataLeft > inputDataLen)
    {
        pOutput->dataLeft = inputDataLen;
    }

#ifdef USE_RANDOM_TEST
    ff_fwrite(pInput->pStream, 1, (inputDataLen - pOutput->dataLeft), pDecCont->ferrorStream);

    if (pOutput->dataLeft == inputDataLen)
        pDecCont->streamNotConsumed = 1;
    else
        pDecCont->streamNotConsumed = 0;
#endif

    FinalizeOutputAll(&pDecCont->fbList);

    if (returnValue == H264DEC_PIC_DECODED)
    {
        pDecCont->gapsCheckedForThis = HANTRO_FALSE;
    }
#ifdef USE_OUTPUT_RELEASE
    if (pDecCont->pp.ppInstance == NULL)
    {
        u32 ret;
        H264DecPicture output;
        u32 flushAll = 0;

        if (returnValue == H264DEC_PENDING_FLUSH)
            flushAll = 1;

        //if(returnValue == H264DEC_PIC_DECODED || returnValue == H264DEC_PENDING_FLUSH)
        {
            do
            {
                ret = H264DecNextPicture_INTERNAL(pDecCont, &output, flushAll);
            }
            while (ret == H264DEC_PIC_RDY);
        }
    }
#endif
    //if(pDecCont->bMC)
    //{
    //    if(returnValue == H264DEC_PIC_DECODED || returnValue == H264DEC_PENDING_FLUSH)
    //    {
    //        h264MCPushOutputAll(pDecCont);
    //    }
    //    else if(pOutput->dataLeft == 0)
    //    {
    //        /* release buffer fully processed by SW */
    //        if(pDecCont->streamConsumedCallback.fn)
    //            pDecCont->streamConsumedCallback.fn((u8*)pInput->pStream,
    //                        (void*)pDecCont->streamConsumedCallback.pUserData);

    //     }
    //}
#ifdef USE_OUTPUT_RELEASE
    if (pDecCont->abort)
        return (H264DEC_ABORTED);
    else
#endif
        return (returnValue);
}

/*------------------------------------------------------------------------------
    Function name : H264DecGetAPIVersion
    Description   : Return the API version information

    Return type   : H264DecApiVersion
    Argument      : void
------------------------------------------------------------------------------*/
H264DecApiVersion H264DecGetAPIVersion(void)
{
    H264DecApiVersion ver;

    ver.major = H264DEC_MAJOR_VERSION;
    ver.minor = H264DEC_MINOR_VERSION;

    DEC_API_TRC("H264DecGetAPIVersion# OK\n");

    return ver;
}

/*------------------------------------------------------------------------------
    Function name : H264DecGetBuild
    Description   : Return the SW and HW build information

    Return type   : H264DecBuild
    Argument      : void
------------------------------------------------------------------------------*/
H264DecBuild H264DecGetBuild(void)
{
    H264DecBuild buildInfo;

    (void)DWLmemset(&buildInfo, 0, sizeof(buildInfo));

    buildInfo.swBuild = HANTRO_DEC_SW_BUILD;
    buildInfo.hwBuild = DWLReadAsicID();

    DWLReadAsicConfig(buildInfo.hwConfig);

    DEC_API_TRC("H264DecGetBuild# OK\n");

    return buildInfo;
}

/*------------------------------------------------------------------------------

    Function: H264DecNextPicture

        Functional description:
            Get next picture in display order if any available.

        Input:
            decInst     decoder instance.
            endOfStream force output of all buffered pictures

        Output:
            pOutput     pointer to output structure

        Returns:
            H264DEC_OK            no pictures available for display
            H264DEC_PIC_RDY       picture available for display
            H264DEC_PARAM_ERROR     invalid parameters
            H264DEC_NOT_INITIALIZED   decoder instance not initialized yet

------------------------------------------------------------------------------*/
H264DecRet H264DecNextPicture(H264DecInst decInst,
                              H264DecPicture *pOutput, u32 endOfStream)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;
    const dpbOutPicture_t *outPic = NULL;
    u32 ret;
    dpbStorage_t *outDpb;

    DEC_API_TRC("H264DecNextPicture#\n");

    if (decInst == NULL || pOutput == NULL)
    {
        DEC_API_TRC("H264DecNextPicture# ERROR: decInst or pOutput is NULL\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecNextPicture# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }

#ifdef USE_OUTPUT_RELEASE
    if (pDecCont->pp.ppInstance == NULL)
    {
        if (pDecCont->decStat == H264DEC_END_OF_STREAM &&
                IsOutputEmpty(&pDecCont->fbList))
        {
            DEC_API_TRC("H264DecNextPicture# H264DEC_END_OF_STREAM\n");
            return (H264DEC_END_OF_STREAM);
        }

        if ((ret = PeekOutputPic(&pDecCont->fbList, pOutput)))
        {
            if (ret == ABORT_MARKER)
            {
                DEC_API_TRC("H264DecNextPicture# H264DEC_ABORTED\n");
                return (H264DEC_ABORTED);
            }
            if (ret == FLUSH_MARKER)
            {
                DEC_API_TRC("H264DecNextPicture# H264DEC_FLUSHED\n");
                return (H264DEC_FLUSHED);
            }
            DEC_API_TRC("H264DecNextPicture# H264DEC_PIC_RDY\n");
            return (H264DEC_PIC_RDY);
        }
        else
        {
            DEC_API_TRC("H264DecNextPicture# H264DEC_OK\n");
            return (H264DEC_OK);
        }
    }
#endif

    if (endOfStream)
    {
        if (pDecCont->asicRunning)
        {
            /* stop HW */
            SetDecRegister(pDecCont->h264Regs, HWIF_DEC_IRQ_STAT, 0);
            SetDecRegister(pDecCont->h264Regs, HWIF_DEC_IRQ, 0);
            SetDecRegister(pDecCont->h264Regs, HWIF_DEC_E, 0);
            DWLDisableHW(pDecCont->dwl, pDecCont->coreID, 4 * 1,
                         pDecCont->h264Regs[1] | DEC_IRQ_DISABLE);

            /* Wait for PP to end also */
            if (pDecCont->pp.ppInstance != NULL &&
                    (pDecCont->pp.decPpIf.ppStatus == DECPP_RUNNING ||
                     pDecCont->pp.decPpIf.ppStatus == DECPP_PIC_NOT_FINISHED))
            {
                pDecCont->pp.decPpIf.ppStatus = DECPP_PIC_READY;

                TRACE_PP_CTRL("H264RunAsic: PP Wait for end\n");

                pDecCont->pp.PPDecWaitEnd(pDecCont->pp.ppInstance);

                TRACE_PP_CTRL("H264RunAsic: PP Finished\n");
            }

            DWLReleaseHw(pDecCont->dwl, pDecCont->coreID); /* release HW lock */

            /* Decrement usage for DPB buffers */
            DecrementDPBRefCount(&pDecCont->storage.dpb[1]);

            pDecCont->asicRunning = 0;
            pDecCont->decStat = H264DEC_INITIALIZED;
            h264InitPicFreezeOutput(pDecCont, 1);
        }
        else if (pDecCont->keepHwReserved)
        {
            DWLReleaseHw(pDecCont->dwl, pDecCont->coreID);
            pDecCont->keepHwReserved = 0;
        }
        /* only one field of last frame decoded, PP still running (wait after
         * second field decoded) -> wait here */
        if (pDecCont->pp.ppInstance != NULL &&
                pDecCont->pp.decPpIf.ppStatus == DECPP_PIC_NOT_FINISHED)
        {
            pDecCont->pp.decPpIf.ppStatus = DECPP_PIC_READY;
            pDecCont->pp.PPDecWaitEnd(pDecCont->pp.ppInstance);
        }

        h264bsdFlushBuffer(&pDecCont->storage);

        FinalizeOutputAll(&pDecCont->fbList);
    }

    /* pp and decoder running in parallel, decoder finished first field ->
     * decode second field and wait PP after that */
    if (pDecCont->pp.ppInstance != NULL &&
            pDecCont->pp.decPpIf.ppStatus == DECPP_PIC_NOT_FINISHED)
        return (H264DEC_OK);

    outDpb = pDecCont->storage.dpbs[pDecCont->storage.outView];

    /* if display order is the same as decoding order and PP is used and
     * cannot be used in pipeline (rotation) -> do not perform PP here but
     * while decoding next picture (parallel processing instead of
     * DEC followed by PP followed by DEC...) */
    if (pDecCont->storage.pendingOutPic)
    {
        outPic = pDecCont->storage.pendingOutPic;
        pDecCont->storage.pendingOutPic = NULL;
    }
    else if (outDpb->noReordering == 0)
    {
        if (!outDpb->delayedOut)
        {
            if (pDecCont->pp.ppInstance && pDecCont->pp.decPpIf.ppStatus ==
                    DECPP_PIC_READY)
                outDpb->noOutput = 0;

            pDecCont->storage.dpb =
                pDecCont->storage.dpbs[pDecCont->storage.outView];

            outPic = h264bsdNextOutputPicture(&pDecCont->storage);

            if ((pDecCont->storage.numViews ||
                    pDecCont->storage.outView) && outPic != NULL)
            {
                pOutput->viewId =
                    pDecCont->storage.viewId[pDecCont->storage.outView];
                pDecCont->storage.outView ^= 0x1;
            }
        }
    }
    else
    {
        /* no reordering of output pics AND stereo was activated after base
         * picture was output -> output stereo view pic if available */
        if (pDecCont->storage.numViews &&
                pDecCont->storage.view && pDecCont->storage.outView == 0 &&
                outDpb->numOut == 0 &&
                pDecCont->storage.dpbs[pDecCont->storage.view]->numOut > 0)
        {
            pDecCont->storage.outView ^= 0x1;
            outDpb = pDecCont->storage.dpbs[pDecCont->storage.outView];
        }

        if (outDpb->numOut > 1 || endOfStream ||
                pDecCont->storage.prevNalUnit->nalRefIdc == 0 ||
                pDecCont->pp.ppInstance == NULL || pDecCont->pp.decPpIf.usePipeline ||
                pDecCont->storage.view != pDecCont->storage.outView)
        {
            if (!endOfStream &&
                    ((outDpb->numOut == 1 && outDpb->delayedOut) ||
                     (pDecCont->storage.sliceHeader->fieldPicFlag &&
                      pDecCont->storage.secondField)))
            {
            }
            else
            {
                pDecCont->storage.dpb =
                    pDecCont->storage.dpbs[pDecCont->storage.outView];
                outPic = h264bsdNextOutputPicture(&pDecCont->storage);
                pOutput->viewId =
                    pDecCont->storage.viewId[pDecCont->storage.outView];
                if ((pDecCont->storage.numViews ||
                        pDecCont->storage.outView) && outPic != NULL)
                    pDecCont->storage.outView ^= 0x1;
            }
        }
    }

    if (outPic != NULL)
    {
        if (!pDecCont->storage.numViews)
            pOutput->viewId = 0;

        pOutput->pOutputPicture = outPic->data->virtualAddress;
        pOutput->outputPictureBusAddress = outPic->data->busAddress;
        pOutput->picId = outPic->picId;
        pOutput->picCodingType[0] = outPic->picCodeType[0];
        pOutput->picCodingType[1] = outPic->picCodeType[1];
        pOutput->isIdrPicture[0] = outPic->isIdr[0];
        pOutput->isIdrPicture[1] = outPic->isIdr[1];
        pOutput->decodeId[0] = outPic->decodeId[0];
        pOutput->decodeId[1] = outPic->decodeId[1];
        pOutput->nbrOfErrMBs = outPic->numErrMbs;

        pOutput->interlaced = outPic->interlaced;
        pOutput->fieldPicture = outPic->fieldPicture;
        pOutput->topField = outPic->topField;

        pOutput->picWidth = outPic->picWidth;
        pOutput->picHeight = outPic->picHeight;

        pOutput->outputFormat = outPic->tiledMode ?
                                DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;
        pOutput->picStruct = outPic->picStruct;

        /* Pending flush caused by new SPS. Decoded output pictures in DPB */
        /* may have different dimensions than defined in new headers. */
        /* Use dimensions from previous picture */
        if (endOfStream && (pDecCont->decStat == H264DEC_NEW_HEADERS) &&
                (pDecCont->pp.ppInstance != NULL))
        {
            pOutput->picWidth = pDecCont->storage.prevPicWidth;
            pOutput->picHeight = pDecCont->storage.prevPicHeight;
        }

        pDecCont->storage.prevPicWidth = pOutput->picWidth;
        pDecCont->storage.prevPicHeight = pOutput->picHeight;

        pOutput->cropParams = outPic->crop;

        if (pDecCont->pp.ppInstance != NULL && (pDecCont->pp.ppInfo.multiBuffer == 0))
        {
            /* single buffer legacy mode */
            DecPpInterface *decPpIf = &pDecCont->pp.decPpIf;

            if (decPpIf->ppStatus == DECPP_PIC_READY)
            {
                pDecCont->pp.decPpIf.ppStatus = DECPP_IDLE;
                TRACE_PP_CTRL
                ("H264DecNextPicture: PP already ran for this picture\n");
            }
            else
            {
                TRACE_PP_CTRL("H264DecNextPicture: PP has to run\n");

                decPpIf->usePipeline = 0;   /* we are in standalone mode */

                decPpIf->inwidth = pOutput->picWidth;
                decPpIf->inheight = pOutput->picHeight;
                decPpIf->croppedW = pOutput->picWidth;
                decPpIf->croppedH = pOutput->picHeight;
                decPpIf->tiledInputMode =
                    (pOutput->outputFormat == DEC_OUT_FRM_RASTER_SCAN) ? 0 : 1;
                decPpIf->progressiveSequence =
                    pDecCont->storage.activeSps->frameMbsOnlyFlag;

                if (pOutput->interlaced == 0)
                {
                    decPpIf->picStruct = DECPP_PIC_FRAME_OR_TOP_FIELD;
                }
                else
                {
                    u32 picStruct = DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
                    if (pDecCont->dpbMode == DEC_DPB_INTERLACED_FIELD)
                        picStruct = DECPP_PIC_TOP_AND_BOT_FIELD;

                    if (pOutput->fieldPicture == 0)
                    {
                        decPpIf->picStruct = picStruct;
                    }
                    else
                    {
                        /* TODO: missing field, is this OK? */
                        decPpIf->picStruct = picStruct;
                    }
                }

                decPpIf->inputBusLuma = pOutput->outputPictureBusAddress;
                decPpIf->inputBusChroma = decPpIf->inputBusLuma +
                                          pOutput->picWidth * pOutput->picHeight;

                if (decPpIf->picStruct != DECPP_PIC_FRAME_OR_TOP_FIELD)
                {
                    if (pDecCont->dpbMode == DEC_DPB_FRAME)
                    {
                        decPpIf->bottomBusLuma = decPpIf->inputBusLuma +
                                                 decPpIf->inwidth;
                        decPpIf->bottomBusChroma = decPpIf->inputBusChroma +
                                                   decPpIf->inwidth;
                    }
                    if (pDecCont->dpbMode == DEC_DPB_INTERLACED_FIELD)
                    {
                        decPpIf->bottomBusLuma = decPpIf->inputBusLuma +
                                                 (decPpIf->inwidth * decPpIf->inheight >> 1);
                        decPpIf->bottomBusChroma = decPpIf->inputBusChroma +
                                                   (decPpIf->inwidth * decPpIf->inheight >> 2);
                    }
                }
                else
                {
                    decPpIf->bottomBusLuma = (u32)(-1);
                    decPpIf->bottomBusChroma = (u32)(-1);
                }

                decPpIf->littleEndian =
                    GetDecRegister(pDecCont->h264Regs, HWIF_DEC_OUT_ENDIAN);
                decPpIf->wordSwap =
                    GetDecRegister(pDecCont->h264Regs, HWIF_DEC_OUTSWAP32_E);

                pDecCont->pp.PPDecStart(pDecCont->pp.ppInstance, decPpIf);

                TRACE_PP_CTRL("H264DecNextPicture: PP wait to be done\n");

                pDecCont->pp.PPDecWaitEnd(pDecCont->pp.ppInstance);

                TRACE_PP_CTRL("H264DecNextPicture: PP Finished\n");

            }
        }

        if (pDecCont->pp.ppInstance != NULL && (pDecCont->pp.ppInfo.multiBuffer != 0))
        {
            /* multibuffer mode */
            DecPpInterface *decPpIf = &pDecCont->pp.decPpIf;
            u32 id;

            if (decPpIf->ppStatus == DECPP_PIC_READY)
            {
                pDecCont->pp.decPpIf.ppStatus = DECPP_IDLE;
                TRACE_PP_CTRL("H264DecNextPicture: PP processed a picture\n");
            }

            id = h264PpMultiFindPic(pDecCont, outPic->data);
            if (id <= pDecCont->pp.multiMaxId)
            {
                TRACE_PP_CTRL("H264DecNextPicture: PPNextDisplayId = %d\n", id);
                TRACE_PP_CTRL("H264DecNextPicture: PP already ran for this picture\n");
                pDecCont->pp.PPNextDisplayId(pDecCont->pp.ppInstance, id);
                h264PpMultiRemovePic(pDecCont, outPic->data);
            }
            else
            {
                if (!endOfStream && GetFreeBufferCount(&pDecCont->fbList) &&
                        pDecCont->pp.queuedPicToPp[pDecCont->storage.view] ==
                        outPic->data &&
                        pDecCont->decStat != H264DEC_NEW_HEADERS &&
                        !pDecCont->pp.ppInfo.pipelineAccepted)
                {
                    pDecCont->storage.pendingOutPic = outPic;
                    return H264DEC_OK;
                }
                TRACE_PP_CTRL("H264DecNextPicture: PP has to run\n");

                id = h264PpMultiAddPic(pDecCont, outPic->data);

                ASSERT(id <= pDecCont->pp.multiMaxId);

                TRACE_PP_CTRL("H264RunAsic: PP Multibuffer index = %d\n", id);
                TRACE_PP_CTRL("H264DecNextPicture: PPNextDisplayId = %d\n", id);
                decPpIf->bufferIndex = id;
                decPpIf->displayIndex = id;
                h264PpMultiRemovePic(pDecCont, outPic->data);

                if (pDecCont->pp.queuedPicToPp[pDecCont->storage.view] == outPic->data)
                {
                    pDecCont->pp.queuedPicToPp[pDecCont->storage.view] = NULL; /* remove it from queue */
                }

                decPpIf->usePipeline = 0;   /* we are in standalone mode */

                decPpIf->inwidth = pOutput->picWidth;
                decPpIf->inheight = pOutput->picHeight;
                decPpIf->croppedW = pOutput->picWidth;
                decPpIf->croppedH = pOutput->picHeight;
                decPpIf->tiledInputMode =
                    (pOutput->outputFormat == DEC_OUT_FRM_RASTER_SCAN) ? 0 : 1;
                decPpIf->progressiveSequence =
                    pDecCont->storage.activeSps->frameMbsOnlyFlag;

                if (pOutput->interlaced == 0)
                {
                    decPpIf->picStruct = DECPP_PIC_FRAME_OR_TOP_FIELD;
                }
                else
                {
                    u32 picStruct = DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
                    if (pDecCont->dpbMode == DEC_DPB_INTERLACED_FIELD)
                        picStruct = DECPP_PIC_TOP_AND_BOT_FIELD;

                    if (pOutput->fieldPicture == 0)
                    {
                        decPpIf->picStruct = picStruct;
                    }
                    else
                    {
                        /* TODO: missing field, is this OK? */
                        decPpIf->picStruct = picStruct;
                    }
                }

                decPpIf->inputBusLuma = pOutput->outputPictureBusAddress;
                decPpIf->inputBusChroma = decPpIf->inputBusLuma +
                                          pOutput->picWidth * pOutput->picHeight;

                if (decPpIf->picStruct != DECPP_PIC_FRAME_OR_TOP_FIELD)
                {
                    if (pDecCont->dpbMode == DEC_DPB_FRAME)
                    {
                        decPpIf->bottomBusLuma = decPpIf->inputBusLuma +
                                                 decPpIf->inwidth;
                        decPpIf->bottomBusChroma = decPpIf->inputBusChroma +
                                                   decPpIf->inwidth;
                    }
                    if (pDecCont->dpbMode == DEC_DPB_INTERLACED_FIELD)
                    {
                        decPpIf->bottomBusLuma = decPpIf->inputBusLuma +
                                                 (decPpIf->inwidth * decPpIf->inheight >> 1);
                        decPpIf->bottomBusChroma = decPpIf->inputBusChroma +
                                                   (decPpIf->inwidth * decPpIf->inheight >> 2);
                    }
                }
                else
                {
                    decPpIf->bottomBusLuma = (u32)(-1);
                    decPpIf->bottomBusChroma = (u32)(-1);
                }

                decPpIf->littleEndian =
                    GetDecRegister(pDecCont->h264Regs, HWIF_DEC_OUT_ENDIAN);
                decPpIf->wordSwap =
                    GetDecRegister(pDecCont->h264Regs, HWIF_DEC_OUTSWAP32_E);

                pDecCont->pp.PPDecStart(pDecCont->pp.ppInstance, decPpIf);

                TRACE_PP_CTRL("H264DecNextPicture: PP wait to be done\n");

                pDecCont->pp.PPDecWaitEnd(pDecCont->pp.ppInstance);

                TRACE_PP_CTRL("H264DecNextPicture: PP Finished\n");
            }
        }

        if (pDecCont->pp.ppInfo.deinterlace)
        {
            pOutput->interlaced = 0;
            pOutput->fieldPicture = 0;
            pOutput->topField = 0;
        }

        ClearOutput(&pDecCont->fbList, outPic->memIdx);

        DEC_API_TRC("H264DecNextPicture# H264DEC_PIC_RDY\n");
        return (H264DEC_PIC_RDY);
    }
    else
    {
        DEC_API_TRC("H264DecNextPicture# H264DEC_OK\n");
        return (H264DEC_OK);
    }

}

/*------------------------------------------------------------------------------
    Function name : h264UpdateAfterPictureDecode
    Description   :

    Return type   : void
    Argument      : decContainer_t * pDecCont
------------------------------------------------------------------------------*/
void h264UpdateAfterPictureDecode(decContainer_t *pDecCont)
{

    u32 tmpRet = HANTRO_OK;
    storage_t *pStorage = &pDecCont->storage;
    sliceHeader_t *sliceHeader = pStorage->sliceHeader;
    u32 interlaced;
    u32 picCodeType;
    u32 secondField = 0;
    i32 poc;

    h264bsdResetStorage(pStorage);

    ASSERT((pStorage));

    /* determine initial reference picture lists */
    H264InitRefPicList(pDecCont);

    if (pStorage->sliceHeader->fieldPicFlag == 0)
        pStorage->currImage->picStruct = FRAME;
    else
        pStorage->currImage->picStruct = pStorage->sliceHeader->bottomFieldFlag;
    if (pStorage->currImage->picStruct < FRAME)
    {
        if (pStorage->dpb->currentOut->status[(u32)!pStorage->currImage->picStruct] != EMPTY)
            secondField = 1;
    }

    if (pStorage->poc->containsMmco5)
    {
        u32 tmp;

        tmp = MIN(pStorage->poc->picOrderCnt[0], pStorage->poc->picOrderCnt[1]);
        pStorage->poc->picOrderCnt[0] -= tmp;
        pStorage->poc->picOrderCnt[1] -= tmp;
    }

    pStorage->currentMarked = pStorage->validSliceInAccessUnit;

    /* Setup tiled mode for picture before updating DPB */
    interlaced = !pDecCont->storage.activeSps->frameMbsOnlyFlag;
    if (pDecCont->tiledModeSupport)
    {
        pDecCont->tiledReferenceEnable =
            DecSetupTiledReference(pDecCont->h264Regs,
                                   pDecCont->tiledModeSupport,
                                   pDecCont->dpbMode,
                                   interlaced);
    }
    else
    {
        pDecCont->tiledReferenceEnable = 0;
    }

    if (pStorage->validSliceInAccessUnit)
    {
        if (IS_I_SLICE(sliceHeader->sliceType))
            picCodeType = DEC_PIC_TYPE_I;
        else if (IS_P_SLICE(sliceHeader->sliceType))
            picCodeType = DEC_PIC_TYPE_P;
        else
            picCodeType = DEC_PIC_TYPE_B;
#ifdef SKIP_OPENB_FRAME
        if (!pDecCont->firstEntryPoint)
        {
            if (pStorage->currImage->picStruct < FRAME)
            {
                if (!secondField)
                {
                    pDecCont->entryIsIDR = IS_IDR_NAL_UNIT(pStorage->prevNalUnit);
                    pDecCont->entryIsI = (picCodeType == DEC_PIC_TYPE_I);
                }
                else
                {
                    pDecCont->entryPOC = MIN(pStorage->poc->picOrderCnt[0],
                                             pStorage->poc->picOrderCnt[1]);
                    if (pDecCont->entryIsI)
                        pDecCont->firstEntryPoint = 1;
                }
            }
            else
            {
                pDecCont->entryIsIDR = IS_IDR_NAL_UNIT(pStorage->prevNalUnit);
                pDecCont->entryIsI = (picCodeType == DEC_PIC_TYPE_I);
                pDecCont->entryPOC = MIN(pStorage->poc->picOrderCnt[0],
                                         pStorage->poc->picOrderCnt[1]);
                if (pDecCont->entryIsI)
                    pDecCont->firstEntryPoint = 1;
            }
        }

        if ((pDecCont->skipB < 2) && pDecCont->firstEntryPoint)
        {
            if (pStorage->currImage->picStruct < FRAME)
            {
                if (secondField)
                {
                    if ((picCodeType == DEC_PIC_TYPE_I) || (picCodeType == DEC_PIC_TYPE_P) ||
                            (pStorage->dpb->currentOut->picCodeType[(u32)!pStorage->currImage->picStruct] == DEC_PIC_TYPE_I) ||
                            (pStorage->dpb->currentOut->picCodeType[(u32)!pStorage->currImage->picStruct] == DEC_PIC_TYPE_P))
                        pDecCont->skipB++;
                    else
                    {
                        poc = MIN(pStorage->poc->picOrderCnt[0],
                                  pStorage->poc->picOrderCnt[1]);
                        if (!pDecCont->entryIsIDR && (poc < pDecCont->entryPOC))
                            pStorage->dpb->currentOut->openBFlag = 1;
                    }
                }
            }
            else
            {
                if ((picCodeType == DEC_PIC_TYPE_I) || (picCodeType == DEC_PIC_TYPE_P))
                    pDecCont->skipB++;
                else
                {
                    poc = MIN(pStorage->poc->picOrderCnt[0],
                              pStorage->poc->picOrderCnt[1]);
                    if (!pDecCont->entryIsIDR && (poc < pDecCont->entryPOC))
                        pStorage->dpb->currentOut->openBFlag = 1;
                }
            }
        }
#endif

        if (pStorage->prevNalUnit->nalRefIdc)
        {
            tmpRet = h264bsdMarkDecRefPic(pStorage->dpb,
                                          &sliceHeader->decRefPicMarking,
                                          pStorage->currImage,
                                          sliceHeader->frameNum,
                                          pStorage->poc->picOrderCnt,
                                          IS_IDR_NAL_UNIT(pStorage->prevNalUnit) ?
                                          HANTRO_TRUE : HANTRO_FALSE,
                                          pStorage->currentPicId,
                                          pStorage->numConcealedMbs,
                                          pDecCont->tiledReferenceEnable,
                                          picCodeType);
        }
        else
        {
            /* non-reference picture, just store for possible display
             * reordering */
            tmpRet = h264bsdMarkDecRefPic(pStorage->dpb, NULL,
                                          pStorage->currImage,
                                          sliceHeader->frameNum,
                                          pStorage->poc->picOrderCnt,
                                          HANTRO_FALSE,
                                          pStorage->currentPicId,
                                          pStorage->numConcealedMbs,
                                          pDecCont->tiledReferenceEnable,
                                          picCodeType);
        }

        if (tmpRet != HANTRO_OK && pStorage->view == 0)
            pStorage->secondField = 0;

        if (pStorage->dpb->delayedOut == 0)
        {
            h264DpbUpdateOutputList(pStorage->dpb);

            if (pStorage->view == 0)
                pStorage->lastBaseNumOut = pStorage->dpb->numOut;
            /* make sure that there are equal number of output pics available
             * for both views */
            else if (pStorage->dpb->numOut < pStorage->lastBaseNumOut)
                h264DpbAdjStereoOutput(pStorage->dpb, pStorage->lastBaseNumOut);
            else if (pStorage->lastBaseNumOut &&
                     pStorage->lastBaseNumOut + 1 < pStorage->dpb->numOut)
                h264DpbAdjStereoOutput(pStorage->dpbs[0],
                                       pStorage->dpb->numOut - 1);
            else if (pStorage->lastBaseNumOut == 0 && pStorage->dpb->numOut)
                h264DpbAdjStereoOutput(pStorage->dpbs[0],
                                       pStorage->dpb->numOut);

            /* check if currentOut already in output buffer and second
             * field to come -> delay output */
            if (pStorage->currImage->picStruct != FRAME &&
                    (pStorage->view == 0 ? pStorage->secondField :
                     !pStorage->baseOppositeFieldPic))
            {
                u32 i, tmp;

                tmp = pStorage->dpb->outIndexR;
                for (i = 0; i < pStorage->dpb->numOut; i++, tmp++)
                {
                    if (tmp == pStorage->dpb->dpbSize + 1)
                        tmp = 0;

                    if (pStorage->dpb->currentOut->data ==
                            pStorage->dpb->outBuf[tmp].data)
                    {
                        pStorage->dpb->delayedId = tmp;
                        DEBUG_PRINT(
                            ("h264UpdateAfterPictureDecode: Current frame in output list; "));
                        pStorage->dpb->delayedOut = 1;
                        break;
                    }
                }
            }
        }
        else
        {
            if (!pStorage->dpb->noReordering)
                h264DpbUpdateOutputList(pStorage->dpb);
            DEBUG_PRINT(
                ("h264UpdateAfterPictureDecode: Output all delayed pictures!\n"));
            pStorage->dpb->delayedOut = 0;
            pStorage->dpb->currentOut->toBeDisplayed = 0;   /* remove it from output list */
        }

    }
    else
    {
        pStorage->dpb->delayedOut = 0;
        pStorage->secondField = 0;
    }

    if ((pStorage->validSliceInAccessUnit && tmpRet == HANTRO_OK) ||
            pStorage->view)
        pStorage->nextView ^= 0x1;
    pStorage->picStarted = HANTRO_FALSE;
    pStorage->validSliceInAccessUnit = HANTRO_FALSE;
    pStorage->asoDetected = 0;
}

/*------------------------------------------------------------------------------
    Function name : h264SpsSupported
    Description   :

    Return type   : u32
    Argument      : const decContainer_t * pDecCont
------------------------------------------------------------------------------*/
u32 h264SpsSupported(const decContainer_t *pDecCont)
{
    const seqParamSet_t *sps = pDecCont->storage.activeSps;

    /* check picture size */
    if (sps->picWidthInMbs * 16 > pDecCont->maxDecPicWidth ||
            sps->picWidthInMbs < 3 || sps->picHeightInMbs < 3 ||
            (sps->picWidthInMbs * sps->picHeightInMbs) > ((4096 >> 4) * (4096 >> 4)))
    {
        DEBUG_PRINT(("Picture size not supported!\n"));
        return 0;
    }

    /* If tile mode is enabled, should take DTRC minimum size(96x48) into consideration */
    if (pDecCont->tiledModeSupport)
    {
        if (sps->picWidthInMbs < 6)
        {
            DEBUG_PRINT(("Picture size not supported!\n"));
            return 0;
        }
    }

    if (pDecCont->h264ProfileSupport == H264_BASELINE_PROFILE)
    {
        if (sps->frameMbsOnlyFlag != 1)
        {
            DEBUG_PRINT(("INTERLACED!!! Not supported in baseline decoder\n"));
            return 0;
        }
        if (sps->chromaFormatIdc != 1)
        {
            DEBUG_PRINT(("CHROMA!!! Only 4:2:0 supported in baseline decoder\n"));
            return 0;
        }
        if (sps->scalingMatrixPresentFlag != 0)
        {
            DEBUG_PRINT(("SCALING Matrix!!! Not supported in baseline decoder\n"));
            return 0;
        }
    }

    return 1;
}

/*------------------------------------------------------------------------------
    Function name : h264PpsSupported
    Description   :

    Return type   : u32
    Argument      : const decContainer_t * pDecCont
------------------------------------------------------------------------------*/
u32 h264PpsSupported(const decContainer_t *pDecCont)
{
    const picParamSet_t *pps = pDecCont->storage.activePps;

    if (pDecCont->h264ProfileSupport == H264_BASELINE_PROFILE)
    {
        if (pps->entropyCodingModeFlag != 0)
        {
            DEBUG_PRINT(("CABAC!!! Not supported in baseline decoder\n"));
            return 0;
        }
        if (pps->weightedPredFlag != 0 || pps->weightedBiPredIdc != 0)
        {
            DEBUG_PRINT(("WEIGHTED Pred!!! Not supported in baseline decoder\n"));
            return 0;
        }
        if (pps->transform8x8Flag != 0)
        {
            DEBUG_PRINT(("TRANSFORM 8x8!!! Not supported in baseline decoder\n"));
            return 0;
        }
        if (pps->scalingMatrixPresentFlag != 0)
        {
            DEBUG_PRINT(("SCALING Matrix!!! Not supported in baseline decoder\n"));
            return 0;
        }
    }
    return 1;
}

/*------------------------------------------------------------------------------
    Function name   : h264StreamIsBaseline
    Description     :
    Return type     : u32
    Argument        : const decContainer_t * pDecCont
------------------------------------------------------------------------------*/
u32 h264StreamIsBaseline(const decContainer_t *pDecCont)
{
    const picParamSet_t *pps = pDecCont->storage.activePps;
    const seqParamSet_t *sps = pDecCont->storage.activeSps;

    if (sps->frameMbsOnlyFlag != 1)
    {
        return 0;
    }
    if (sps->chromaFormatIdc != 1)
    {
        return 0;
    }
    if (sps->scalingMatrixPresentFlag != 0)
    {
        return 0;
    }
    if (pps->entropyCodingModeFlag != 0)
    {
        return 0;
    }
    if (pps->weightedPredFlag != 0 || pps->weightedBiPredIdc != 0)
    {
        return 0;
    }
    if (pps->transform8x8Flag != 0)
    {
        return 0;
    }
    if (pps->scalingMatrixPresentFlag != 0)
    {
        return 0;
    }
    return 1;
}

/*------------------------------------------------------------------------------
    Function name : h264AllocateResources
    Description   :

    Return type   : u32
    Argument      : decContainer_t * pDecCont
------------------------------------------------------------------------------*/
u32 h264AllocateResources(decContainer_t *pDecCont)
{
    u32 ret, mbs_in_pic;
    DecAsicBuffers_t *asic = pDecCont->asicBuff;
    storage_t *pStorage = &pDecCont->storage;

    const seqParamSet_t *sps = pStorage->activeSps;

    SetDecRegister(pDecCont->h264Regs, HWIF_PIC_MB_WIDTH, sps->picWidthInMbs);
    SetDecRegister(pDecCont->h264Regs, HWIF_PIC_MB_HEIGHT_P,
                   sps->picHeightInMbs);
    SetDecRegister(pDecCont->h264Regs, HWIF_AVS_H264_H_EXT,
                   sps->picHeightInMbs >> 8);

    ReleaseAsicBuffers(pDecCont->dwl, asic);

    ret = AllocateAsicBuffers(pDecCont, asic, pStorage->picSizeInMbs);
    if (ret == 0)
    {
        SET_ADDR_REG(pDecCont->h264Regs, HWIF_INTRA_4X4_BASE,
                     asic->intraPred.busAddress);
        SET_ADDR_REG(pDecCont->h264Regs, HWIF_DIFF_MV_BASE,
                     asic->mv.busAddress);

        if (pDecCont->rlcMode)
        {
            /* release any previously allocated stuff */
            FREE(pStorage->mb);
            FREE(pStorage->sliceGroupMap);

            mbs_in_pic = sps->picWidthInMbs * sps->picHeightInMbs;

            DEBUG_PRINT(("ALLOCATE pStorage->mb            - %8d bytes\n",
                         mbs_in_pic * sizeof(mbStorage_t)));
            pStorage->mb = DWLcalloc(mbs_in_pic, sizeof(mbStorage_t));

            DEBUG_PRINT(("ALLOCATE pStorage->sliceGroupMap - %8d bytes\n",
                         mbs_in_pic * sizeof(u32)));

            ALLOCATE(pStorage->sliceGroupMap, mbs_in_pic, u32);

            if (pStorage->mb == NULL || pStorage->sliceGroupMap == NULL)
            {
                ret = MEMORY_ALLOCATION_ERROR;
            }
            else
            {
                h264bsdInitMbNeighbours(pStorage->mb, sps->picWidthInMbs,
                                        pStorage->picSizeInMbs);
            }
        }
        else
        {
            pStorage->mb = NULL;
            pStorage->sliceGroupMap = NULL;
        }
    }

    return ret;
}

/*------------------------------------------------------------------------------
    Function name : h264InitPicFreezeOutput
    Description   :

    Return type   : u32
    Argument      : decContainer_t * pDecCont
------------------------------------------------------------------------------*/
void h264InitPicFreezeOutput(decContainer_t *pDecCont, u32 fromOldDpb)
{
#ifndef _DISABLE_PIC_FREEZE
    u32 index = 0;
    const u8 *refData;
#endif

    storage_t *storage = &pDecCont->storage;

    /* for concealment after a HW error report we use the saved reference list */
    dpbStorage_t *dpb = &storage->dpb[fromOldDpb];

    /* update status of decoded image (relevant only for  multi-core) */
    /* current out is always in dpb[0] */
    //{
    //    dpbPicture_t *currentOut = storage->dpb->currentOut;

    //    u8 *pSyncMem = (u8*)currentOut->data->virtualAddress +
    //                        dpb->syncMcOffset;
    //    h264MCSetRefPicStatus(pSyncMem, currentOut->isFieldPic,
    //                          currentOut->isBottomField);
    //}

#ifndef _DISABLE_PIC_FREEZE
    do
    {
        refData = h264bsdGetRefPicDataVlcMode(dpb, dpb->list[index], 0);
        index++;
    }
    while (index < 16 && refData == NULL);
#endif

    /* "freeze" whole picture if not field pic or if opposite field of the
     * field pic does not exist in the buffer */
    if (pDecCont->storage.sliceHeader->fieldPicFlag == 0 ||
            storage->dpb[0].currentOut->status[
                !pDecCont->storage.sliceHeader->bottomFieldFlag] == EMPTY)
    {
        storage->dpb[0].currentOut->corruptedFirstFieldOrFrame = 1;
#ifndef _DISABLE_PIC_FREEZE
        /* reset DMV storage for erroneous pictures */
        if (pDecCont->asicBuff->enableDmvAndPoc != 0)
        {
            const u32 dvm_mem_size = storage->picSizeInMbs * 64;
            void *dvm_base = (u8 *)storage->currImage->data->virtualAddress +
                             pDecCont->storage.dpb->dirMvOffset;

            (void) DWLmemset(dvm_base, 0, dvm_mem_size);
        }

        if (refData == NULL)
        {
            DEBUG_PRINT(("h264InitPicFreezeOutput: pic freeze memset\n"));
            (void) DWLmemset(storage->currImage->data->
                             virtualAddress, 128,
                             pDecCont->storage.activeSps->monoChrome ?
                             256 * storage->picSizeInMbs :
                             384 * storage->picSizeInMbs);
            if (storage->enable2ndChroma &&
                    !storage->activeSps->monoChrome)
                (void) DWLmemset((u8 *)storage->currImage->data->virtualAddress +
                                 dpb->ch2Offset, 128,
                                 128 * storage->picSizeInMbs);
        }
        else
        {
            DEBUG_PRINT(("h264InitPicFreezeOutput: pic freeze memcopy\n"));
            (void) DWLmemcpy(storage->currImage->data->virtualAddress,
                             refData,
                             pDecCont->storage.activeSps->monoChrome ?
                             256 * storage->picSizeInMbs :
                             384 * storage->picSizeInMbs);
            if (storage->enable2ndChroma &&
                    !storage->activeSps->monoChrome)
                (void) DWLmemcpy((u8 *)storage->currImage->data->virtualAddress +
                                 dpb->ch2Offset, refData + dpb->ch2Offset,
                                 128 * storage->picSizeInMbs);
        }
#endif
    }
    else
    {
        if (!storage->dpb[0].currentOut->corruptedFirstFieldOrFrame &&
                !storage->dpb[0].currentOut->numErrMbs)
        {
            storage->dpb[0].currentOut->corruptedSecondField = 1;
            if (pDecCont->storage.sliceHeader->bottomFieldFlag != 0)
                storage->dpb[0].currentOut->picStruct = TOPFIELD;
            else
                storage->dpb[0].currentOut->picStruct = BOTFIELD;
        }

#ifndef _DISABLE_PIC_FREEZE
        u32 i;
        u32 fieldOffset = storage->activeSps->picWidthInMbs * 16;
        u8 *lumBase = (u8 *)storage->currImage->data->virtualAddress;
        u8 *chBase = (u8 *)storage->currImage->data->virtualAddress + storage->picSizeInMbs * 256;
        u8 *ch2Base = (u8 *)storage->currImage->data->virtualAddress + dpb->ch2Offset;
        const u8 *refChData = refData + storage->picSizeInMbs * 256;
        const u8 *refCh2Data = refData + dpb->ch2Offset;

        /*Base for second field*/
        u8 *lumBase1 = lumBase;
        u8 *chBase1 = chBase;
        u8 *ch2Base1 = ch2Base;

        if (pDecCont->dpbMode == DEC_DPB_INTERLACED_FIELD)
        {
            if (pDecCont->storage.sliceHeader->bottomFieldFlag != 0)
            {
                lumBase1 = lumBase + 256 * storage->picSizeInMbs / 2;
                chBase1 = chBase + 128 * storage->picSizeInMbs / 2;
                ch2Base1 = ch2Base + 128 * storage->picSizeInMbs / 2;
            }
            else
            {
                lumBase = lumBase + 256 * storage->picSizeInMbs / 2;
                chBase = chBase + 128 * storage->picSizeInMbs / 2;
                ch2Base = ch2Base + 128 * storage->picSizeInMbs / 2;
            }

            if (refData == NULL)
            {
                (void) DWLmemcpy(lumBase1, lumBase,
                                 256 * storage->picSizeInMbs / 2);
                (void) DWLmemcpy(chBase1, chBase,
                                 128 * storage->picSizeInMbs / 2);

                if (storage->enable2ndChroma &&
                        !storage->activeSps->monoChrome)
                    (void) DWLmemcpy(ch2Base, ch2Base1,
                                     128 * storage->picSizeInMbs);
            }
            else
            {
                if (pDecCont->storage.sliceHeader->bottomFieldFlag != 0)
                {
                    refData = refData + 256 * storage->picSizeInMbs / 2;
                    refChData = refChData + 128 * storage->picSizeInMbs / 2;
                    refCh2Data = refCh2Data + 128 * storage->picSizeInMbs / 2;
                }

                (void) DWLmemcpy(lumBase1, refData,
                                 256 * storage->picSizeInMbs / 2);
                (void) DWLmemcpy(chBase1, refChData,
                                 128 * storage->picSizeInMbs / 2);

                if (storage->enable2ndChroma &&
                        !storage->activeSps->monoChrome)
                    (void) DWLmemcpy(ch2Base1, refCh2Data,
                                     128 * storage->picSizeInMbs);
            }
        }
        else
        {

            if (pDecCont->storage.sliceHeader->bottomFieldFlag != 0)
            {
                lumBase += fieldOffset;
                chBase += fieldOffset;
                ch2Base += fieldOffset;
            }

            if (refData == NULL)
            {
                DEBUG_PRINT(("h264InitPicFreezeOutput: pic freeze memset, one field\n"));

                for (i = 0; i < (storage->activeSps->picHeightInMbs * 8); i++)
                {
                    (void) DWLmemset(lumBase, 128, fieldOffset);
                    if ((pDecCont->storage.activeSps->monoChrome == 0) && (i & 0x1U))
                    {
                        (void) DWLmemset(chBase, 128, fieldOffset);
                        chBase += 2 * fieldOffset;
                        if (storage->enable2ndChroma)
                        {
                            (void) DWLmemset(ch2Base, 128, fieldOffset);
                            ch2Base += 2 * fieldOffset;
                        }
                    }
                    lumBase += 2 * fieldOffset;
                }
            }
            else
            {
                if (pDecCont->storage.sliceHeader->bottomFieldFlag != 0)
                {
                    refData += fieldOffset;
                    refChData += fieldOffset;
                    refCh2Data += fieldOffset;
                }

                DEBUG_PRINT(("h264InitPicFreezeOutput: pic freeze memcopy, one field\n"));
                for (i = 0; i < (storage->activeSps->picHeightInMbs * 8); i++)
                {
                    (void) DWLmemcpy(lumBase, refData, fieldOffset);
                    if ((pDecCont->storage.activeSps->monoChrome == 0) && (i & 0x1U))
                    {
                        (void) DWLmemcpy(chBase, refChData, fieldOffset);
                        chBase += 2 * fieldOffset;
                        refChData += 2 * fieldOffset;
                        if (storage->enable2ndChroma)
                        {
                            (void) DWLmemcpy(ch2Base, refCh2Data, fieldOffset);
                            ch2Base += 2 * fieldOffset;
                            refCh2Data += 2 * fieldOffset;
                        }
                    }
                    lumBase += 2 * fieldOffset;
                    refData += 2 * fieldOffset;
                }
            }
        }
#endif
    }

    dpb = &storage->dpb[0]; /* update results for current output */

    {
        i32 i = dpb->numOut;
        u32 tmp = dpb->outIndexR;

        while ((i--) > 0)
        {
            if (tmp == dpb->dpbSize + 1)
                tmp = 0;

            if (dpb->outBuf[tmp].data == storage->currImage->data)
            {
                dpb->outBuf[tmp].numErrMbs = storage->picSizeInMbs;

                /* If first field is ok but second field is corrupted, we treat the picture as
                   single field picture and propagate some internal parameters here */
                dpb->outBuf[tmp].corruptedSecondField =
                    dpb->currentOut->corruptedSecondField;
                dpb->outBuf[tmp].picStruct = dpb->currentOut->picStruct;
                if (dpb->currentOut->corruptedSecondField)
                {
                    dpb->outBuf[tmp].fieldPicture = 1;
                    dpb->outBuf[tmp].topField = (dpb->currentOut->picStruct == TOPFIELD) ? 1 : 0;
                }
                break;
            }
            tmp++;
        }

        i = dpb->dpbSize + 1;

        while ((i--) > 0)
        {
            if (dpb->buffer[i].data == storage->currImage->data)
            {
                dpb->buffer[i].numErrMbs = storage->picSizeInMbs;
                ASSERT(&dpb->buffer[i] == dpb->currentOut);
                break;
            }
        }
    }

    pDecCont->storage.numConcealedMbs = storage->picSizeInMbs;

}

/*------------------------------------------------------------------------------
    Function name : bsdDecodeReturn
    Description   :

    Return type   : void
    Argument      : bsd decoder return value
------------------------------------------------------------------------------*/
static void bsdDecodeReturn(u32 retval)
{

    DEBUG_PRINT(("H264bsdDecode returned: "));
    switch (retval)
    {
    case H264BSD_PIC_RDY:
        DEBUG_PRINT(("H264BSD_PIC_RDY\n"));
        break;
    case H264BSD_RDY:
        DEBUG_PRINT(("H264BSD_RDY\n"));
        break;
    case H264BSD_HDRS_RDY:
        DEBUG_PRINT(("H264BSD_HDRS_RDY\n"));
        break;
    case H264BSD_ERROR:
        DEBUG_PRINT(("H264BSD_ERROR\n"));
        break;
    case H264BSD_PARAM_SET_ERROR:
        DEBUG_PRINT(("H264BSD_PARAM_SET_ERROR\n"));
        break;
    case H264BSD_NEW_ACCESS_UNIT:
        DEBUG_PRINT(("H264BSD_NEW_ACCESS_UNIT\n"));
        break;
    case H264BSD_FMO:
        DEBUG_PRINT(("H264BSD_FMO\n"));
        break;
    default:
        DEBUG_PRINT(("UNKNOWN\n"));
        break;
    }
}

/*------------------------------------------------------------------------------
    Function name   : h264GetSarInfo
    Description     : Returns the sample aspect ratio size info
    Return type     : void
    Argument        : storage_t *pStorage - decoder storage
    Argument        : u32 * sar_width - SAR width returned here
    Argument        : u32 *sar_height - SAR height returned here
------------------------------------------------------------------------------*/
void h264GetSarInfo(const storage_t *pStorage, u32 *sar_width,
                    u32 *sar_height)
{
    switch (h264bsdAspectRatioIdc(pStorage))
    {
    case 0:
        *sar_width = 0;
        *sar_height = 0;
        break;
    case 1:
        *sar_width = 1;
        *sar_height = 1;
        break;
    case 2:
        *sar_width = 12;
        *sar_height = 11;
        break;
    case 3:
        *sar_width = 10;
        *sar_height = 11;
        break;
    case 4:
        *sar_width = 16;
        *sar_height = 11;
        break;
    case 5:
        *sar_width = 40;
        *sar_height = 33;
        break;
    case 6:
        *sar_width = 24;
        *sar_height = 1;
        break;
    case 7:
        *sar_width = 20;
        *sar_height = 11;
        break;
    case 8:
        *sar_width = 32;
        *sar_height = 11;
        break;
    case 9:
        *sar_width = 80;
        *sar_height = 33;
        break;
    case 10:
        *sar_width = 18;
        *sar_height = 11;
        break;
    case 11:
        *sar_width = 15;
        *sar_height = 11;
        break;
    case 12:
        *sar_width = 64;
        *sar_height = 33;
        break;
    case 13:
        *sar_width = 160;
        *sar_height = 99;
        break;
    case 255:
        h264bsdSarSize(pStorage, sar_width, sar_height);
        break;
    default:
        *sar_width = 0;
        *sar_height = 0;
    }
}

/*------------------------------------------------------------------------------
    Function name   : h264CheckReleasePpAndHw
    Description     : Release HW lock and wait for PP to finish, need to be
                      called if errors/problems after first field of a picture
                      finished and PP left running
    Return type     : void
    Argument        :
    Argument        :
    Argument        :
------------------------------------------------------------------------------*/
void h264CheckReleasePpAndHw(decContainer_t *pDecCont)
{

    if (pDecCont->pp.ppInstance != NULL &&
            (pDecCont->pp.decPpIf.ppStatus == DECPP_RUNNING ||
             pDecCont->pp.decPpIf.ppStatus == DECPP_PIC_NOT_FINISHED))
    {
        pDecCont->pp.decPpIf.ppStatus = DECPP_PIC_READY;
        pDecCont->pp.PPDecWaitEnd(pDecCont->pp.ppInstance);
    }

    if (pDecCont->keepHwReserved)
    {
        pDecCont->keepHwReserved = 0;
        DWLReleaseHw(pDecCont->dwl, pDecCont->coreID);
    }

}

/*------------------------------------------------------------------------------

    Function: H264DecPeek

        Functional description:
            Get last decoded picture if any available. No pictures are removed
            from output nor DPB buffers.

        Input:
            decInst     decoder instance.

        Output:
            pOutput     pointer to output structure

        Returns:
            H264DEC_OK            no pictures available for display
            H264DEC_PIC_RDY       picture available for display
            H264DEC_PARAM_ERROR   invalid parameters

------------------------------------------------------------------------------*/
H264DecRet H264DecPeek(H264DecInst decInst, H264DecPicture *pOutput)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;
    dpbPicture_t *currentOut = pDecCont->storage.dpb->currentOut;

    DEC_API_TRC("H264DecPeek#\n");

    if (decInst == NULL || pOutput == NULL)
    {
        DEC_API_TRC("H264DecPeek# ERROR: decInst or pOutput is NULL\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecPeek# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }

    if (pDecCont->decStat != H264DEC_NEW_HEADERS &&
            pDecCont->storage.dpb->fullness && currentOut != NULL &&
            (currentOut->status[0] != EMPTY || currentOut->status[1] != EMPTY))
    {

        pOutput->pOutputPicture = currentOut->data->virtualAddress;
        pOutput->outputPictureBusAddress = currentOut->data->busAddress;
        pOutput->picId = currentOut->picId;
        pOutput->picCodingType[0] = currentOut->picCodeType[0];
        pOutput->picCodingType[1] = currentOut->picCodeType[1];
        pOutput->isIdrPicture[0] = currentOut->isIdr[0];
        pOutput->isIdrPicture[1] = currentOut->isIdr[1];
        pOutput->decodeId[0] = currentOut->decodeId[0];
        pOutput->decodeId[1] = currentOut->decodeId[1];
        pOutput->nbrOfErrMBs = currentOut->numErrMbs;

        pOutput->interlaced = pDecCont->storage.dpb->interlaced;
        pOutput->fieldPicture = currentOut->isFieldPic;
        pOutput->outputFormat = currentOut->tiledMode ?
                                DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;
        pOutput->topField = 0;
        pOutput->picStruct = currentOut->picStruct;

        if (pOutput->fieldPicture)
        {
            /* just one field in buffer -> that is the output */
            if (currentOut->status[0] == EMPTY || currentOut->status[1] == EMPTY)
            {
                pOutput->topField = (currentOut->status[0] == EMPTY) ? 0 : 1;
            }
            /* both fields decoded -> check field parity from slice header */
            else
                pOutput->topField =
                    pDecCont->storage.sliceHeader->bottomFieldFlag == 0;
        }
        else
            pOutput->topField = 1;

        pOutput->picWidth = h264bsdPicWidth(&pDecCont->storage) << 4;
        pOutput->picHeight = h264bsdPicHeight(&pDecCont->storage) << 4;

        pOutput->cropParams = currentOut->crop;


        DEC_API_TRC("H264DecPeek# H264DEC_PIC_RDY\n");
        return (H264DEC_PIC_RDY);
    }
    else
    {
        DEC_API_TRC("H264DecPeek# H264DEC_OK\n");
        return (H264DEC_OK);
    }

}

/*------------------------------------------------------------------------------

    Function: H264DecSetMvc()

        Functional description:
            This function configures decoder to decode both views of MVC
            stereo high profile compliant streams.

        Inputs:
            decInst     decoder instance

        Outputs:

        Returns:
            H264DEC_OK            success
            H264DEC_PARAM_ERROR   invalid parameters
            H264DEC_NOT_INITIALIZED   decoder instance not initialized yet

------------------------------------------------------------------------------*/
H264DecRet H264DecSetMvc(H264DecInst decInst)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;
    DWLHwConfig_t hwCfg;

    DEC_API_TRC("H264DecSetMvc#");

    if (decInst == NULL)
    {
        DEC_API_TRC("H264DecSetMvc# ERROR: decInst is NULL\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecSetMvc# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }

    (void) DWLmemset(&hwCfg, 0, sizeof(DWLHwConfig_t));
    DWLReadAsicConfig(&hwCfg);
    if (!hwCfg.mvcSupport)
    {
        DEC_API_TRC("H264DecSetMvc# ERROR: H264 MVC not supported in HW\n");
        return H264DEC_FORMAT_NOT_SUPPORTED;
    }

    pDecCont->storage.mvc = HANTRO_TRUE;

    DEC_API_TRC("H264DecSetMvc# OK\n");

    return (H264DEC_OK);
}

#ifdef USE_OUTPUT_RELEASE
/*------------------------------------------------------------------------------

    Function: H264DecPictureConsumed()

        Functional description:
            Release the frame displayed and sent by APP

        Inputs:
            decInst     Decoder instance

            pPicture    pointer of picture structure to be released

        Outputs:
            none

        Returns:
            H264DEC_PARAM_ERROR       Decoder instance or pPicture is null
            H264DEC_NOT_INITIALIZED   Decoder instance isn't initialized
            H264DEC_OK                pPicture release success

------------------------------------------------------------------------------*/
H264DecRet H264DecPictureConsumed(H264DecInst decInst,
                                  const H264DecPicture *pPicture)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;
    const dpbStorage_t *dpb;
    u32 id = FB_NOT_VALID_ID, i;

    DEC_API_TRC("H264DecPictureConsumed#\n");

    if (decInst == NULL || pPicture == NULL)
    {
        DEC_API_TRC("H264DecPictureConsumed# ERROR: decInst or pPicture is NULL\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecPictureConsumed# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }

    /* find the mem descriptor for this specific buffer, base view first */
    dpb = pDecCont->storage.dpbs[0];
    for (i = 0; i < dpb->totBuffers; i++)
    {
        if (pPicture->outputPictureBusAddress == dpb->picBuffers[i].busAddress &&
                pPicture->pOutputPicture == dpb->picBuffers[i].virtualAddress)
        {
            id = i;
            break;
        }
    }

    /* if not found, search other view for MVC mode */
    if (id == FB_NOT_VALID_ID && pDecCont->storage.mvc == HANTRO_TRUE)
    {
        dpb = pDecCont->storage.dpbs[1];
        /* find the mem descriptor for this specific buffer */
        for (i = 0; i < dpb->totBuffers; i++)
        {
            if (pPicture->outputPictureBusAddress == dpb->picBuffers[i].busAddress &&
                    pPicture->pOutputPicture == dpb->picBuffers[i].virtualAddress)
            {
                id = i;
                break;
            }
        }
    }

    if (id == FB_NOT_VALID_ID)
        return H264DEC_PARAM_ERROR;

    PopOutputPic(&pDecCont->fbList, dpb->picBuffID[id]);

    return H264DEC_OK;
}


/*------------------------------------------------------------------------------

    Function: H264DecNextPicture_INTERNAL

        Functional description:
            Push next picture in display order into output list if any available.

        Input:
            decInst     decoder instance.
            endOfStream force output of all buffered pictures

        Output:
            pOutput     pointer to output structure

        Returns:
            H264DEC_OK            no pictures available for display
            H264DEC_PIC_RDY       picture available for display
            H264DEC_PARAM_ERROR     invalid parameters
            H264DEC_NOT_INITIALIZED   decoder instance not initialized yet

------------------------------------------------------------------------------*/
H264DecRet H264DecNextPicture_INTERNAL(H264DecInst decInst,
                                       H264DecPicture *pOutput,
                                       u32 endOfStream)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;
    const dpbOutPicture_t *outPic = NULL;
    dpbStorage_t *outDpb;
    storage_t *pStorage;
    sliceHeader_t *pSliceHdr;

    DEC_API_TRC("H264DecNextPicture_INTERNAL#\n");

    if (decInst == NULL || pOutput == NULL)
    {
        DEC_API_TRC("H264DecNextPicture_INTERNAL# ERROR: decInst or pOutput is NULL\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecNextPicture_INTERNAL# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }

    pStorage = &pDecCont->storage;
    pSliceHdr = pStorage->sliceHeader;
    outDpb = pDecCont->storage.dpbs[pDecCont->storage.outView];

    /* if display order is the same as decoding order and PP is used and
     * cannot be used in pipeline (rotation) -> do not perform PP here but
     * while decoding next picture (parallel processing instead of
     * DEC followed by PP followed by DEC...) */
    if (pDecCont->storage.pendingOutPic)
    {
        outPic = pDecCont->storage.pendingOutPic;
        pDecCont->storage.pendingOutPic = NULL;
    }
    else if (outDpb->noReordering == 0)
    {
        if (!outDpb->delayedOut)
        {
            if (pDecCont->pp.ppInstance && pDecCont->pp.decPpIf.ppStatus ==
                    DECPP_PIC_READY)
                outDpb->noOutput = 0;

            pDecCont->storage.dpb =
                pDecCont->storage.dpbs[pDecCont->storage.outView];

            outPic = h264bsdNextOutputPicture(&pDecCont->storage);

            if ((pDecCont->storage.numViews ||
                    pDecCont->storage.outView) && outPic != NULL)
            {
                pOutput->viewId =
                    pDecCont->storage.viewId[pDecCont->storage.outView];
                pDecCont->storage.outView ^= 0x1;
            }
        }
    }
    else
    {
        /* no reordering of output pics AND stereo was activated after base
         * picture was output -> output stereo view pic if available */
        if (pDecCont->storage.numViews &&
                pDecCont->storage.view && pDecCont->storage.outView == 0 &&
                outDpb->numOut == 0 &&
                pDecCont->storage.dpbs[pDecCont->storage.view]->numOut > 0)
        {
            pDecCont->storage.outView ^= 0x1;
            outDpb = pDecCont->storage.dpbs[pDecCont->storage.outView];
        }

        if (outDpb->numOut > 1 || endOfStream ||
                pStorage->prevNalUnit->nalRefIdc == 0 ||
                pDecCont->pp.ppInstance == NULL ||
                pDecCont->pp.decPpIf.usePipeline ||
                pStorage->view != pStorage->outView)
        {
            if (!endOfStream &&
                    ((outDpb->numOut == 1 && outDpb->delayedOut) ||
                     (pSliceHdr->fieldPicFlag && pStorage->secondField)))
            {
            }
            else
            {
                pDecCont->storage.dpb =
                    pDecCont->storage.dpbs[pDecCont->storage.outView];

                outPic = h264bsdNextOutputPicture(&pDecCont->storage);

                pOutput->viewId =
                    pDecCont->storage.viewId[pDecCont->storage.outView];
                if ((pDecCont->storage.numViews ||
                        pDecCont->storage.outView) && outPic != NULL)
                    pDecCont->storage.outView ^= 0x1;
            }
        }
    }

    if (outPic != NULL)
    {
        if (!pDecCont->storage.numViews)
            pOutput->viewId = 0;

        pOutput->pOutputPicture = outPic->data->virtualAddress;
        pOutput->outputPictureBusAddress = outPic->data->busAddress;
        pOutput->picId = outPic->picId;
        pOutput->picCodingType[0] = outPic->picCodeType[0];
        pOutput->picCodingType[1] = outPic->picCodeType[1];
        pOutput->isIdrPicture[0] = outPic->isIdr[0];
        pOutput->isIdrPicture[1] = outPic->isIdr[1];
        pOutput->decodeId[0] = outPic->decodeId[0];
        pOutput->decodeId[1] = outPic->decodeId[1];
        pOutput->nbrOfErrMBs = outPic->numErrMbs;

        pOutput->interlaced = outPic->interlaced;
        pOutput->fieldPicture = outPic->fieldPicture;
        pOutput->topField = outPic->topField;

        pOutput->picWidth = outPic->picWidth;
        pOutput->picHeight = outPic->picHeight;

        pOutput->outputFormat = outPic->tiledMode ?
                                DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

        pOutput->cropParams = outPic->crop;
        if (pOutput->fieldPicture)
            pOutput->picStruct = pOutput->topField ? TOPFIELD : BOTFIELD;
        else
            pOutput->picStruct = outPic->picStruct;

        DEC_API_TRC("H264DecNextPicture_INTERNAL# H264DEC_PIC_RDY\n");

        if (pOutput->nbrOfErrMBs && !outPic->corruptedSecondField)
            ClearOutput(&pDecCont->fbList, outPic->memIdx);
        else
            PushOutputPic(&pDecCont->fbList, pOutput, outPic->memIdx);

        return (H264DEC_PIC_RDY);
    }
    else
    {
        DEC_API_TRC("H264DecNextPicture_INTERNAL# H264DEC_OK\n");
        return (H264DEC_OK);
    }

}


H264DecRet H264DecEndOfStream(H264DecInst decInst, u32 strmEndFlag)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;
    u32 count = 0;

    DEC_API_TRC("H264DecEndOfStream#\n");

    if (decInst == NULL)
    {
        DEC_API_TRC("H264DecEndOfStream# ERROR: decInst is NULL\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecEndOfStream# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }
    //pthread_mutex_lock(&pDecCont->protect_mutex);
    if (pDecCont->decStat == H264DEC_END_OF_STREAM)
    {
        //pthread_mutex_unlock(&pDecCont->protect_mutex);
        return (H264DEC_OK);
    }

    if (pDecCont->asicRunning)
    {
        /* stop HW */
        SetDecRegister(pDecCont->h264Regs, HWIF_DEC_IRQ_STAT, 0);
        SetDecRegister(pDecCont->h264Regs, HWIF_DEC_IRQ, 0);
        SetDecRegister(pDecCont->h264Regs, HWIF_DEC_E, 0);
        DWLDisableHW(pDecCont->dwl, pDecCont->coreID, 4 * 1,
                     pDecCont->h264Regs[1] | DEC_IRQ_DISABLE);
        DWLReleaseHw(pDecCont->dwl, pDecCont->coreID);  /* release HW lock */
        pDecCont->asicRunning = 0;

        /* Decrement usage for DPB buffers */
        DecrementDPBRefCount(&pDecCont->storage.dpb[1]);
        pDecCont->decStat = H264DEC_INITIALIZED;
        h264InitPicFreezeOutput(pDecCont, 1);
    }
    else if (pDecCont->keepHwReserved)
    {
        DWLReleaseHw(pDecCont->dwl, pDecCont->coreID);  /* release HW lock */
        pDecCont->keepHwReserved = 0;
    }

    /* flush any remaining pictures form DPB */
    h264bsdFlushBuffer(&pDecCont->storage);

    FinalizeOutputAll(&pDecCont->fbList);

    {
        H264DecPicture output;

        while (H264DecNextPicture_INTERNAL(decInst, &output, 1) == H264DEC_PIC_RDY)
        {
            count++;
        }
    }

    /* After all output pictures were pushed, update decoder status to
     * reflect the end-of-stream situation. This way the H264DecMCNextPicture
     * will not block anymore once all output was handled.
     */
    if (strmEndFlag)
    {
#ifndef CLEAR_HDRINFO_IN_SEEK
        if (pDecCont->decStat != H264DEC_WAITING_FOR_BUFFER &&
                pDecCont->decStat != H264DEC_NEW_HEADERS)
#endif
            pDecCont->decStat = H264DEC_END_OF_STREAM;
    }

    /* wake-up output thread */
    PushOutputPic(&pDecCont->fbList, NULL, -1);

    /* TODO(atna): should it be enough to wait until all cores idle and
     *             not that output is empty !?
     */
#if defined(USE_EXTERNAL_BUFFER) && !defined(H264_EXT_BUF_SAFE_RELEASE)
    if (strmEndFlag)
    {
        int i;
        pthread_mutex_lock(&pDecCont->fbList.ref_count_mutex);
        for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++)
        {
            pDecCont->fbList.fbStat[i].nRefCount = 0;
        }
        pthread_mutex_unlock(&pDecCont->fbList.ref_count_mutex);
    }
#endif
    WaitListNotInUse(&pDecCont->fbList);
    //pthread_mutex_unlock(&pDecCont->protect_mutex);

    DEC_API_TRC("H264DecEndOfStream# H264DEC_OK\n");
    return (H264DEC_OK);
}

#endif


#ifdef USE_EXTERNAL_BUFFER
void H264SetExternalBufferInfo(H264DecInst decInst, storage_t *storage)
{
    decContainer_t *decCont = (decContainer_t *)decInst;
    u32 picSizeInMbs = storage->activeSps->picWidthInMbs * storage->activeSps->picHeightInMbs;
    u32 picSize = picSizeInMbs * (storage->activeSps->monoChrome ? 256 : 384);

    /* buffer size of dpb pic = picSize + dir_mv_size + tbl_size */
    u32 dmvMemSize = picSizeInMbs * 64;
    u32 refBuffSize = picSize  + dmvMemSize + 32;
    u32 minBufferNum, maxDpbSize, noReorder, totBuffers;

    if (storage->noReordering ||
            storage->activeSps->picOrderCntType == 2 ||
            (storage->activeSps->vuiParametersPresentFlag &&
             storage->activeSps->vuiParameters->bitstreamRestrictionFlag &&
             !storage->activeSps->vuiParameters->numReorderFrames))
        noReorder = HANTRO_TRUE;
    else
        noReorder = HANTRO_FALSE;

    if (storage->view == 0)
        maxDpbSize = storage->activeSps->maxDpbSize;
    else
    {
        /* stereo view dpb size at least equal to base view size (to make sure
         * that base view pictures get output in correct display order) */
        maxDpbSize = MAX(storage->activeSps->maxDpbSize, storage->activeViewSps[0]->maxDpbSize);
    }
    /* restrict max dpb size of mvc (stereo high) streams, make sure that
     * base address 15 is available/restricted for inter view reference use */
    if (storage->mvcStream)
        maxDpbSize = MIN(maxDpbSize, 8);

    if (noReorder)
        totBuffers = MAX(storage->activeSps->numRefFrames, 1) + 1;
    else
        totBuffers = maxDpbSize + 1;

    if (totBuffers > MAX_FRAME_BUFFER_NUMBER)
        totBuffers = MAX_FRAME_BUFFER_NUMBER;

    minBufferNum = totBuffers;

    decCont->bufNum = minBufferNum;
    decCont->nextBufSize = refBuffSize;
}

void H264SetMVCExternalBufferInfo(H264DecInst decInst, storage_t *storage)
{
    decContainer_t *decCont = (decContainer_t *)decInst;
    u32 picSizeInMbs, picSize;

    if (storage->sps[1] != 0)
        picSizeInMbs = MAX(storage->sps[0]->picWidthInMbs * storage->sps[0]->picHeightInMbs,
                           storage->sps[1]->picWidthInMbs * storage->sps[1]->picHeightInMbs);
    else
        picSizeInMbs = storage->sps[0]->picWidthInMbs * storage->sps[0]->picHeightInMbs;

    picSize = picSizeInMbs * (storage->sps[0]->monoChrome ? 256 : 384);

    /* buffer size of dpb pic = picSize + dir_mv_size + tbl_size */
    u32 dmvMemSize = picSizeInMbs * 64;
    decCont->nextBufSize = picSize  + dmvMemSize + 32;

    decCont->bufNum = 0;
    u32 j = 0;
    for (u32 i = 0; i < 2; i ++)
    {
        u32 maxDpbSize, noReorder, totBuffers;
        if (storage->noReordering ||
                storage->sps[j]->picOrderCntType == 2 ||
                (storage->sps[j]->vuiParametersPresentFlag &&
                 storage->sps[j]->vuiParameters->bitstreamRestrictionFlag &&
                 !storage->sps[j]->vuiParameters->numReorderFrames))
            noReorder = HANTRO_TRUE;
        else
            noReorder = HANTRO_FALSE;

        maxDpbSize = storage->sps[j]->maxDpbSize;

        /* restrict max dpb size of mvc (stereo high) streams, make sure that
        * base address 15 is available/restricted for inter view reference use */
        maxDpbSize = MIN(maxDpbSize, 8);

        if (noReorder)
            totBuffers = MAX(storage->sps[j]->numRefFrames, 1) + 1;
        else
            totBuffers = maxDpbSize + 1;

        decCont->bufNum += totBuffers;
        if (storage->sps[1] != 0)
            j ++;
    }

    if (decCont->bufNum > MAX_FRAME_BUFFER_NUMBER)
        decCont->bufNum = MAX_FRAME_BUFFER_NUMBER;
}


H264DecRet H264DecGetBufferInfo(H264DecInst decInst, H264DecBufferInfo *mem_info)
{
    decContainer_t   *decCont = (decContainer_t *)decInst;

    DWLLinearMem_t empty = {0, 0, 0};

    if (decCont == NULL || mem_info == NULL)
    {
        return H264DEC_PARAM_ERROR;
    }

    if (decCont->bufToFree == NULL && decCont->nextBufSize == 0)
    {
        /* External reference buffer: release done. */
        mem_info->bufToFree = empty;
        mem_info->nextBufSize = decCont->nextBufSize;
        mem_info->bufNum = decCont->bufNum;
        // Take guard buffer into consideration
        mem_info->bufNum = decCont->bufNum + decCont->nGuardSize;
        return H264DEC_OK;
    }

    if (decCont->bufToFree)
    {
        mem_info->bufToFree = *decCont->bufToFree;
        decCont->bufToFree->virtualAddress = NULL;
        decCont->bufToFree = NULL;
    }
    else
        mem_info->bufToFree = empty;

    mem_info->nextBufSize = decCont->nextBufSize;
    mem_info->bufNum = decCont->bufNum;

    // Take guard buffer into consideration
    mem_info->bufNum += decCont->nGuardSize;

    ASSERT((mem_info->bufNum && mem_info->nextBufSize) ||
           (mem_info->bufToFree.virtualAddress != NULL));

    return H264DEC_WAITING_FOR_BUFFER;
}

H264DecRet H264DecAddBuffer(H264DecInst decInst, DWLLinearMem_t *info)
{
    decContainer_t *decCont = (decContainer_t *)decInst;
    H264DecRet decRet = H264DEC_OK;

    if (decInst == NULL || info == NULL ||
            X170_CHECK_VIRTUAL_ADDRESS(info->virtualAddress) ||
            X170_CHECK_BUS_ADDRESS_AGLINED(info->busAddress) ||
            info->size < decCont->nextBufSize)
    {
        return H264DEC_PARAM_ERROR;
    }

    decCont->nExtBufSize = info->size;
    if (!decCont->bMVC)
    {
        u32 i = decCont->bufferIndex[0];
        u32 id;
        dpbStorage_t *dpb = decCont->storage.dpbs[0];
        if (i < dpb->totBuffers)
        {
            dpb->picBuffers[i] = *info;
            if (i < dpb->dpbSize + 1)
            {
                id = AllocateIdUsed(dpb->fbList, dpb->picBuffers + i);
                if (id == FB_NOT_VALID_ID)
                {
                    return MEMORY_ALLOCATION_ERROR;
                }

                dpb->buffer[i].data = dpb->picBuffers + i;
                dpb->buffer[i].memIdx = id;
                dpb->buffer[i].numErrMbs = -1;
                dpb->picBuffID[i] = id;
            }
            else
            {
                id = AllocateIdFree(dpb->fbList, dpb->picBuffers + i);
                if (id == FB_NOT_VALID_ID)
                {
                    return MEMORY_ALLOCATION_ERROR;
                }

                dpb->picBuffID[i] = id;
            }

            void *base =
                (char *)(dpb->picBuffers[i].virtualAddress) + dpb->dirMvOffset;
            (void)DWLmemset(base, 0, info->size - dpb->dirMvOffset);

            decCont->bufferIndex[0]++;
            if (decCont->bufferIndex[0] < dpb->totBuffers)
                decRet = H264DEC_WAITING_FOR_BUFFER;
        }
        else
        {
            /* Adding extra buffers. */
            if (decCont->bufferIndex[0] >= MAX_FRAME_BUFFER_NUMBER)
            {
                /* Too much buffers added. */
                return H264DEC_EXT_BUFFER_REJECTED;
            }

            dpb->picBuffers[i] = *info;
            dpb[1].picBuffers[i] = *info;
            /* Need the allocate a USED id to be added as free buffer in SetFreePicBuffer. */
            id = AllocateIdUsed(dpb->fbList, dpb->picBuffers + i);
            if (id == FB_NOT_VALID_ID)
            {
                return MEMORY_ALLOCATION_ERROR;
            }
            dpb->picBuffID[i] = id;
            dpb[1].picBuffID[i] = id;

            void *base =
                (char *)(dpb->picBuffers[i].virtualAddress) + dpb->dirMvOffset;
            (void)DWLmemset(base, 0, info->size - dpb->dirMvOffset);

            decCont->bufferIndex[0]++;
            dpb->totBuffers++;
            dpb[1].totBuffers++;

            SetFreePicBuffer(dpb->fbList, id);
        }
    }
    else
    {
        u32 *idx = decCont->bufferIndex;
        if (idx[0] < decCont->storage.dpbs[0]->totBuffers || idx[1] < decCont->storage.dpbs[1]->totBuffers)
        {
            for (u32 i = 0; i < 2; i ++)
            {
                u32 id;
                dpbStorage_t *dpb = decCont->storage.dpbs[i];
                if (idx[i] < dpb->totBuffers)
                {
                    dpb->picBuffers[idx[i]] = *info;
                    if (idx[i] < dpb->dpbSize + 1)
                    {
                        id = AllocateIdUsed(dpb->fbList, dpb->picBuffers + idx[i]);
                        if (id == FB_NOT_VALID_ID)
                        {
                            return MEMORY_ALLOCATION_ERROR;
                        }

                        dpb->buffer[idx[i]].data = dpb->picBuffers + idx[i];
                        dpb->buffer[idx[i]].memIdx = id;
                        dpb->picBuffID[idx[i]] = id;
                    }
                    else
                    {
                        id = AllocateIdFree(dpb->fbList, dpb->picBuffers + idx[i]);
                        if (id == FB_NOT_VALID_ID)
                        {
                            return MEMORY_ALLOCATION_ERROR;
                        }

                        dpb->picBuffID[idx[i]] = id;
                    }

                    void *base =
                        (char *)(dpb->picBuffers[idx[i]].virtualAddress) + dpb->dirMvOffset;
                    (void)DWLmemset(base, 0, info->size - dpb->dirMvOffset);

                    decCont->bufferIndex[i]++;
                    if (decCont->bufferIndex[i] < dpb->totBuffers)
                        decRet = H264DEC_WAITING_FOR_BUFFER;
                    break;
                }
            }
        }
        else
        {
            /* Adding extra buffers. */
            if ((idx[0] + idx[1]) >= MAX_FRAME_BUFFER_NUMBER)
            {
                /* Too much buffers added. */
                return H264DEC_EXT_BUFFER_REJECTED;
            }
            u32 i = idx[0] < idx[1] ? 0 : 1;
            dpbStorage_t *dpb = decCont->storage.dpbs[i];
            dpb->picBuffers[idx[i]] = *info;
            /* Need the allocate a USED id to be added as free buffer in SetFreePicBuffer. */
            u32 id = AllocateIdUsed(dpb->fbList, dpb->picBuffers + idx[i]);
            if (id == FB_NOT_VALID_ID)
            {
                return MEMORY_ALLOCATION_ERROR;
            }
            dpb->picBuffID[idx[i]] = id;

            void *base =
                (char *)(dpb->picBuffers[idx[i]].virtualAddress) + dpb->dirMvOffset;
            (void)DWLmemset(base, 0, info->size - dpb->dirMvOffset);

            decCont->bufferIndex[i]++;
            dpb->totBuffers++;

            SetFreePicBuffer(dpb->fbList, id);
        }
    }

    return decRet;
}
#endif


#ifdef USE_OUTPUT_RELEASE
void H264EnterAbortState(decContainer_t *pDecCont)
{
    SetAbortStatusInList(&pDecCont->fbList);
    pDecCont->abort = 1;
}

void H264ExistAbortState(decContainer_t *pDecCont)
{
    ClearAbortStatusInList(&pDecCont->fbList);
    pDecCont->abort = 0;
}

void h264DecStateReset(decContainer_t *pDecCont)
{
    dpbStorage_t *dpb = pDecCont->storage.dpbs[0];

    /* Clear parameters in dpb */
    h264DpbStateReset(dpb);
    if (pDecCont->storage.mvcStream)
    {
        dpb = pDecCont->storage.dpbs[1];
        h264DpbStateReset(dpb);
    }

    /* Clear parameters in storage */
    h264bsdClearStorage(&pDecCont->storage);

    /* Clear parameters in decContainer */
#ifndef CLEAR_HDRINFO_IN_SEEK
    if (pDecCont->decStat != H264DEC_WAITING_FOR_BUFFER &&
            pDecCont->decStat != H264DEC_NEW_HEADERS)
#endif
        pDecCont->decStat = H264DEC_INITIALIZED;

    pDecCont->picNumber = 0;
#ifdef CLEAR_HDRINFO_IN_SEEK
    pDecCont->rlcMode = 0;
    pDecCont->tryVlc = 0;
    pDecCont->modeChange = 0;
#endif
    pDecCont->reallocate = 0;
    pDecCont->gapsCheckedForThis = 0;
    pDecCont->packetDecoded = 0;
    pDecCont->keepHwReserved = 0;
    pDecCont->forceNalMode = 0;

#ifdef USE_EXTERNAL_BUFFER
    pDecCont->bufferIndex[0] = 0;
    pDecCont->bufferIndex[1] = 0;
#endif

#ifdef SKIP_OPENB_FRAME
    pDecCont->entryIsIDR = 0;
    pDecCont->entryIsI = 0;
    pDecCont->entryPOC = 0;
    pDecCont->firstEntryPoint = 0;
    pDecCont->skipB = 0;
#endif
}

H264DecRet H264DecAbort(H264DecInst decInst)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;
    if (decInst == NULL)
    {
        DEC_API_TRC("H264DecAbort# ERROR: decInst is NULL\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecSetMvc# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }

    //pthread_mutex_lock(&pDecCont->protect_mutex);
    /* Abort frame buffer waiting and rs/ds buffer waiting */
    H264EnterAbortState(pDecCont);
    //pthread_mutex_unlock(&pDecCont->protect_mutex);
    return (H264DEC_OK);
}

H264DecRet H264DecAbortAfter(H264DecInst decInst)
{
    decContainer_t *pDecCont = (decContainer_t *) decInst;
    int i;
    if (decInst == NULL)
    {
        DEC_API_TRC("H264DecAbortAfter# ERROR: decInst is NULL\n");
        return (H264DEC_PARAM_ERROR);
    }

    /* Check for valid decoder instance */
    if (pDecCont->checksum != pDecCont)
    {
        DEC_API_TRC("H264DecAbortAfter# ERROR: Decoder not initialized\n");
        return (H264DEC_NOT_INITIALIZED);
    }

    //pthread_mutex_lock(&pDecCont->protect_mutex);

#if 0
    /* If normal EOS is waited, return directly */
    if (pDecCont->decStat == H264DEC_END_OF_STREAM)
    {
        //pthread_mutex_unlock(&pDecCont->protect_mutex);
        return (H264DEC_OK);
    }
#endif

    if (pDecCont->asicRunning)
    {
        /* stop HW */
        SetDecRegister(pDecCont->h264Regs, HWIF_DEC_IRQ_STAT, 0);
        SetDecRegister(pDecCont->h264Regs, HWIF_DEC_IRQ, 0);
        SetDecRegister(pDecCont->h264Regs, HWIF_DEC_E, 0);
        DWLDisableHW(pDecCont->dwl, pDecCont->coreID, 4 * 1,
                     pDecCont->h264Regs[1] | DEC_IRQ_DISABLE);
        DWLReleaseHw(pDecCont->dwl, pDecCont->coreID);  /* release HW lock */
        DecrementDPBRefCount(pDecCont->storage.dpb);
        pDecCont->asicRunning = 0;

    }

    /* Clear any remaining pictures from DPB */
    h264EmptyDpb(pDecCont->storage.dpbs[0]);
    h264EmptyDpb(pDecCont->storage.dpbs[1]);
    h264DecStateReset(pDecCont);
    H264ExistAbortState(pDecCont);

#ifndef H264_EXT_BUF_SAFE_RELEASE
    pthread_mutex_lock(&pDecCont->fbList.ref_count_mutex);
    for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++)
    {
        pDecCont->fbList.fbStat[i].nRefCount = 0;
    }
    pthread_mutex_unlock(&pDecCont->fbList.ref_count_mutex);
#endif

    //pthread_mutex_unlock(&pDecCont->protect_mutex);
    return (H264DEC_OK);
}
#endif
