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
--  Abstract : Stream decoding utilities
--
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Table of context

     1. Include headers
     2. External identifiers
     3. Module defines
     4. Module identifiers
     5. Fuctions

------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "bqueue.h"
#include "dwl.h"
#ifndef HANTRO_OK
    #define HANTRO_OK   (0)
#endif /* HANTRO_TRUE */

#ifndef HANTRO_NOK
    #define HANTRO_NOK  (1)
#endif /* HANTRO_FALSE*/

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    BqueueInit
        Initialize buffer queue
------------------------------------------------------------------------------*/
u32 BqueueInit(bufferQueue_t *bq, u32 numBuffers)
{
    u32 i;

    if (DWLmemset(bq, 0, sizeof(*bq)) != bq)
        return HANTRO_NOK;

    if (numBuffers == 0)
        return HANTRO_OK;
#if 0
#ifndef USE_EXTERNAL_BUFFER
    bq->picI = (u32 *)DWLmalloc(sizeof(u32) * numBuffers);
#else
    bq->picI = (u32 *)DWLmalloc(sizeof(u32) * 16);
#endif
    if (bq->picI == NULL)
    {
        return HANTRO_NOK;
    }
#endif
#ifndef USE_EXTERNAL_BUFFER
    for (i = 0 ; i < numBuffers ; ++i)
    {
        bq->picI[i] = 0;
    }
#else
    for (i = 0; i < 16; ++i)
    {
        bq->picI[i] = 0;
    }
#endif
    bq->queueSize = numBuffers;
    bq->ctr = 1;

    return HANTRO_OK;
}


/*------------------------------------------------------------------------------
    BqueueRelease
------------------------------------------------------------------------------*/
void BqueueRelease(bufferQueue_t *bq)
{
#if 0
    if (bq->picI)
    {
        DWLfree(bq->picI);
        bq->picI = NULL;
    }
#endif
    bq->prevAnchorSlot  = 0;
    bq->queueSize       = 0;
}

/*------------------------------------------------------------------------------
    BqueueNext
        Return "oldest" available buffer.
------------------------------------------------------------------------------*/
u32 BqueueNext(bufferQueue_t *bq, u32 ref0, u32 ref1, u32 ref2, u32 bPic)
{
    u32 minPicI = 1 << 30;
    u32 nextOut = (u32)0xFFFFFFFFU;
    u32 i;
    /* Find available buffer with smallest index number  */
    i = 0;

    while (i < bq->queueSize)
    {
        if (i == ref0 || i == ref1 || i == ref2) /* Skip reserved anchor pictures */
        {
            i++;
            continue;
        }

        if (bq->picI[i] < minPicI)
        {
            minPicI = bq->picI[i];
            nextOut = i;
        }
        i++;
    }

    if (nextOut == (u32)0xFFFFFFFFU)
    {
        return 0; /* No buffers available, shouldn't happen */
    }

    /* Update queue state */
    if (bPic)
    {
        bq->picI[nextOut] = bq->ctr - 1;
        bq->picI[bq->prevAnchorSlot]++;
    }
    else
    {
        bq->picI[nextOut] = bq->ctr;
    }
    bq->ctr++;
    if (!bPic)
    {
        bq->prevAnchorSlot = nextOut;
    }

    return nextOut;
}

/*------------------------------------------------------------------------------
    BqueueDiscard
        "Discard" output buffer, e.g. if error concealment used and buffer
        at issue is never going out.
------------------------------------------------------------------------------*/
void BqueueDiscard(bufferQueue_t *bq, u32 buffer)
{
    bq->picI[buffer] = 0;
}

#ifdef USE_OUTPUT_RELEASE

/*------------------------------------------------------------------------------
    BqueueInit2
        Initialize buffer queue
------------------------------------------------------------------------------*/
u32 BqueueInit2(bufferQueue_t *bq, u32 numBuffers)
{
    u32 i;

    if (DWLmemset(bq, 0, sizeof(*bq)) != bq)
        return HANTRO_NOK;

    if (numBuffers == 0)
        return HANTRO_OK;

#if 0
#ifndef USE_EXTERNAL_BUFFER
    bq->picI = (u32 *)DWLmalloc(sizeof(u32) * numBuffers);
#else
    bq->picI = (u32 *)DWLmalloc(sizeof(u32) * 16);
#endif
    if (bq->picI == NULL)
    {
        return HANTRO_NOK;
    }
#endif

#ifndef USE_EXTERNAL_BUFFER
    for (i = 0 ; i < numBuffers ; ++i)
    {
        bq->picI[i] = 0;
    }
#else
    for (i = 0; i < 16; ++i)
    {
        bq->picI[i] = 0;
    }
#endif
    bq->queueSize = numBuffers;
    bq->ctr = 1;
    bq->abort = 0;
    pthread_mutex_init(&bq->buf_release_mutex, NULL);
    pthread_cond_init(&bq->buf_release_cv, NULL);

#if 0
#ifndef USE_EXTERNAL_BUFFER
    bq->bufUsed = (u32 *)DWLmalloc(sizeof(u32) * numBuffers);
#else
    bq->bufUsed = (u32 *)DWLmalloc(sizeof(u32) * 16);
#endif
    if (bq->bufUsed == NULL)
    {
        return HANTRO_NOK;
    }
#endif

#ifndef USE_EXTERNAL_BUFFER
    for (i = 0; i < numBuffers; ++i)
    {
        bq->bufUsed[i] = 0;
    }
#else
    for (i = 0; i < 16; ++i)
    {
        bq->bufUsed[i] = 0;
    }
#endif

    return HANTRO_OK;
}



/*------------------------------------------------------------------------------
    BqueueRelease2
------------------------------------------------------------------------------*/
void BqueueRelease2(bufferQueue_t *bq)
{
#if 0
    if (bq->picI)
    {
        DWLfree(bq->picI);
        bq->picI = NULL;
    }
#endif
    bq->prevAnchorSlot  = 0;
    bq->queueSize       = 0;
#if 0
    if (bq->bufUsed)
    {
        DWLfree(bq->bufUsed);
        bq->bufUsed = NULL;
        pthread_mutex_destroy(&bq->buf_release_mutex);
        pthread_cond_destroy(&bq->buf_release_cv);
    }
#endif
    pthread_mutex_destroy(&bq->buf_release_mutex);
    pthread_cond_destroy(&bq->buf_release_cv);
}



/*------------------------------------------------------------------------------
    BqueuePictureRelease
        "Release" output buffer.
------------------------------------------------------------------------------*/
void BqueuePictureRelease(bufferQueue_t *bq, u32 buffer)
{
    pthread_mutex_lock(&bq->buf_release_mutex);
    bq->bufUsed[buffer] = 0;
    pthread_cond_signal(&bq->buf_release_cv);
    pthread_mutex_unlock(&bq->buf_release_mutex);

}

/*------------------------------------------------------------------------------
    BqueueNext2
        Return "oldest" available buffer.
------------------------------------------------------------------------------*/
u32 BqueueNext2(bufferQueue_t *bq, u32 ref0, u32 ref1, u32 ref2, u32 bPic)
{
    u32 minPicI = 1 << 30;
    u32 nextOut = (u32)0xFFFFFFFFU;
    u32 i;
    /* Find available buffer with smallest index number  */
    i = 0;

    while (i < bq->queueSize)
    {
        if (i == ref0 || i == ref1 || i == ref2) /* Skip reserved anchor pictures */
        {
            i++;
            continue;
        }

        pthread_mutex_lock(&bq->buf_release_mutex);
        if (!bq->bufUsed[i] && !bq->abort)
        {
            nextOut = i;
            pthread_mutex_unlock(&bq->buf_release_mutex);
            break;
        }
        pthread_mutex_unlock(&bq->buf_release_mutex);

        if (bq->picI[i] < minPicI)
        {
            minPicI = bq->picI[i];
            nextOut = i;
        }
        i++;
    }

    if (nextOut == (u32)0xFFFFFFFFU)
    {
        return 0; /* No buffers available, shouldn't happen */
    }

    pthread_mutex_lock(&bq->buf_release_mutex);
    while (bq->bufUsed[nextOut] && !bq->abort)
    {
        for (i = 0; i < bq->queueSize; i++)
        {
            if (!bq->bufUsed[i] &&
                    i != ref0 && i != ref1 && i != ref2)
            {
                nextOut = i;
                break;
            }
        }

        if (!bq->bufUsed[nextOut])
            break;

        pthread_cond_wait(&bq->buf_release_cv, &bq->buf_release_mutex);
    }

    if (bq->abort)
    {
        nextOut = (u32)0xFFFFFFFFU;
    }
    pthread_mutex_unlock(&bq->buf_release_mutex);

    if (nextOut == (u32)0xFFFFFFFFU)
    {
        return nextOut;
    }

    /* Update queue state */
    if (bPic)
    {
        bq->picI[nextOut] = bq->ctr - 1;
        bq->picI[bq->prevAnchorSlot]++;
    }
    else
    {
        bq->picI[nextOut] = bq->ctr;
    }
    bq->ctr++;
    if (!bPic)
    {
        bq->prevAnchorSlot = nextOut;
    }

    return nextOut;
}

u32 BqueueWaitNotInUse(bufferQueue_t *bq)
{
    u32 i;
    for (i = 0; i < bq->queueSize; i++)
    {
        pthread_mutex_lock(&bq->buf_release_mutex);
        while (bq->bufUsed[i] && !bq->abort)
            pthread_cond_wait(&bq->buf_release_cv, &bq->buf_release_mutex);
        pthread_mutex_unlock(&bq->buf_release_mutex);
    }

    if (bq->abort)
        return HANTRO_NOK;
    else
        return HANTRO_OK;
}

void BqueueSetBufferAsUsed(bufferQueue_t *bq, u32 buffer)
{
    pthread_mutex_lock(&bq->buf_release_mutex);
    bq->bufUsed[buffer] = 1;
    pthread_mutex_unlock(&bq->buf_release_mutex);
}

u32 BqueueWaitBufNotInUse(bufferQueue_t *bq, u32 buffer)
{
    pthread_mutex_lock(&bq->buf_release_mutex);
    while (bq->bufUsed[buffer] && !bq->abort)
        pthread_cond_wait(&bq->buf_release_cv, &bq->buf_release_mutex);
    pthread_mutex_unlock(&bq->buf_release_mutex);

    if (bq->abort)
        return HANTRO_NOK;
    else
        return HANTRO_OK;
}

void BqueueSetAbort(bufferQueue_t *bq)
{
    pthread_mutex_lock(&bq->buf_release_mutex);
    bq->abort = 1;
    pthread_cond_signal(&bq->buf_release_cv);
    pthread_mutex_unlock(&bq->buf_release_mutex);
}

void BqueueClearAbort(bufferQueue_t *bq)
{
    pthread_mutex_lock(&bq->buf_release_mutex);
    bq->abort = 0;
    pthread_mutex_unlock(&bq->buf_release_mutex);
}

void BqueueEmpty(bufferQueue_t *bq)
{
    u32 i;

    pthread_mutex_lock(&bq->buf_release_mutex);
#ifndef USE_EXTERNAL_BUFFER
    for (i = 0 ; i < bq->queueSize ; ++i)
    {
        bq->picI[i] = 0;
        bq->bufUsed[i] = 0;
    }
#else
    for (i = 0; i < 16; ++i)
    {
        bq->picI[i] = 0;
        bq->bufUsed[i] = 0;
    }
#endif

    bq->ctr = 1;
    bq->abort = 0;
    bq->prevAnchorSlot  = 0;
    pthread_mutex_unlock(&bq->buf_release_mutex);
}
#endif
