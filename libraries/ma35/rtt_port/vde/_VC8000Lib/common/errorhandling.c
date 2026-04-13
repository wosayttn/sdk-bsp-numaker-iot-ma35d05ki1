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
--  Abstract :
--
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "errorhandling.h"
#include "dwl.h"
#include "deccfg.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

#define MAGIC_WORD_LENGTH   (8)

static const u8 magicWord[MAGIC_WORD_LENGTH] = "Rosebud\0";

#define NUM_OFFSETS 6

static const u32 rowOffsets[] = {1, 2, 4, 8, 12, 16};

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

static u32 GetMbOffset(u32 mbNum, u32 vopWidth, u32 vopHeight);

/*------------------------------------------------------------------------------

   5.1  Function name: GetMbOffset

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 GetMbOffset(u32 mbNum, u32 vopWidth, u32 vopHeight)
{
    u32 mbRow, mbCol;
    u32 offset;
    UNUSED(vopHeight);

    mbRow = mbNum / vopWidth;
    mbCol = mbNum % vopWidth;
    offset = mbRow * 16 * 16 * vopWidth + mbCol * 16;

    return offset;
}

/*------------------------------------------------------------------------------

   5.1  Function name: CopyRows

        Purpose: Copy numRows bottom mb rows from pRefPic to pDecOut

        Input:

        Output:

------------------------------------------------------------------------------*/
void CopyRows(u32 numRows, u8 *pDecOut, const u8 *pRefPic, u32 vopWidth,
              u32 vopHeight)
{

    u32 pixWidth;
    u32 offset;
    u32 lumaSize;
    u8 const *pSrc;
    u8 *pDst;

    pixWidth = 16 * vopWidth;

    offset = (vopHeight - numRows) * 16 * pixWidth;
    lumaSize = 256 * vopWidth * vopHeight;

    pDst = pDecOut;
    pSrc = pRefPic;

    pDst += offset;
    pSrc += offset;

    if (pRefPic)
        DWLmemcpy(pDst, pSrc, numRows * 16 * pixWidth);
    else
        DWLmemset(pDst, 0, numRows * 16 * pixWidth);

    /* Chroma data */
    offset = (vopHeight - numRows) * 8 * pixWidth;

    pDst = pDecOut;
    pSrc = pRefPic;

    pDst += lumaSize;
    pSrc += lumaSize;
    pDst += offset;
    pSrc += offset;

    if (pRefPic)
        DWLmemcpy(pDst, pSrc, numRows * 8 * pixWidth);
    else
        DWLmemset(pDst, 128, numRows * 8 * pixWidth);

}

/*------------------------------------------------------------------------------

   5.1  Function name: PreparePartialFreeze

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
void PreparePartialFreeze(u8 *pDecOut, u32 vopWidth, u32 vopHeight)
{

    u32 i, j;
    u8 *pBase;

    for (i = 0; i < NUM_OFFSETS && rowOffsets[i] < vopHeight / 4 &&
            rowOffsets[i] <= DEC_X170_MAX_EC_COPY_ROWS; i++)
    {
        pBase = pDecOut + GetMbOffset(vopWidth * (vopHeight - rowOffsets[i]),
                                      vopWidth, vopHeight);

        for (j = 0 ; j < MAGIC_WORD_LENGTH ; ++j)
            pBase[j] = magicWord[j];
    }

}

/*------------------------------------------------------------------------------

   5.1  Function name: ProcessPartialFreeze

        Purpose:

        Input:

        Output:
            HANTRO_TRUE
            HANTRO_FALSE

------------------------------------------------------------------------------*/
u32  ProcessPartialFreeze(u8 *pDecOut, const u8 *pRefPic, u32 vopWidth,
                          u32 vopHeight, u32 copy)
{

    u32 i, j;
    u8 *pBase;
    u32 match = HANTRO_TRUE;

    for (i = 0; i < NUM_OFFSETS && rowOffsets[i] < vopHeight / 4 &&
            rowOffsets[i] <= DEC_X170_MAX_EC_COPY_ROWS; i++)
    {
        pBase = pDecOut + GetMbOffset(vopWidth * (vopHeight - rowOffsets[i]),
                                      vopWidth, vopHeight);

        for (j = 0 ; j < MAGIC_WORD_LENGTH && match ; ++j)
            if (pBase[j] != magicWord[j])
                match = HANTRO_FALSE;

        if (!match)
        {
            if (copy)
                CopyRows(rowOffsets[i], pDecOut, pRefPic, vopWidth, vopHeight);
            return HANTRO_TRUE;
        }
    }

    return HANTRO_FALSE;

}
