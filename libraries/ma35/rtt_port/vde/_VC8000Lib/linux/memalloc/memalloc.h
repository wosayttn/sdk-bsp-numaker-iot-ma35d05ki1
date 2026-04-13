/* Copyright 2012 Google Inc. All Rights Reserved. */

#ifndef MEMALLOC_H
#define MEMALLOC_H

//#include <linux/ioctl.h>

#undef PDEBUG
#ifdef MEMALLOC_DEBUG
    #ifdef __KERNEL__
        #define PDEBUG(fmt, args...) sysprintf("memalloc: " fmt, ## args)
    #else
        #define PDEBUG(fmt, args...) sysprintf(fmt, ## args)
    #endif
#else
    #define PDEBUG(fmt, args...)
#endif

typedef struct
{
    unsigned busAddress;
    unsigned size;
    unsigned translationOffset;
} MemallocParams;

#define MEMALLOC_IOC_MAGIC  'k'

#define MEMALLOC_IOCXGETBUFFER    1     // _IOWR(MEMALLOC_IOC_MAGIC, 1, MemallocParams*)
#define MEMALLOC_IOCSFREEBUFFER   2     // _IOW(MEMALLOC_IOC_MAGIC, 2, unsigned long*)

#define MEMALLOC_IOCHARDRESET     10    // _IO(MEMALLOC_IOC_MAGIC, 15) /* debugging tool */
#define MEMALLOC_IOC_MAXNR 15

#endif /* MEMALLOC_H */



