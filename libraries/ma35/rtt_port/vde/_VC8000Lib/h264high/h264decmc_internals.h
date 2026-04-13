/* Copyright 2012 Google Inc. All Rights Reserved. */


#ifndef SOFTWARE_SOURCE_H264HIGH_H264DECMC_INTERNALS_H_
#define SOFTWARE_SOURCE_H264HIGH_H264DECMC_INTERNALS_H_

#include "basetype.h"
#include "h264hwd_container.h"

void h264MCPushOutputAll(decContainer_t *pDecCont);
void h264MCWaitPicReadyAll(decContainer_t *pDecCont);

void h264MCSetHwRdyCallback(decContainer_t *pDecCont);

void h264MCSetRefPicStatus(volatile u8 *pSyncMem, u32 isFieldPic,
                           u32 isBottomField);

void h264MCHwRdyCallback(void *args, i32 core_id);

#endif /* SOFTWARE_SOURCE_H264HIGH_H264DECMC_INTERNALS_H_ */
