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
--  Abstract : Decode Supplemental Enhancement Information (SEI) from the stream
--
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Table of contents

     1. Include headers
     2. External compiler flags
     3. Module defines
     4. Local function prototypes
     5. Functions
          h264bsdDecodeSeiParameters
          h264bsdDecodeBufferingPeriodInfo
          h264bsdDecodePicTimingInfo
          h264bsdComputeTimes

------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "dwl.h"
#include "basetype.h"
#include "h264hwd_vlc.h"
#include "h264hwd_stream.h"
#include "h264hwd_util.h"
#include "h264hwd_sei.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function: h264bsdDecodeSEIParameters

        Functional description:
            Decode SEI parameters from the stream. See standard for details.

        Inputs:
            pStrmData       pointer to stream data structure

        Outputs:
            pSeiParameters  decoded information is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data or end of stream

------------------------------------------------------------------------------*/

u32 h264bsdDecodeSeiParameters(seqParamSet_t **sps, strmData_t *pStrmData,
                               seiParameters_t *pSeiParameters)
{

    /* Variables */

    u32 tmp;
    u32 payLoadType = 0;
    u32 payLoadSize = 0;
    u32 lastPayLoadTypeByte;
    u32 lastPayLoadSizeByte;
    strmData_t tmpStrmData;

    /* Code */

    ASSERT(pStrmData);
    ASSERT(pSeiParameters);

    do
    {
        payLoadType = 0;

        while (h264bsdShowBits(pStrmData, 8) == 0xFF)
        {
            payLoadType += 255;
            if (h264bsdFlushBits(pStrmData, 8) == END_OF_STREAM)
                return (END_OF_STREAM);
        }

        tmp = h264bsdGetBits(pStrmData, 8);
        if (tmp == END_OF_STREAM)
            return (END_OF_STREAM);
        lastPayLoadTypeByte = tmp;
        payLoadType += lastPayLoadTypeByte;

        payLoadSize = 0;

        while (h264bsdShowBits(pStrmData, 8) == 0xFF)
        {
            payLoadSize += 255;
            if (h264bsdFlushBits(pStrmData, 8) == END_OF_STREAM)
                return (END_OF_STREAM);
        }

        tmp = h264bsdGetBits(pStrmData, 8);
        if (tmp == END_OF_STREAM)
            return (END_OF_STREAM);
        lastPayLoadSizeByte = tmp;
        payLoadSize += lastPayLoadSizeByte;

        tmpStrmData = *pStrmData;
        switch (payLoadType)
        {
        case SEI_BUFFERING_PERIOD:
            h264bsdDecodeBufferingPeriodInfo(sps, &tmpStrmData,
                                             &pSeiParameters->bufferingPeriodInfo);
            if (tmp == HANTRO_NOK)
            {
                pSeiParameters->bufferingPeriodInfo.existFlag = 0;
                return (HANTRO_NOK);
            }
            else
                pSeiParameters->bufferingPeriodInfo.existFlag = 1;
            break;

        case SEI_PIC_TIMING:
            h264bsdDecodePicTimingInfo(sps,
                                       &tmpStrmData, &pSeiParameters->picTimingInfo,
                                       &pSeiParameters->bufferingPeriodInfo);
            if (tmp == HANTRO_NOK)
            {
                pSeiParameters->picTimingInfo.existFlag = 0;
                return (HANTRO_NOK);
            }
            else
                pSeiParameters->picTimingInfo.existFlag = 1;
            break;

        case SEI_PAN_SCAN_RECT:
            break;

        case SEI_FILLER_PAYLOAD:
            break;

        case SEI_USER_DATA_REGISTERED_ITU_T_T35:
            break;

        case SEI_USER_DATA_UNREGISTERED:
            break;

        case SEI_RECOVERY_POINT:
            break;

        case SEI_DEC_REF_PIC_MARKING_REPETITION:
            break;

        case SEI_SPARE_PIC:
            break;

        case SEI_SCENE_INFO:
            break;

        case SEI_SUB_SEQ_INFO:
            break;

        case SEI_SUB_SEQ_LAYER_CHARACTERISTICS:
            break;

        case SEI_SUB_SEQ_CHARACTERISTICS:
            break;

        case SEI_FULL_FRAME_FREEZE:
            break;

        case SEI_FULL_FRAME_FREEZE_RELEASE:
            break;

        case SEI_FULL_FRAME_SNAPSHOT:
            break;

        case SEI_PROGRESSIVE_REFINEMENT_SEGMENT_START:
            break;

        case SEI_PROGRESSIVE_REFINEMENT_SEGMENT_END:
            break;

        case SEI_MOTION_CONSTRAINED_SLICE_GROUP_SET:
            break;

        case SEI_FILM_GRAIN_CHARACTERISTICS:
            break;

        case SEI_DEBLOCKING_FILTER_DISPLAY_PREFERENCE:
            break;

        case SEI_STEREO_VIDEO_INFO:
            break;

        case SEI_TONE_MAPPING:
            break;

        case SEI_POST_FILTER_HINTS:
            break;

        case SEI_FRAME_PACKING_ARRANGEMENT:
            break;

        case SEI_GREEN_METADATA:
            break;

        default:
            break;
        }

        if (h264bsdFlushBits(pStrmData, payLoadSize * 8) == END_OF_STREAM)
            return (END_OF_STREAM);


    }
    while (h264bsdMoreRbspData(pStrmData));

    return (HANTRO_OK);
}


u32 h264bsdDecodeBufferingPeriodInfo(seqParamSet_t **sps,
                                     strmData_t *pStrmData,
                                     bufferingPeriodInfo_t *pBufferingPeriodInfo)
{

    /* Variables */

    u32 tmp, i;

    /* Code */

    ASSERT(pStrmData);
    ASSERT(pBufferingPeriodInfo);
    ASSERT(sps);

    seqParamSet_t *pSeqParamSet;
    (void) DWLmemset(pBufferingPeriodInfo, 0, sizeof(bufferingPeriodInfo_t));

    pBufferingPeriodInfo->seqParameterSetId =
        tmp = h264bsdDecodeExpGolombUnsigned(pStrmData,
                &pBufferingPeriodInfo->seqParameterSetId);
    if (tmp != HANTRO_OK)
        return (tmp);

    pSeqParamSet = sps[pBufferingPeriodInfo->seqParameterSetId];
    if (pSeqParamSet == NULL || pSeqParamSet->vuiParameters == NULL)
        return (HANTRO_NOK);
    if (pSeqParamSet->vuiParameters->errorHRDParameterFlag)
        return (HANTRO_NOK);

    if (pSeqParamSet->vuiParametersPresentFlag)
    {
        if (pSeqParamSet->vuiParameters->nalHrdParametersPresentFlag)
        {

            for (i = 0; i < pSeqParamSet->vuiParameters->nalHrdParameters.cpbCnt; i++)
            {
                tmp = h264bsdShowBits(pStrmData,
                                      pSeqParamSet->vuiParameters->nalHrdParameters.initialCpbRemovalDelayLength);
                if (h264bsdFlushBits(pStrmData,
                                     pSeqParamSet->vuiParameters->nalHrdParameters.initialCpbRemovalDelayLength) == END_OF_STREAM)
                    return (END_OF_STREAM);
                pBufferingPeriodInfo->initialCpbRemovalDelay[i] = tmp;

                tmp = h264bsdShowBits(pStrmData,
                                      pSeqParamSet->vuiParameters->nalHrdParameters.initialCpbRemovalDelayLength);
                if (h264bsdFlushBits(pStrmData,
                                     pSeqParamSet->vuiParameters->nalHrdParameters.initialCpbRemovalDelayLength) == END_OF_STREAM)
                    return (END_OF_STREAM);
                pBufferingPeriodInfo->initialCpbRemovalDelayOffset[i] = tmp;
            }
        }

        if (pSeqParamSet->vuiParameters->vclHrdParametersPresentFlag)
        {

            for (i = 0; i < pSeqParamSet->vuiParameters->vclHrdParameters.cpbCnt; i++)
            {
                tmp = h264bsdShowBits(pStrmData,
                                      pSeqParamSet->vuiParameters->vclHrdParameters.initialCpbRemovalDelayLength);
                if (h264bsdFlushBits(pStrmData,
                                     pSeqParamSet->vuiParameters->vclHrdParameters.initialCpbRemovalDelayLength) == END_OF_STREAM)
                    return (END_OF_STREAM);
                pBufferingPeriodInfo->initialCpbRemovalDelay[i] = tmp;

                tmp = h264bsdShowBits(pStrmData,
                                      pSeqParamSet->vuiParameters->vclHrdParameters.initialCpbRemovalDelayLength);
                if (h264bsdFlushBits(pStrmData,
                                     pSeqParamSet->vuiParameters->vclHrdParameters.initialCpbRemovalDelayLength) == END_OF_STREAM)
                    return (END_OF_STREAM);
                pBufferingPeriodInfo->initialCpbRemovalDelayOffset[i] = tmp;
            }
        }
    }

    return (HANTRO_OK);
}

u32 h264bsdDecodePicTimingInfo(seqParamSet_t **sps, strmData_t *pStrmData,
                               picTimingInfo_t *pPicTimingInfo,
                               bufferingPeriodInfo_t *pBufferingPeriodInfo)
{

    /* Variables */

    u32 tmp, i;
    u32 CpbDpbDelaysPresentFlag;
    u32 cpbRemovalLen = 0;
    u32 dpbOutputLen = 0;
    u32 picStructPresentFlag;
    u32 NumClockTs = 0;
    u32 timeOffsetLength;

    /* Code */

    ASSERT(pStrmData);
    ASSERT(pPicTimingInfo);
    ASSERT(sps);

    (void) DWLmemset(pPicTimingInfo, 0, sizeof(picTimingInfo_t));

    seqParamSet_t *pSeqParamSet = sps[pBufferingPeriodInfo->seqParameterSetId];
    if (pSeqParamSet == NULL || pSeqParamSet->vuiParameters == NULL)
        return (HANTRO_NOK);
    if (pSeqParamSet->vuiParameters->errorHRDParameterFlag)
        return (HANTRO_NOK);

    CpbDpbDelaysPresentFlag = pSeqParamSet->vuiParametersPresentFlag
                              && ((pSeqParamSet->vuiParameters->nalHrdParametersPresentFlag != 0)
                                  || (pSeqParamSet->vuiParameters->vclHrdParametersPresentFlag != 0));

    if (CpbDpbDelaysPresentFlag)
    {
        if (pSeqParamSet->vuiParametersPresentFlag)
        {
            if (pSeqParamSet->vuiParameters->nalHrdParametersPresentFlag)
            {
                cpbRemovalLen =
                    pSeqParamSet->vuiParameters->nalHrdParameters.cpbRemovalDelayLength;
                dpbOutputLen  =
                    pSeqParamSet->vuiParameters->nalHrdParameters.dpbOutputDelayLength;
            }

            if (pSeqParamSet->vuiParameters->vclHrdParametersPresentFlag)
            {
                cpbRemovalLen =
                    pSeqParamSet->vuiParameters->vclHrdParameters.cpbRemovalDelayLength;
                dpbOutputLen  =
                    pSeqParamSet->vuiParameters->vclHrdParameters.dpbOutputDelayLength;
            }
        }

        if (pSeqParamSet->vuiParameters->nalHrdParametersPresentFlag
                || pSeqParamSet->vuiParameters->vclHrdParametersPresentFlag)
        {
            tmp = h264bsdGetBits(pStrmData, cpbRemovalLen);
            if (tmp == END_OF_STREAM)
                return (END_OF_STREAM);
            pPicTimingInfo->cpbRemovalDelay = tmp;

            tmp = h264bsdGetBits(pStrmData, dpbOutputLen);
            if (tmp == END_OF_STREAM)
                return (END_OF_STREAM);
            pPicTimingInfo->dpbOutputDelay = tmp;
        }
    }

    if (!pSeqParamSet->vuiParametersPresentFlag)
        picStructPresentFlag = 0;
    else
        picStructPresentFlag = pSeqParamSet->vuiParameters->picStructPresentFlag;

    if (picStructPresentFlag)
    {
        tmp = h264bsdGetBits(pStrmData, 4);
        if (tmp == END_OF_STREAM)
            return (END_OF_STREAM);
        pPicTimingInfo->picStruct = tmp;

        switch (pPicTimingInfo->picStruct)
        {
        case 0:
        case 1:
        case 2:
            NumClockTs = 1;
            break;

        case 3:
        case 4:
        case 7:
            NumClockTs = 2;
            break;

        case 5:
        case 6:
        case 8:
            NumClockTs = 3;
            break;
        default:
            break;
        }

        for (i = 0; i < NumClockTs; i++)
        {
            tmp = h264bsdGetBits(pStrmData, 1);
            if (tmp == END_OF_STREAM)
                return (END_OF_STREAM);
            pPicTimingInfo->clockTimestampFlag[i] = tmp;

            if (pPicTimingInfo->clockTimestampFlag[i])
            {
                tmp = h264bsdGetBits(pStrmData, 2);
                if (tmp == END_OF_STREAM)
                    return (END_OF_STREAM);
                pPicTimingInfo->ctType = tmp;

                tmp = h264bsdGetBits(pStrmData, 1);
                if (tmp == END_OF_STREAM)
                    return (END_OF_STREAM);
                pPicTimingInfo->nuitFieldBasedFlag = tmp;

                tmp = h264bsdGetBits(pStrmData, 5);
                if (tmp == END_OF_STREAM)
                    return (END_OF_STREAM);
                pPicTimingInfo->countingType = tmp;

                tmp = h264bsdGetBits(pStrmData, 1);
                if (tmp == END_OF_STREAM)
                    return (END_OF_STREAM);
                pPicTimingInfo->fullTimestampFlag = tmp;

                tmp = h264bsdGetBits(pStrmData, 1);
                if (tmp == END_OF_STREAM)
                    return (END_OF_STREAM);
                pPicTimingInfo->discontinuityFlag = tmp;

                tmp = h264bsdGetBits(pStrmData, 1);
                if (tmp == END_OF_STREAM)
                    return (END_OF_STREAM);
                pPicTimingInfo->cntDroppedFlag = tmp;

                tmp = h264bsdGetBits(pStrmData, 8);
                if (tmp == END_OF_STREAM)
                    return (END_OF_STREAM);
                pPicTimingInfo->nFrames = tmp;

                if (pPicTimingInfo->fullTimestampFlag)
                {
                    tmp = h264bsdGetBits(pStrmData, 6);
                    if (tmp == END_OF_STREAM)
                        return (END_OF_STREAM);
                    pPicTimingInfo->secondsValue = tmp;

                    tmp = h264bsdGetBits(pStrmData, 6);
                    if (tmp == END_OF_STREAM)
                        return (END_OF_STREAM);
                    pPicTimingInfo->minutesValue = tmp;

                    tmp = h264bsdGetBits(pStrmData, 5);
                    if (tmp == END_OF_STREAM)
                        return (END_OF_STREAM);
                    pPicTimingInfo->hoursValue = tmp;
                }
                else
                {
                    tmp = h264bsdGetBits(pStrmData, 1);
                    if (tmp == END_OF_STREAM)
                        return (END_OF_STREAM);
                    pPicTimingInfo->secondsFlag = tmp;

                    if (pPicTimingInfo->secondsFlag)
                    {
                        tmp = h264bsdGetBits(pStrmData, 6);
                        if (tmp == END_OF_STREAM)
                            return (END_OF_STREAM);
                        pPicTimingInfo->secondsValue = tmp;

                        tmp = h264bsdGetBits(pStrmData, 1);
                        if (tmp == END_OF_STREAM)
                            return (END_OF_STREAM);
                        pPicTimingInfo->minutesFlag = tmp;

                        if (pPicTimingInfo->minutesFlag)
                        {
                            tmp = h264bsdGetBits(pStrmData, 6);
                            if (tmp == END_OF_STREAM)
                                return (END_OF_STREAM);
                            pPicTimingInfo->minutesValue = tmp;

                            tmp = h264bsdGetBits(pStrmData, 1);
                            if (tmp == END_OF_STREAM)
                                return (END_OF_STREAM);
                            pPicTimingInfo->hoursFlag = tmp;

                            if (pPicTimingInfo->hoursFlag)
                            {
                                tmp = h264bsdGetBits(pStrmData, 5);
                                if (tmp == END_OF_STREAM)
                                    return (END_OF_STREAM);
                                pPicTimingInfo->minutesValue = tmp;
                            }
                        }
                    }
                }

                if (pSeqParamSet->vuiParameters->vclHrdParametersPresentFlag)
                {
                    timeOffsetLength =
                        pSeqParamSet->vuiParameters->vclHrdParameters.timeOffsetLength;
                }
                else if (pSeqParamSet->vuiParameters->nalHrdParametersPresentFlag)
                {
                    timeOffsetLength =
                        pSeqParamSet->vuiParameters->nalHrdParameters.timeOffsetLength;
                }
                else
                    timeOffsetLength = 24;

                if (timeOffsetLength)
                {
                    tmp = h264bsdGetBits(pStrmData, 5);
                    if (tmp == END_OF_STREAM)
                        return (END_OF_STREAM);
                    pPicTimingInfo->timeOffset = (i32)tmp;
                }
                else
                    pPicTimingInfo->timeOffset = 0;

            }
        }
    }
    return (HANTRO_OK);
}

double Ceil(double a)
{
    u32 tmp;
    tmp = (u32) a;
    if ((double)(tmp) < a)
        return ((double)(tmp + 1));
    else
        return ((double)(tmp));
}

u32 h264bsdComputeTimes(seqParamSet_t *sps,
                        seiParameters_t *pSeiParameters)
{
    seqParamSet_t *pSeqParamSet = sps;
    computeTimeInfo_t *timeInfo = &pSeiParameters->computeTimeInfo;
    u32 cbrFlag = 0;
    u32 bitRateValue = 0;
    u32 bitRateScale = 0;
    u32 bitRate;

    // compute tc
    if (pSeqParamSet->vuiParameters == NULL)
        return (HANTRO_NOK);

    if (!pSeiParameters->picTimingInfo.existFlag ||
            !pSeiParameters->bufferingPeriodInfo.existFlag)
    {
        pSeiParameters->picTimingInfo.existFlag = 0;
        return (HANTRO_NOK);
    }

    if (!pSeiParameters->picTimingInfo.cpbRemovalDelay &&
            !pSeiParameters->picTimingInfo.dpbOutputDelay)
    {
        pSeiParameters->picTimingInfo.existFlag = 0;
        return (HANTRO_NOK);
    }

    if (timeInfo->isFirstAu)
    {
        if (pSeqParamSet->vuiParameters->updateHRDParameterFlag)
            timeInfo->hrdInitFlag = 1;
        else
            timeInfo->hrdInitFlag = 0;
        pSeqParamSet->vuiParameters->updateHRDParameterFlag = 0;
    }
    if (pSeqParamSet->vuiParameters->timingInfoPresentFlag)
        timeInfo->clockTick =
            (double)pSeqParamSet->vuiParameters->numUnitsInTick /
            pSeqParamSet->vuiParameters->timeScale;
    else
        timeInfo->clockTick = 0;

    if (pSeqParamSet->vuiParameters->nalHrdParametersPresentFlag)
    {
        cbrFlag = pSeqParamSet->vuiParameters->nalHrdParameters.cbrFlag[0];
        bitRateScale = pSeqParamSet->vuiParameters->nalHrdParameters.bitRateScale;
        bitRateValue = pSeqParamSet->vuiParameters->nalHrdParameters.bitRateValue[0];
    }

    else
    {
        cbrFlag = pSeqParamSet->vuiParameters->vclHrdParameters.cbrFlag[0];
        bitRateScale = pSeqParamSet->vuiParameters->vclHrdParameters.bitRateScale;
        bitRateValue = pSeqParamSet->vuiParameters->vclHrdParameters.bitRateValue[0];
    }

    //compute trn

    if (timeInfo->isFirstAu)
    {
        if (timeInfo->hrdInitFlag)
        {
            timeInfo->nominalRemovalTime =
                pSeiParameters->bufferingPeriodInfo.initialCpbRemovalDelay[0] / 90000.0;
        }
        else
        {
            timeInfo->nominalRemovalTime = timeInfo->prevNominalRemovalTime +
                                           timeInfo->clockTick * pSeiParameters->picTimingInfo.cpbRemovalDelay;
        }
        timeInfo->prevNominalRemovalTime = timeInfo->nominalRemovalTime;
        timeInfo->nominalRemovalTimeFirst = timeInfo->nominalRemovalTime;
    }
    else
    {
        timeInfo->nominalRemovalTime = timeInfo->nominalRemovalTimeFirst +
                                       timeInfo->clockTick * pSeiParameters->picTimingInfo.cpbRemovalDelay;
    }

    if (timeInfo->isFirstAu)
    {
        timeInfo->initialArrivalTimeEarliest = timeInfo->nominalRemovalTime -
                                               (pSeiParameters->bufferingPeriodInfo.initialCpbRemovalDelay[0] / 90000.0);
    }
    else
        timeInfo->initialArrivalTimeEarliest = timeInfo->nominalRemovalTime -
                                               ((pSeiParameters->bufferingPeriodInfo.initialCpbRemovalDelay[0] +
                                                       pSeiParameters->bufferingPeriodInfo.initialCpbRemovalDelayOffset[0]) / 90000.0);

    //compute tai
    if (timeInfo->isFirstAu)
        timeInfo->initialArrivalTime = 0;
    else if (cbrFlag == 1)
        timeInfo->initialArrivalTime = timeInfo->finalArrivalTime;
    else
        timeInfo->initialArrivalTime =
            (timeInfo->finalArrivalTime >= timeInfo->initialArrivalTimeEarliest) ?
            timeInfo->finalArrivalTime : timeInfo->initialArrivalTimeEarliest;

    //compute taf
    bitRate = bitRateValue * (2 << (6 + bitRateScale));
    timeInfo->finalArrivalTime =
        timeInfo->initialArrivalTime + ((double)timeInfo->accessUnitSize) / bitRate;

    //compute tr
    if (!pSeqParamSet->vuiParameters->lowDelayHrdFlag || (timeInfo->nominalRemovalTime >= timeInfo->finalArrivalTime))
        timeInfo->cpbRemovalTime = timeInfo->nominalRemovalTime;
    else
        timeInfo->cpbRemovalTime =
            timeInfo->nominalRemovalTime + timeInfo->clockTick *
            Ceil((timeInfo->finalArrivalTime - timeInfo->nominalRemovalTime) /
                 timeInfo->clockTick);

    //compute to_dpb
    timeInfo->dpbOutputTime = timeInfo->cpbRemovalTime +
                              timeInfo->clockTick * pSeiParameters->picTimingInfo.dpbOutputDelay;
    timeInfo->isFirstAu = 0;
    return (HANTRO_OK);
}
