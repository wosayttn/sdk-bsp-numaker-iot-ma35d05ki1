/* Copyright 2012 Google Inc. All Rights Reserved. */
/* Author: attilanagy@google.com (Atti Nagy) */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>


#include "basetype.h"
#include "dwl.h"
#include "dwlthread.h"
#include "dwl_hw_core_array.h"
#include "dwl_swhw_sync.h"

#include "../../test/common/swhw/tb_cfg.h"

#ifdef INTERNAL_TEST
    #include "internal_test.h"
#endif

#ifdef _DWL_DEBUG
#define DWL_DEBUG(fmt, args...) sysprintf(__FILE__":%d:%s() " fmt,\
                                        __LINE__, __func__, ## args)
#else
#define DWL_DEBUG(fmt, args...) do {} while (0)
#endif

/* Constants related to registers */
#define DEC_X170_REGS 256
#define PP_FIRST_REG  60
#define DEC_STREAM_START_REG 12

#define IS_PIPELINE_ENABLED(val)    ((val) & 0x02)

#ifdef DWL_PRESET_FAILING_ALLOC
    #define FAIL_DURING_ALLOC DWL_PRESET_FAILING_ALLOC
#endif

extern TBCfg tbCfg;
extern u32 gHwVer;
extern u32 h264HighSupport;

#ifdef ASIC_TRACE_SUPPORT
    extern u8 *dpbBaseAddress;
#endif
static i32 DWLTestRandomFail(void);

/* counters for core usage statistics */
u32 coreUsageCounts[MAX_ASIC_CORES] = {0};


typedef struct DWLInstance
{
    u32 clientType;
    u8 *pFreeRefFrmMem;
    u8 *pFrmBase;

    u32 bReservedPipe;

    /* Keep track of allocated memories */
    u32 referenceTotal;
    u32 referenceAllocCount;
    u32 referenceMaximum;
    u32 linearTotal;
    i32 linearAllocCount;

    hw_core_array hwCoreArray;
    /* TODO(vmr): Get rid of temporary core "memory" mechanism. */
    core currentCore;

} DWLInstance_t;


hw_core_array gHwCoreArray;
mc_listener_thread_params listenerThreadParams;
pthread_t mc_listener_thread;

#ifdef _DWL_PERFORMANCE
    u32 referenceTotalMax = 0;
    u32 linearTotalMax = 0;
    u32 mallocTotalMax = 0;
#endif

#ifdef FAIL_DURING_ALLOC
    u32 failedAllocCount = 0;
#endif

/* a mutex protecting the wrapper init */
//pthread_mutex_t dwl_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int nDwlInstanceCount = 0;

/* single core PP mutex */
//static pthread_mutex_t pp_mutex = PTHREAD_MUTEX_INITIALIZER;

/*------------------------------------------------------------------------------
    Function name   : DWLReadAsicID
    Description     : Read the HW ID. Does not need a DWL instance to run

    Return type     : u32 - the HW ID
------------------------------------------------------------------------------*/
u32 DWLReadAsicID()
{
    u32 build = 0;

    /* Set HW info from TB config */
#if defined(DWL_EVALUATION_8170)
    gHwVer = 8170;
#elif defined(DWL_EVALUATION_8190)
    gHwVer = 8190;
#elif defined(DWL_EVALUATION_9170)
    gHwVer = 9170;
#elif defined(DWL_EVALUATION_9190)
    gHwVer = 9190;
#elif defined(DWL_EVALUATION_G1)
    gHwVer = 10000;
#else
    gHwVer = tbCfg.decParams.hwVersion ? tbCfg.decParams.hwVersion : 10000;
#endif
    build = tbCfg.decParams.hwBuild;

    build = (build / 1000)     * 0x1000 +
            ((build / 100) % 10) * 0x100 +
            ((build / 10) % 10)  * 0x10 +
            ((build) % 10)     * 0x1;

    switch (gHwVer)
    {
    case 8190:
        return 0x81900000 + build;
    case 9170:
        return 0x91700000 + build;
    case 9190:
        return 0x91900000 + build;
    case 10000:
    case 6731:
    default:
        return 0x6e640000 + build;
    }
}

/*------------------------------------------------------------------------------
    Function name   : DWLReadAsicConfig
    Description     : Read HW configuration. Does not need a DWL instance to run

    Returns     : DWLHwConfig_t *pHwCfg - structure with HW configuration
------------------------------------------------------------------------------*/
void DWLReadAsicConfig(DWLHwConfig_t *pHwCfg)
{
    assert(pHwCfg != NULL);

    TBGetHwConfig(&tbCfg, pHwCfg);

    if (gHwVer < 8190 || pHwCfg->h264Support == H264_BASELINE_PROFILE)
    {
        h264HighSupport = 0;
    }

    /* if HW config (or tb.cfg here) says that SupportH264="10" -> keep it
     * like that. This does not mean MAIN profile support but is used to
     * cut down bus load by disabling direct mv writing in certain
     * applications */
    if (pHwCfg->h264Support && pHwCfg->h264Support != 2)
    {
        pHwCfg->h264Support =
            h264HighSupport ? H264_HIGH_PROFILE : H264_BASELINE_PROFILE;
    }

    /* Apply fuse limitations */
    {
        u32 asicID;
        u32 deInterlace;
        u32 alphaBlend;
        u32 deInterlaceFuse;
        u32 alphaBlendFuse;

        /* check the HW version */
        asicID = DWLReadAsicID();
        if (((asicID >> 16) >= 0x8190U) ||
                ((asicID >> 16) == 0x6731U) ||
                ((asicID >> 16) == 0x6e64U))
        {
            DWLHwFuseStatus_t pHwFuseSts;
            /* check fuse status */
            DWLReadAsicFuseStatus(&pHwFuseSts);

            /* Maximum decoding width supported by the HW */
            if (pHwCfg->maxDecPicWidth > pHwFuseSts.maxDecPicWidthFuse)
                pHwCfg->maxDecPicWidth = pHwFuseSts.maxDecPicWidthFuse;
            /* Maximum output width of Post-Processor */
            if (pHwCfg->maxPpOutPicWidth > pHwFuseSts.maxPpOutPicWidthFuse)
                pHwCfg->maxPpOutPicWidth = pHwFuseSts.maxPpOutPicWidthFuse;
            /* h264 */
            if (!pHwFuseSts.h264SupportFuse)
                pHwCfg->h264Support = H264_NOT_SUPPORTED;
            /* mpeg-4 */
            if (!pHwFuseSts.mpeg4SupportFuse)
                pHwCfg->mpeg4Support = MPEG4_NOT_SUPPORTED;
            /* jpeg (baseline && progressive) */
            if (!pHwFuseSts.jpegSupportFuse)
                pHwCfg->jpegSupport = JPEG_NOT_SUPPORTED;
            if (pHwCfg->jpegSupport == JPEG_PROGRESSIVE &&
                    !pHwFuseSts.jpegProgSupportFuse)
                pHwCfg->jpegSupport = JPEG_BASELINE;
            /* mpeg-2 */
            if (!pHwFuseSts.mpeg2SupportFuse)
                pHwCfg->mpeg2Support = MPEG2_NOT_SUPPORTED;
            /* vc-1 */
            if (!pHwFuseSts.vc1SupportFuse)
                pHwCfg->vc1Support = VC1_NOT_SUPPORTED;
            /* vp6 */
            if (!pHwFuseSts.vp6SupportFuse)
                pHwCfg->vp6Support = VP6_NOT_SUPPORTED;
            /* vp7 */
            if (!pHwFuseSts.vp7SupportFuse)
                pHwCfg->vp7Support = VP7_NOT_SUPPORTED;
            /* vp8 */
            if (!pHwFuseSts.vp8SupportFuse)
                pHwCfg->vp8Support = VP8_NOT_SUPPORTED;
            /* webp */
            if (!pHwFuseSts.vp8SupportFuse)
                pHwCfg->webpSupport = WEBP_NOT_SUPPORTED;
            /* avs */
            if (!pHwFuseSts.avsSupportFuse)
                pHwCfg->avsSupport = AVS_NOT_SUPPORTED;
            /* rv */
            if (!pHwFuseSts.rvSupportFuse)
                pHwCfg->rvSupport = RV_NOT_SUPPORTED;
            /* mvc */
            if (!pHwFuseSts.mvcSupportFuse)
                pHwCfg->mvcSupport = MVC_NOT_SUPPORTED;
            /* pp */
            if (!pHwFuseSts.ppSupportFuse)
                pHwCfg->ppSupport = PP_NOT_SUPPORTED;
            /* check the pp config vs fuse status */
            if ((pHwCfg->ppConfig & 0xFC000000) &&
                    ((pHwFuseSts.ppConfigFuse & 0xF0000000) >> 5))
            {
                /* config */
                deInterlace = ((pHwCfg->ppConfig & PP_DEINTERLACING) >> 25);
                alphaBlend = ((pHwCfg->ppConfig & PP_ALPHA_BLENDING) >> 24);
                /* fuse */
                deInterlaceFuse =
                    (((pHwFuseSts.ppConfigFuse >> 5) & PP_DEINTERLACING) >> 25);
                alphaBlendFuse =
                    (((pHwFuseSts.
                       ppConfigFuse >> 5) & PP_ALPHA_BLENDING) >> 24);

                /* check fuse */
                if (deInterlace && !deInterlaceFuse)
                    pHwCfg->ppConfig &= 0xFD000000;
                if (alphaBlend && !alphaBlendFuse)
                    pHwCfg->ppConfig &= 0xFE000000;
            }
            /* sorenson */
            if (!pHwFuseSts.sorensonSparkSupportFuse)
                pHwCfg->sorensonSparkSupport = SORENSON_SPARK_NOT_SUPPORTED;
            /* ref. picture buffer */
            if (!pHwFuseSts.refBufSupportFuse)
                pHwCfg->refBufSupport = REF_BUF_NOT_SUPPORTED;
        }
    }
}

void DWLReadMCAsicConfig(DWLHwConfig_t pHwCfg[MAX_ASIC_CORES])
{
    u32 i, cores = DWLReadAsicCoreCount();
    assert(cores <= MAX_ASIC_CORES);

    /* read core 0  cfg */
    DWLReadAsicConfig(pHwCfg);

    /* ... and replicate first core cfg to all other cores */
    for (i = 1;  i < cores; i++)
        DWLmemcpy(pHwCfg + i, pHwCfg, sizeof(DWLHwConfig_t));
}

/*------------------------------------------------------------------------------
    Function name   : DWLReadAsicFuseStatus
    Description     : Read HW fuse configuration.
                      Does not need a DWL instance to run

    Returns         : *pHwFuseSts - structure with HW fuse configuration
------------------------------------------------------------------------------*/
void DWLReadAsicFuseStatus(DWLHwFuseStatus_t *pHwFuseSts)
{
    assert(pHwFuseSts != NULL);
    /* Maximum decoding width supported by the HW (fuse) */
    pHwFuseSts->maxDecPicWidthFuse = 4096;
    /* Maximum output width of Post-Processor (fuse) */
    pHwFuseSts->maxPpOutPicWidthFuse = 4096;

    pHwFuseSts->h264SupportFuse = H264_FUSE_ENABLED;    /* HW supports H264 */
    pHwFuseSts->mpeg4SupportFuse = MPEG4_FUSE_ENABLED;  /* HW supports MPEG-4 */
    /* HW supports MPEG-2/MPEG-1 */
    pHwFuseSts->mpeg2SupportFuse = MPEG2_FUSE_ENABLED;
    /* HW supports Sorenson Spark */
    pHwFuseSts->sorensonSparkSupportFuse = SORENSON_SPARK_ENABLED;
    /* HW supports baseline JPEG */
    pHwFuseSts->jpegSupportFuse = JPEG_FUSE_ENABLED;
    pHwFuseSts->vp6SupportFuse = VP6_FUSE_ENABLED;  /* HW supports VP6 */
    pHwFuseSts->vc1SupportFuse = VC1_FUSE_ENABLED;  /* HW supports VC-1 */
    /* HW supports progressive JPEG */
    pHwFuseSts->jpegProgSupportFuse = JPEG_PROGRESSIVE_FUSE_ENABLED;
    pHwFuseSts->refBufSupportFuse = REF_BUF_FUSE_ENABLED;
    pHwFuseSts->avsSupportFuse = AVS_FUSE_ENABLED;
    pHwFuseSts->rvSupportFuse = RV_FUSE_ENABLED;
    pHwFuseSts->vp7SupportFuse = VP7_FUSE_ENABLED;
    pHwFuseSts->vp8SupportFuse = VP8_FUSE_ENABLED;
    pHwFuseSts->mvcSupportFuse = MVC_FUSE_ENABLED;

    /* PP fuses */
    pHwFuseSts->ppSupportFuse = PP_FUSE_ENABLED;    /* HW supports PP */
    /* PP fuse has all optional functions */
    pHwFuseSts->ppConfigFuse = PP_FUSE_DEINTERLACING_ENABLED |
                               PP_FUSE_ALPHA_BLENDING_ENABLED |
                               MAX_PP_OUT_WIDHT_1920_FUSE_ENABLED;

}

/*------------------------------------------------------------------------------
    Function name   : DWLInit
    Description     : Initialize a DWL instance

    Return type     : const void * - pointer to a DWL instance

    Argument        : DWLInitParam_t * param - initialization params
------------------------------------------------------------------------------*/
const void *DWLInit(DWLInitParam_t *param)
{
    DWLInstance_t *dwlInst;
    unsigned int i;

    dwlInst = (DWLInstance_t *) calloc(1, sizeof(DWLInstance_t));
    if (dwlInst == NULL)
        return NULL;
    dwlInst->referenceTotal = 0;
    dwlInst->linearTotal = 0;

    switch (param->clientType)
    {
    case DWL_CLIENT_TYPE_H264_DEC:
        sysprintf("DWL initialized by an H264 decoder instance...\n");
        break;
    case DWL_CLIENT_TYPE_MPEG4_DEC:
        sysprintf("DWL initialized by an MPEG4 decoder instance...\n");
        break;
    case DWL_CLIENT_TYPE_JPEG_DEC:
        sysprintf("DWL initialized by a JPEG decoder instance...\n");
        break;
    case DWL_CLIENT_TYPE_PP:
        sysprintf("DWL initialized by a PP instance...\n");
        break;
    case DWL_CLIENT_TYPE_VC1_DEC:
        sysprintf("DWL initialized by an VC1 decoder instance...\n");
        break;
    case DWL_CLIENT_TYPE_MPEG2_DEC:
        sysprintf("DWL initialized by an MPEG2 decoder instance...\n");
        break;
    case DWL_CLIENT_TYPE_AVS_DEC:
        sysprintf("DWL initialized by an AVS decoder instance...\n");
        break;
    case DWL_CLIENT_TYPE_RV_DEC:
        sysprintf("DWL initialized by an RV decoder instance...\n");
        break;
    case DWL_CLIENT_TYPE_VP6_DEC:
        sysprintf("DWL initialized by a VP6 decoder instance...\n");
        break;
    case DWL_CLIENT_TYPE_VP8_DEC:
        sysprintf("DWL initialized by a VP8 decoder instance...\n");
        break;
    default:
        sysprintf("ERROR: DWL client type has to be always specified!\n");
        free(dwlInst);
        return NULL;
    }

#ifdef INTERNAL_TEST
    InternalTestInit();
#endif

    dwlInst->clientType = param->clientType;
    dwlInst->pFrmBase = NULL;
    dwlInst->pFreeRefFrmMem = NULL;

    //pthread_mutex_lock(&dwl_init_mutex);
    /* Allocate cores just once */
    if (!nDwlInstanceCount)
    {
        //pthread_attr_t attr;
        //pthread_attr_init(&attr);
        //pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        gHwCoreArray = initialize_core_array();
        if (gHwCoreArray == NULL)
        {
            //pthread_attr_destroy(&attr);
            free(dwlInst);
            dwlInst = NULL;
            return NULL;
        }

        listenerThreadParams.nDecCores = get_core_count();
        listenerThreadParams.nPPCores  = 1; /* no multi-core support */

        for (i = 0; i < listenerThreadParams.nDecCores; i++)
        {
            core c = get_core_by_id(gHwCoreArray, i);

            listenerThreadParams.pRegBase[i] = hw_core_get_base_address(c);

            listenerThreadParams.pCallback[i] = NULL;
        }

        listenerThreadParams.bStopped = 0;

        //pthread_create(&mc_listener_thread, &attr, thread_mc_listener,
        //                 &listenerThreadParams);

        //pthread_attr_destroy(&attr);
    }

    dwlInst->hwCoreArray = gHwCoreArray;
    nDwlInstanceCount++;

    //pthread_mutex_unlock(&dwl_init_mutex);

    return (void *) dwlInst;
}

/*------------------------------------------------------------------------------
    Function name   : DWLRelease
    Description     : Release a DWl instance

    Return type     : i32 - 0 for success or a negative error code

    Argument        : const void * instance - instance to be released
------------------------------------------------------------------------------*/
i32 DWLRelease(const void *instance)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    unsigned int i;

    assert(dwlInst != NULL);

    assert(dwlInst->referenceTotal == 0);
    assert(dwlInst->referenceAllocCount == 0);
    assert(dwlInst->linearTotal == 0);
    assert(dwlInst->linearAllocCount == 0);


    //pthread_mutex_lock(&dwl_init_mutex);

#ifdef INTERNAL_TEST
    InternalTestFinalize();
#endif

    nDwlInstanceCount--;

    /* Release the signal handling and cores just when
     * nobody is referencing them anymore
     */
    if (!nDwlInstanceCount)
    {
        listenerThreadParams.bStopped = 1;

        stop_core_array(gHwCoreArray);

        //pthread_join(mc_listener_thread, NULL);

        release_core_array(gHwCoreArray);
    }

    //pthread_mutex_unlock(&dwl_init_mutex);

    free((void *) dwlInst);

#ifdef _DWL_PERFORMANCE
    sysprintf("Total allocated reference mem = %8d\n", referenceTotalMax);
    sysprintf("Total allocated linear mem    = %8d\n", linearTotalMax);
    sysprintf("Total allocated SWSW mem      = %8d\n", mallocTotalMax);
#endif

    /* print core usage stats */
    {
        u32 totalUsage = 0;
        u32 cores = DWLReadAsicCoreCount();
        for (i = 0; i < cores; i++)
            totalUsage += coreUsageCounts[i];
        /* avoid zero division */
        totalUsage = totalUsage ? totalUsage : 1;

        sysprintf("\nMulti-core usage statistics:\n");
        for (i = 0; i < cores; i++)
            sysprintf("\tCore[%2d] used %6d times (%2d%%)\n",
                      i, coreUsageCounts[i],
                      (coreUsageCounts[i] * 100) / totalUsage);

        sysprintf("\n");
    }

    return DWL_OK;
}


void DWLSetIRQCallback(const void *instance, i32 coreID,
                       DWLIRQCallbackFn *pCallbackFn, void *arg)
{
    UNUSED(instance);
    listenerThreadParams.pCallback[coreID] = pCallbackFn;
    listenerThreadParams.pCallbackArg[coreID] = arg;
}


/*------------------------------------------------------------------------------
    Function name   : DWLMallocRefFrm
    Description     : Allocate a frame buffer (contiguous linear RAM memory)

    Return type     : i32 - 0 for success or a negative error code

    Argument        : const void * instance - DWL instance
    Argument        : u32 size - size in bytes of the requested memory
    Argument        : DWLLinearMem_t *info - place where the allocated memory
                        buffer parameters are returned
------------------------------------------------------------------------------*/
i32 DWLMallocRefFrm(const void *instance, u32 size, DWLLinearMem_t *info)
{

    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    extern TBCfg tbCfg;

    if (DWLTestRandomFail())
    {
        return DWL_ERROR;
    }
#ifdef ASIC_TRACE_SUPPORT
    u32 memorySize;

    sysprintf("DWLMallocRefFrm: %8d\n", size);

    if (dwlInst->pFrmBase == NULL)
    {
        if (tbCfg.tbParams.refFrmBufferSize == -1)
        {
            /* Max value based on limits set by Level 5.2 */
            /* Use tb.cfg to overwrite this limit */
            u32 cores = DWLReadAsicCoreCount();
            u32 maxFrameBuffers = (36864 * (5 + 1 + cores)) * (384 + 64);

            memorySize = MIN(maxFrameBuffers, (16 + 1 + cores) * size);
        }
        else
        {
            /* Use tb.cfg to set max size for nonconforming streams */
            memorySize = tbCfg.tbParams.refFrmBufferSize;
        }

        dwlInst->pFrmBase = (u8 *)malloc(memorySize);
        if (dwlInst->pFrmBase == NULL)
            return DWL_ERROR;

        dwlInst->referenceTotal = 0;
        dwlInst->referenceMaximum  = memorySize;
        dwlInst->pFreeRefFrmMem = dwlInst->pFrmBase;

        /* for DPB offset tracing */
        dpbBaseAddress = (u8 *) dwlInst->pFrmBase;
    }

    /* Check that we have enough memory to spare */
    if (dwlInst->referenceTotal + size > dwlInst->referenceMaximum)
        return DWL_ERROR;

    info->virtualAddress = (u32 *) dwlInst->pFreeRefFrmMem;
    info->busAddress = (g1_addr_t) info->virtualAddress;
    info->size = size;

    dwlInst->pFreeRefFrmMem += size;
#else
    sysprintf("DWLMallocRefFrm: %8d\n", size);
    info->virtualAddress = (u32 *) malloc(size);
    if (info->virtualAddress == NULL)
        return DWL_ERROR;
    info->busAddress = (g1_addr_t) info->virtualAddress;
    info->size = size;
#endif  /* ASIC_TRACE_SUPPORT */

#ifdef _DWL_PERFORMANCE
    referenceTotalMax += size;
#endif /* _DWL_PERFORMANCE */

    dwlInst->referenceTotal += size;
    dwlInst->referenceAllocCount++;
    sysprintf("DWLMallocRefFrm: memory allocated %8d bytes in %2d buffers\n",
              dwlInst->referenceTotal, dwlInst->referenceAllocCount);
    return DWL_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLFreeRefFrm
    Description     : Release a frame buffer previously allocated with
                        DWLMallocRefFrm.

    Return type     : void

    Argument        : const void * instance - DWL instance
    Argument        : DWLLinearMem_t *info - frame buffer memory information
------------------------------------------------------------------------------*/
void DWLFreeRefFrm(const void *instance, DWLLinearMem_t *info)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    assert(dwlInst != NULL);

    sysprintf("DWLFreeRefFrm: %8d\n", info->size);
    dwlInst->referenceTotal -= info->size;
    dwlInst->referenceAllocCount--;
    sysprintf("DWLFreeRefFrm: not freed %8d bytes in %2d buffers\n",
              dwlInst->referenceTotal, dwlInst->referenceAllocCount);

#ifdef ASIC_TRACE_SUPPORT
    if (dwlInst->pFrmBase)
    {
        free(dwlInst->pFrmBase);
        dwlInst->pFrmBase = NULL;
        dpbBaseAddress = NULL;
    }
#else
    free(info->virtualAddress);
    info->size = 0;
#endif  /* ASIC_TRACE_SUPPORT */
}

/*------------------------------------------------------------------------------
    Function name   : DWLMallocLinear
    Description     : Allocate a contiguous, linear RAM  memory buffer

    Return type     : i32 - 0 for success or a negative error code

    Argument        : const void * instance - DWL instance
    Argument        : u32 size - size in bytes of the requested memory
    Argument        : DWLLinearMem_t *info - place where the allocated
                        memory buffer parameters are returned
------------------------------------------------------------------------------*/
i32 DWLMallocLinear(const void *instance, u32 size, DWLLinearMem_t *info)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;

    if (DWLTestRandomFail())
    {
        return DWL_ERROR;
    }
    info->virtualAddress = calloc(size, 1);
    sysprintf("DWLMallocLinear: %8d\n", size);
    if (info->virtualAddress == NULL)
        return DWL_ERROR;
    info->busAddress = (g1_addr_t) info->virtualAddress;
    info->size = size;
    dwlInst->linearTotal += size;
    dwlInst->linearAllocCount++;
    sysprintf("DWLMallocLinear: allocated total %8d bytes in %2d buffers\n",
              dwlInst->linearTotal, dwlInst->linearAllocCount);

#ifdef _DWL_PERFORMANCE
    linearTotalMax += size;
#endif  /* _DWL_PERFORMANCE */
    return DWL_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLFreeLinear
    Description     : Release a linera memory buffer, previously allocated with
                        DWLMallocLinear.

    Return type     : void
    Argument        : const void * instance - DWL instance
    Argument        : DWLLinearMem_t *info - linear buffer memory information
------------------------------------------------------------------------------*/
void DWLFreeLinear(const void *instance, DWLLinearMem_t *info)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    assert(dwlInst != NULL);

    sysprintf("DWLFreeLinear: %8d\n", info->size);
    dwlInst->linearTotal -= info->size;
    dwlInst->linearAllocCount--;
    sysprintf("DWLFreeLinear: not freed %8d bytes in %2d buffers\n",
              dwlInst->linearTotal, dwlInst->linearAllocCount);
    free(info->virtualAddress);
    info->size = 0;
}

/*------------------------------------------------------------------------------
    Function name   : DWLWriteReg
    Description     : Write a value to a hardware IO register

    Return type     : void

    Argument        : const void * instance - DWL instance
    Argument        : u32 offset - byte offset of the register to be written
    Argument        : u32 value - value to be written out
------------------------------------------------------------------------------*/
void DWLWriteReg(const void *instance, i32 coreID, u32 offset, u32 value)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    core c = get_core_by_id(dwlInst->hwCoreArray, coreID);
    u32 *coreRegBase = hw_core_get_base_address(c);
    if (coreRegBase == NULL)
        return;
#ifndef DWL_DISABLE_REG_PRINTS
    DWL_DEBUG("core[%d] swreg[%d] at offset 0x%02X = %08X\n",
              coreID, offset / 4, offset, value);
#endif
#ifdef INTERNAL_TEST
    InternalTestDumpWriteSwReg(coreID, offset >> 2, value, coreRegBase);
#endif
    assert(offset <= DEC_X170_REGS * 4);

    coreRegBase[offset >> 2] = value;
}

static core lastDecCore = NULL;

/*------------------------------------------------------------------------------
    Function name   : DWLEnableHw
    Description     :
    Return type     : void
    Argument        : const void * instance - DWL instance
    Argument        : u32 offset - byte offset of the register to be written
    Argument        : u32 value - value to be written out
------------------------------------------------------------------------------*/
void DWLEnableHW(const void *instance, i32 coreID, u32 offset, u32 value)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    core c = get_core_by_id(dwlInst->hwCoreArray, coreID);

    if (!IS_PIPELINE_ENABLED(value))
        assert(c == dwlInst->currentCore);

    DWLWriteReg(dwlInst, coreID, offset, value);

#ifndef DWL_DISABLE_REG_PRINTS
    DWL_DEBUG("HW enabled by previous DWLWriteReg\n");
#endif

    if (dwlInst->clientType != DWL_CLIENT_TYPE_PP)
    {
        hw_core_dec_enable(c);
    }
    else
    {
        /* standalone PP start */
        hw_core_pp_enable(c,  lastDecCore != NULL ? 0 : 1);
    }
}

/*------------------------------------------------------------------------------
    Function name   : DWLDisableHw
    Description     :
    Return type     : void
    Argument        : const void * instance - DWL instance
    Argument        : u32 offset - byte offset of the register to be written
    Argument        : u32 value - value to be written out
------------------------------------------------------------------------------*/
void DWLDisableHW(const void *instance, i32 coreID, u32 offset, u32 value)
{
    DWLWriteReg(instance, coreID, offset, value);
#ifndef DWL_DISABLE_REG_PRINTS
    DWL_DEBUG("HW disabled by previous DWLWriteReg\n");
#endif
}

/*------------------------------------------------------------------------------
    Function name   : DWLReadReg
    Description     : Read the value of a hardware IO register
    Return type     : u32 - the value stored in the register
    Argument        : const void * instance - DWL instance
    Argument        : u32 offset - byte offset of the register to be read
------------------------------------------------------------------------------*/
u32 DWLReadReg(const void *instance, i32 coreID, u32 offset)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    core c = get_core_by_id(dwlInst->hwCoreArray, coreID);
    u32 *coreRegBase = hw_core_get_base_address(c);
    if (coreRegBase == NULL)
        return 0xFFFFFFFF;
    u32 val;

#ifdef INTERNAL_TEST
    InternalTestDumpReadSwReg(coreID, offset >> 2, coreRegBase[offset >> 2], coreRegBase);
#endif

    assert(offset <= DEC_X170_REGS * 4);

    val = coreRegBase[offset >> 2];

#ifndef DWL_DISABLE_REG_PRINTS
    DWL_DEBUG("core[%d] swreg[%d] at offset 0x%02X = %08X\n",
              coreID, offset / 4, offset, val);
#endif

    return val;
}

/*------------------------------------------------------------------------------
    Function name   : DWLWaitDecHwReady
    Description     : Wait until decoder hardware has stopped running.
                      Used for synchronizing software runs with the hardware.
                      The wait could succed, timeout, or fail with an error.
    Return type     : i32 - one of the values DWL_HW_WAIT_OK
                                              DWL_HW_WAIT_TIMEOUT
                                              DWL_HW_WAIT_ERROR
    Argument        : const void * instance - DWL instance
------------------------------------------------------------------------------*/
i32 DWLWaitDecHwReady(const void *instance, i32 coreID, u32 timeout)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    core c = get_core_by_id(dwlInst->hwCoreArray, coreID);

    UNUSED(timeout);

    assert(c == dwlInst->currentCore);

    if (hw_core_wait_dec_rdy(c) != 0)
    {
        return (i32) DWL_HW_WAIT_ERROR;
    }

    return (i32) DWL_HW_WAIT_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLWaitPpHwReady
    Description     : Wait until hardware has stopped running.
                      Used for synchronizing software runs with the hardware.
                      The wait could succed, timeout, or fail with an error.
    Return type     : i32 - one of the values DWL_HW_WAIT_OK
                                              DWL_HW_WAIT_TIMEOUT
                                              DWL_HW_WAIT_ERROR
    Argument        : const void * instance - DWL instance
------------------------------------------------------------------------------*/
i32 DWLWaitPpHwReady(const void *instance, i32 coreID, u32 timeout)
{

    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    core c = get_core_by_id(dwlInst->hwCoreArray, coreID);

    UNUSED(timeout);

    assert(c == dwlInst->currentCore);

    if (hw_core_wait_pp_rdy(c) != 0)
    {
        return (i32) DWL_HW_WAIT_ERROR;
    }

#ifdef ASIC_TRACE_SUPPORT
    /* update swregister_accesses.trc */
    trace_WaitPpEnd();
#endif

    return (i32) DWL_HW_WAIT_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLWaitHwReady
    Description     : Wait until hardware has stopped running.
                      Used for synchronizing software runs with the hardware.
                      The wait could succed, timeout, or fail with an error.
    Return type     : i32 - one of the values DWL_HW_WAIT_OK
                                              DWL_HW_WAIT_TIMEOUT
                                              DWL_HW_WAIT_ERROR
    Argument        : const void * instance - DWL instance
------------------------------------------------------------------------------*/
i32 DWLWaitHwReady(const void *instance, i32 coreID, u32 timeout)
{
    const DWLInstance_t *dec_dwl = (DWLInstance_t *) instance;

    i32 ret;
    assert(dec_dwl);
    switch (dec_dwl->clientType)
    {
    case DWL_CLIENT_TYPE_H264_DEC:
    case DWL_CLIENT_TYPE_MPEG4_DEC:
    case DWL_CLIENT_TYPE_JPEG_DEC:
    case DWL_CLIENT_TYPE_VC1_DEC:
    case DWL_CLIENT_TYPE_MPEG2_DEC:
    case DWL_CLIENT_TYPE_AVS_DEC:
    case DWL_CLIENT_TYPE_RV_DEC:
    case DWL_CLIENT_TYPE_VP6_DEC:
    case DWL_CLIENT_TYPE_VP8_DEC:
        ret = DWLWaitDecHwReady(dec_dwl, coreID, timeout);
        break;
    case DWL_CLIENT_TYPE_PP:
        ret = DWLWaitPpHwReady(dec_dwl, coreID, timeout);
        break;
    default:
        assert(0);  /* should not happen */
        ret = DWL_HW_WAIT_ERROR;
        break;
    }

    return ret;
}

/*------------------------------------------------------------------------------
    Function name   : DWLmalloc
    Description     : Allocate a memory block. Same functionality as
                      the ANSI C malloc()
    Return type     : void pointer to the allocated space, or NULL if there
                      is insufficient memory available
    Argument        : u32 n - Bytes to allocate
------------------------------------------------------------------------------*/
void *DWLmalloc(u32 n)
{
    if (DWLTestRandomFail())
    {
        return NULL;
    }
    DWL_DEBUG("%8d\n", n);

#ifdef _DWL_PERFORMANCE
    mallocTotalMax += n;
#endif  /* _DWL_PERFORMANCE */

    return malloc((size_t) n);
}

/*------------------------------------------------------------------------------
    Function name   : DWLfree
    Description     : Deallocates or frees a memory block. Same functionality as
                      the ANSI C free()
    Return type     : void
    Argument        : void *p - Previously allocated memory block to be freed
------------------------------------------------------------------------------*/
void DWLfree(void *p)
{
    free(p);
}

/*------------------------------------------------------------------------------
    Function name   : DWLcalloc
    Description     : Allocates an array in memory with elements initialized
                      to 0. Same functionality as the ANSI C calloc()
    Return type     : void pointer to the allocated space, or NULL if there
                      is insufficient memory available
    Argument        : u32 n - Number of elements
    Argument        : u32 s - Length in bytes of each element.
------------------------------------------------------------------------------*/
void *DWLcalloc(u32 n, u32 s)
{
    if (DWLTestRandomFail())
    {
        return NULL;
    }
    DWL_DEBUG("%8d\n", n * s);
#ifdef _DWL_PERFORMANCE
    mallocTotalMax += n * s;
#endif  /* _DWL_PERFORMANCE */

    return calloc((size_t) n, (size_t) s);
}

/*------------------------------------------------------------------------------
    Function name   : DWLmemcpy
    Description     : Copies characters between buffers. Same functionality as
                      the ANSI C memcpy()
    Return type     : The value of destination d
    Argument        : void *d - Destination buffer
    Argument        : const void *s - Buffer to copy from
    Argument        : u32 n - Number of bytes to copy
------------------------------------------------------------------------------*/
void *DWLmemcpy(void *d, const void *s, u32 n)
{
    return memcpy(d, s, (size_t) n);
}

/*------------------------------------------------------------------------------
    Function name   : DWLmemset
    Description     : Sets buffers to a specified character. Same functionality
                      as the ANSI C memset()
    Return type     : The value of destination d
    Argument        : void *d - Pointer to destination
    Argument        : i32 c - Character to set
    Argument        : u32 n - Number of characters
------------------------------------------------------------------------------*/
void *DWLmemset(void *d, i32 c, u32 n)
{
    return memset(d, (int) c, (size_t) n);
}

/*------------------------------------------------------------------------------
    Function name   : DWLReserveHw
    Description     :
    Return type     : i32
    Argument        : const void *instance
------------------------------------------------------------------------------*/
i32 DWLReserveHw(const void *instance, i32 *coreID)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;

    if (dwlInst->clientType == DWL_CLIENT_TYPE_PP)
    {
        //pthread_mutex_lock(&pp_mutex);

        if (lastDecCore == NULL)
            /* Blocks until core available. */
            dwlInst->currentCore = borrow_hw_core(dwlInst->hwCoreArray);
        else
            /* We rely on the fact that in combined mode the PP is always reserved
             * after the decoder
             */
            dwlInst->currentCore = lastDecCore;
    }
    else
    {
        /* Blocks until core available. */
        dwlInst->currentCore = borrow_hw_core(dwlInst->hwCoreArray);
        lastDecCore = dwlInst->currentCore;
    }

    *coreID = hw_core_getid(dwlInst->currentCore);
    DWL_DEBUG("Reserved %s core %d\n",
              dwlInst->clientType == DWL_CLIENT_TYPE_PP ? "PP" : "DEC",
              *coreID);

    coreUsageCounts[*coreID]++;

    return DWL_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLReserveHwPipe
    Description     : Reserve both DEC and PP on same core for pipeline
    Return type     : i32
    Argument        : const void *instance
------------------------------------------------------------------------------*/
i32 DWLReserveHwPipe(const void *instance, i32 *coreID)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;

    /* only decoder can reserve a DEC+PP hardware for pipelined operation */
    assert(dwlInst->clientType != DWL_CLIENT_TYPE_PP);

    /* Blocks until core available. */
    dwlInst->currentCore = borrow_hw_core(dwlInst->hwCoreArray);
    lastDecCore = dwlInst->currentCore;

    /* lock PP also */
    //pthread_mutex_lock(&pp_mutex);

    dwlInst->bReservedPipe = 1;

    *coreID = hw_core_getid(dwlInst->currentCore);
    DWL_DEBUG("Reserved DEC+PP core %d\n", *coreID);

    coreUsageCounts[*coreID]++;

    return DWL_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLReleaseHw
    Description     :
    Return type     : void
    Argument        : const void *instance
------------------------------------------------------------------------------*/
void DWLReleaseHw(const void *instance, i32 coreID)
{
    DWLInstance_t *dwlInst = (DWLInstance_t *) instance;
    core c = get_core_by_id(dwlInst->hwCoreArray, coreID);

    if (dwlInst->clientType == DWL_CLIENT_TYPE_PP)
    {
        DWL_DEBUG("Released PP core %d\n", coreID);

        //pthread_mutex_unlock(&pp_mutex);

        /* core will be released by decoder */
        if (lastDecCore != NULL)
            return;
    }

    /* PP reserved by decoder in DWLReserveHwPipe */
    //if(dwlInst->bReservedPipe)
    //    pthread_mutex_unlock(&pp_mutex);

    dwlInst->bReservedPipe = 0;

    return_hw_core(dwlInst->hwCoreArray, c);
    lastDecCore = NULL;
    DWL_DEBUG("Released %s core %d\n",
              dwlInst->clientType == DWL_CLIENT_TYPE_PP ? "PP" : "DEC",
              coreID);
}

/*------------------------------------------------------------------------------
    Function name   : DWLReadAsicCoreCount
    Description     : Return number of ASIC cores, static implementation
    Return type     : u32
    Argument        : void
------------------------------------------------------------------------------*/
u32 DWLReadAsicCoreCount(void)
{
    return get_core_count();
}

i32 DWLTestRandomFail(void)
{
#ifdef FAIL_DURING_ALLOC
    if (!failedAllocCount)
    {
        srand(time(NULL));
    }
    failedAllocCount++;

    /* If fail preset to this alloc occurance, failt it */
    if (failedAllocCount == FAIL_DURING_ALLOC)
    {
        sysprintf("DWL: Preset allocation fail during alloc %d\n",
                  failedAllocCount);
        return DWL_ERROR;
    }
    /* If failing point is preset, no randomization */
    if (FAIL_DURING_ALLOC > 0)
        return DWL_OK;

    if ((rand() % 100) > 90)
    {
        sysprintf("DWL: Testing a failure in memory allocation number %d\n",
                  failedAllocCount);
        return DWL_ERROR;
    }
    else
    {
        return DWL_OK;
    }
#endif
    return DWL_OK;
}
