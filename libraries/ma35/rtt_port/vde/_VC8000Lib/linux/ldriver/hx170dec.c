/* Copyright 2012 Google Inc. All Rights Reserved. */

#include <rtthread.h>
#include <rthw.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NuMicro.h"
#include "hx170dec.h"
#include "../dwl/dwl_defs.h"

#include "basetype.h"

#define DEF_MAX_WAIT_MS               50
#define HXDEC_MAX_CORES                 4

#define HANTRO_DEC_ORG_REGS             60
#define HANTRO_PP_ORG_REGS              41

#define HANTRO_DEC_EXT_REGS             27
#define HANTRO_PP_EXT_REGS              9

#define HANTRO_DEC_TOTAL_REGS           (HANTRO_DEC_ORG_REGS + HANTRO_DEC_EXT_REGS)
#define HANTRO_PP_TOTAL_REGS            (HANTRO_PP_ORG_REGS + HANTRO_PP_EXT_REGS)
#define HANTRO_TOTAL_REGS               155

#define HANTRO_DEC_ORG_FIRST_REG        0
#define HANTRO_DEC_ORG_LAST_REG         59
#define HANTRO_DEC_EXT_FIRST_REG        119
#define HANTRO_DEC_EXT_LAST_REG         145

#define HANTRO_PP_ORG_FIRST_REG         60
#define HANTRO_PP_ORG_LAST_REG          100
#define HANTRO_PP_EXT_FIRST_REG         146
#define HANTRO_PP_EXT_LAST_REG          154

/* Logic module base address */
#define SOCLE_LOGIC_0_BASE              0x20000000
#define SOCLE_LOGIC_1_BASE              0x21000000

#define VEXPRESS_LOGIC_0_BASE           0xFC010000
#define VEXPRESS_LOGIC_1_BASE           0xFC020000

/* Logic module IRQs */
#define HXDEC_NO_IRQ                    VDEC_IRQ_NUM
#define VP_PB_INT_LT                    30
#define SOCLE_INT                       36

/* module defaults */
#define DEC_IO_BASE             VDEC_BASE    // SOCLE_LOGIC0_BASE

#ifdef USE_64BIT_ENV
    #define DEC_IO_SIZE             ((HANTRO_TOTAL_REGS) * 4) /* bytes */
#else
    #define DEC_IO_SIZE             ((HANTRO_DEC_ORG_REGS + HANTRO_PP_ORG_REGS) * 4) /* bytes */
#endif

#define DEC_IRQ                 HXDEC_NO_IRQ

static struct rt_completion sComplPP;
static struct rt_completion sComplDEC;

void vde_os_wait_pp_event(uint32_t wait_ms)
{
    rt_completion_init(&sComplPP);
    rt_completion_wait(&sComplPP, wait_ms);
}

void vde_os_signal_pp_event(void)
{
    rt_completion_done(&sComplPP);
}

void vde_os_wait_dec_event(uint32_t wait_ms)
{
    rt_completion_init(&sComplDEC);
    rt_completion_wait(&sComplDEC, wait_ms);
}

void vde_os_signal_dec_event(void)
{
    rt_completion_done(&sComplDEC);
}


static const int DecHwId[] =
{
    0x8190,
    0x8170,
    0x9170,
    0x9190,
    0x6731,
    0x6e64
};

unsigned long base_port = VDEC_BASE;  // ychuang - defined in nua3500.h

unsigned long multicorebase[HXDEC_MAX_CORES] =
{
    SOCLE_LOGIC_0_BASE,
    SOCLE_LOGIC_1_BASE,
    -1,
    -1
};

int irq = DEC_IRQ;
int elements = 0;

/* module_param(name, type, perm) */
//module_param(base_port, ulong, 0);
//module_param(irq, int, 0);
//module_param_array(multicorebase, ulong, &elements, 0);

static void hx170dec_isr(int vector, void *param);

static int hx170dec_major = 0; /* dynamic allocation */

/* here's all the must remember stuff */
typedef struct
{
    char *buffer;
    unsigned int iosize;
    volatile u8 *hwregs[HXDEC_MAX_CORES];
    int irq;
    int cores;
//    struct fasync_struct *async_queue_dec;
//    struct fasync_struct *async_queue_pp;
} hx170dec_t;

static hx170dec_t hx170dec_data; /* dynamic allocation? */

static int ReserveIO(void);
//static void ReleaseIO(void);

static void ResetAsic(hx170dec_t *dev);

#ifdef HX170DEC_DEBUG
    static void dump_regs(hx170dec_t *dev);
#endif


static u32 dec_regs[HXDEC_MAX_CORES][DEC_IO_SIZE / 4];
//struct semaphore dec_core_sem;
//struct semaphore pp_core_sem;

static volatile int dec_irq = 0;
static volatile int pp_irq = 0;

static struct file *dec_owner[HXDEC_MAX_CORES];
static struct file *pp_owner[HXDEC_MAX_CORES];

#define DWL_CLIENT_TYPE_H264_DEC         1U
#define DWL_CLIENT_TYPE_MPEG4_DEC        2U
#define DWL_CLIENT_TYPE_JPEG_DEC         3U
#define DWL_CLIENT_TYPE_PP               4U
#define DWL_CLIENT_TYPE_VC1_DEC          5U
#define DWL_CLIENT_TYPE_MPEG2_DEC        6U
#define DWL_CLIENT_TYPE_VP6_DEC          7U
#define DWL_CLIENT_TYPE_AVS_DEC          8U
#define DWL_CLIENT_TYPE_RV_DEC           9U
#define DWL_CLIENT_TYPE_VP8_DEC          10U

static u32 cfg[HXDEC_MAX_CORES];

static void ReadCoreConfig(hx170dec_t *dev)
{
    int c;
    u32 reg, mask, tmp;

    memset(cfg, 0, sizeof(cfg));

    for (c = 0; c < dev->cores; c++)
    {
        /* Decoder configuration */
        //reg = ioread32(dev->hwregs[c] + HX170DEC_SYNTH_CFG * 4);
        reg = inpw((void *)(dev->hwregs[c] + HX170DEC_SYNTH_CFG * 4));

        tmp = (reg >> DWL_H264_E) & 0x3U;
        if (tmp) sysprintf("hx170dec: core[%d] has H264\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

        tmp = (reg >> DWL_JPEG_E) & 0x01U;
        if (tmp) sysprintf("hx170dec: core[%d] has JPEG\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

        tmp = (reg >> DWL_MPEG4_E) & 0x3U;
        if (tmp) sysprintf("hx170dec: core[%d] has MPEG4\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG4_DEC : 0;

        tmp = (reg >> DWL_VC1_E) & 0x3U;
        if (tmp) sysprintf("hx170dec: core[%d] has VC1\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VC1_DEC : 0;

        tmp = (reg >> DWL_MPEG2_E) & 0x01U;
        if (tmp) sysprintf("hx170dec: core[%d] has MPEG2\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG2_DEC : 0;

        tmp = (reg >> DWL_VP6_E) & 0x01U;
        if (tmp) sysprintf("hx170dec: core[%d] has VP6\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP6_DEC : 0;

        //reg = ioread32(dev->hwregs[c] + HX170DEC_SYNTH_CFG_2 * 4);
        reg = inpw((void *)dev->hwregs[c] + HX170DEC_SYNTH_CFG_2 * 4);

        /* VP7 and WEBP is part of VP8 */
        mask = (1 << DWL_VP8_E) | (1 << DWL_VP7_E) | (1 << DWL_WEBP_E);
        tmp = (reg & mask);
        if (tmp & (1 << DWL_VP8_E))
            sysprintf("hx170dec: core[%d] has VP8\n", c);
        if (tmp & (1 << DWL_VP7_E))
            sysprintf("hx170dec: core[%d] has VP7\n", c);
        if (tmp & (1 << DWL_WEBP_E))
            sysprintf("hx170dec: core[%d] has WebP\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP8_DEC : 0;

        tmp = (reg >> DWL_AVS_E) & 0x01U;
        if (tmp) sysprintf("hx170dec: core[%d] has AVS\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_AVS_DEC : 0;

        tmp = (reg >> DWL_RV_E) & 0x03U;
        if (tmp) sysprintf("hx170dec: core[%d] has RV\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_RV_DEC : 0;

        /* Post-processor configuration */
        //reg = ioread32(dev->hwregs[c] + HX170PP_SYNTH_CFG * 4);
        reg = inpw((void *)dev->hwregs[c] + HX170PP_SYNTH_CFG * 4);

        tmp = (reg >> DWL_PP_E) & 0x01U;
        if (tmp) sysprintf("hx170dec: core[%d] has PP\n", c);
        cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;
    }
}

static int CoreHasFormat(const u32 *cfg, int core, u32 format)
{
    return (cfg[core] & (1 << format)) ? 1 : 0;
}

#if 0
int GetDecCore(long core, hx170dec_t *dev, struct file *filp)
{
    int success = 0;
    unsigned long flags;

    //spin_lock_irqsave(&owner_lock, flags);
    if (dec_owner[core] == NULL)
    {
        dec_owner[core] = filp;
        success = 1;
    }

    //spin_unlock_irqrestore(&owner_lock, flags);

    return success;
}
#endif

#if 0
int GetDecCoreAny(long *core, hx170dec_t *dev, struct file *filp,
                  unsigned long format)
{
    int success = 0;
    long c;

    *core = -1;

    for (c = 0; c < dev->cores; c++)
    {
        /* a free core that has format */
        if (CoreHasFormat(cfg, c, format) && GetDecCore(c, dev, filp))
        {
            success = 1;
            *core = c;
            break;
        }
    }

    return success;
}
#endif

#if 0
long ReserveDecoder(hx170dec_t *dev, struct file *filp, unsigned long format)
{
    long core = -1;

    /* reserve a core */
    //if (down_interruptible(&dec_core_sem))
    //    return -ERESTARTSYS;

    /* lock a core that has specific format*/
    //if(wait_event_interruptible(hw_queue,
    //        GetDecCoreAny(&core, dev, filp, format) != 0 ))
    //    return -ERESTARTSYS;

    while (GetDecCoreAny(&core, dev, filp, format) == 0);

    return core;
}
#endif
void ReleaseDecoder(hx170dec_t *dev, long core)
{
    u32 status;
    unsigned long flags;

    // status = ioread32(dev->hwregs[core] + HX170_IRQ_STAT_DEC_OFF);
    status = inpw(dev->hwregs[core] + HX170_IRQ_STAT_DEC_OFF);

    /* make sure HW is disabled */
    if (status & HX170_DEC_E)
    {
        sysprintf("hx170dec: DEC[%li] still enabled -> reset\n", core);

        /* abort decoder */
        status |= HX170_DEC_ABORT | HX170_DEC_IRQ_DISABLE;
        // iowrite32(status, dev->hwregs[core] + HX170_IRQ_STAT_DEC_OFF);
        outpw((void *)dev->hwregs[core] + HX170_IRQ_STAT_DEC_OFF, status);
    }

    //spin_lock_irqsave(&owner_lock, flags);

    dec_owner[core] = NULL;

    //spin_unlock_irqrestore(&owner_lock, flags);

    //up(&dec_core_sem);

    // wake_up_interruptible_all(&hw_queue);
    //  while (hw_queue);
}

#if 0
long ReservePostProcessor(hx170dec_t *dev, struct file *filp)
{
    unsigned long flags;

    long core = 0;

    /* single core PP only */
    //if (down_interruptible(&pp_core_sem))
    //    return -ERESTARTSYS;

    //spin_lock_irqsave(&owner_lock, flags);

    pp_owner[core] = filp;

    //spin_unlock_irqrestore(&owner_lock, flags);

    return core;
}

void ReleasePostProcessor(hx170dec_t *dev, long core)
{
    unsigned long flags;

    // u32 status = ioread32(dev->hwregs[core] + HX170_IRQ_STAT_PP_OFF);
    u32 status = inpw(dev->hwregs[core] + HX170_IRQ_STAT_PP_OFF);

    /* make sure HW is disabled */
    if (status & HX170_PP_E)
    {
        sysprintf("hx170dec: PP[%li] still enabled -> reset\n", core);

        /* disable IRQ */
        status |= HX170_PP_IRQ_DISABLE;

        /* disable postprocessor */
        status &= (~HX170_PP_E);
        // iowrite32(0x10, dev->hwregs[core] + HX170_IRQ_STAT_PP_OFF);
        outpw((void *)dev->hwregs[core] + HX170_IRQ_STAT_PP_OFF, 0x10);
    }

    //spin_lock_irqsave(&owner_lock, flags);

    pp_owner[core] = NULL;

    //spin_unlock_irqrestore(&owner_lock, flags);

    //up(&pp_core_sem);
}
#endif

#if 0
long ReserveDecPp(hx170dec_t *dev, struct file *filp, unsigned long format)
{
    /* reserve core 0, DEC+PP for pipeline */
    unsigned long flags;

    long core = 0;

    /* check that core has the requested dec format */
    if (!CoreHasFormat(cfg, core, format))
        return -EFAULT;

    /* check that core has PP */
    if (!CoreHasFormat(cfg, core, DWL_CLIENT_TYPE_PP))
        return -EFAULT;

    /* reserve a core */
    //if (down_interruptible(&dec_core_sem))
    //    return -ERESTARTSYS;

    /* wait until the core is available */
    //if(wait_event_interruptible(hw_queue,
    //        GetDecCore(core, dev, filp) != 0))
    //{
    //    up(&dec_core_sem);
    //    return -ERESTARTSYS;
    //}

    while (hw_queue);

    //if (down_interruptible(&pp_core_sem))
    //{
    //    ReleaseDecoder(dev, core);
    //    return -ERESTARTSYS;
    //}

    //spin_lock_irqsave(&owner_lock, flags);
    pp_owner[core] = filp;
    //spin_unlock_irqrestore(&owner_lock, flags);

    return core;
}
#endif

long DecFlushRegs(hx170dec_t *dev, struct core_desc *core)
{
    long ret = 0, i;

    u32 id = core->id;

    /* copy original dec regs to kernal space*/
    //ret = copy_from_user(dec_regs[id], core->regs, HANTRO_DEC_ORG_REGS*4);
    memcpy(dec_regs[id], core->regs, HANTRO_DEC_ORG_REGS * 4);

#ifdef USE_64BIT_ENV
    /* copy extended dec regs to kernal space*/
    ret = copy_from_user(dec_regs[id] + HANTRO_DEC_EXT_FIRST_REG,
                         core->regs + HANTRO_DEC_EXT_FIRST_REG,
                         HANTRO_DEC_EXT_REGS * 4);
#endif
    //if (ret)
    //{
    //    PDEBUG("copy_from_user failed, returned %li\n", ret);
    //    return -EFAULT;
    //}

    /* write dec regs but the status reg[1] to hardware */
    /* both original and extended regs need to be written */
    for (i = 2; i <= HANTRO_DEC_ORG_LAST_REG; i++)
    {
        // iowrite32(dec_regs[id][i], dev->hwregs[id] + i*4);
        outpw((void *)(dev->hwregs[id] + i * 4), dec_regs[id][i]);
    }

#ifdef USE_64BIT_ENV
    for (i = HANTRO_DEC_EXT_FIRST_REG; i <= HANTRO_DEC_EXT_LAST_REG; i++)
        iowrite32(dec_regs[id][i], dev->hwregs[id] + i * 4);
#endif

    /* write the status register, which may start the decoder */
    //iowrite32(dec_regs[id][1], dev->hwregs[id] + 4);
    outpw((void *)dev->hwregs[id] + 4, dec_regs[id][1]);
    // PDEBUG("flushed registers on core %d\n", id);

    return 0;
}

long DecRefreshRegs(hx170dec_t *dev, struct core_desc *core)
{
    long ret, i;
    u32 id = core->id;
#ifdef USE_64BIT_ENV
    /* user has to know exactly what they are asking for */
    if (core->size != (HANTRO_DEC_TOTAL_REGS * 4))
        return -EFAULT;
#else
    /* user has to know exactly what they are asking for */
    if (core->size != (HANTRO_DEC_ORG_REGS * 4))
        return -1; // -EFAULT;
#endif
    /* read all registers from hardware */
    /* both original and extended regs need to be read */
    for (i = 0; i <= HANTRO_DEC_ORG_LAST_REG; i++)
        // dec_regs[id][i] = ioread32(dev->hwregs[id] + i*4);
        dec_regs[id][i] = inpw((void *)dev->hwregs[id] + i * 4);

#ifdef USE_64BIT_ENV
    for (i = HANTRO_DEC_EXT_FIRST_REG; i <= HANTRO_DEC_EXT_LAST_REG; i++)
        dec_regs[id][i] = ioread32(dev->hwregs[id] + i * 4);
#endif
    /* put registers to user space*/
    /* put original registers to user space*/
    //ret = copy_to_user(core->regs, dec_regs[id], HANTRO_DEC_ORG_REGS*4);
    memcpy(core->regs, dec_regs[id], HANTRO_DEC_ORG_REGS * 4);

#ifdef USE_64BIT_ENV
    /*put extended registers to user space*/
    ret = copy_to_user(core->regs + HANTRO_DEC_EXT_FIRST_REG,
                       dec_regs[id] + HANTRO_DEC_EXT_FIRST_REG,
                       HANTRO_DEC_EXT_REGS * 4);
#endif
    //if (ret)
    //{
    //    PDEBUG("copy_to_user failed, returned %li\n", ret);
    //    return -EFAULT;
    //}

    return 0;
}

static int CheckDecIrq(hx170dec_t *dev, int id)
{
    unsigned long flags;
    int rdy = 0;

    const u32 irq_mask = (1 << id);

    if (dec_irq & irq_mask)
    {
        /* reset the wait condition(s) */
        dec_irq &= ~irq_mask;
        rdy = 1;
    }

    return rdy;
}

long WaitDecReadyAndRefreshRegs(hx170dec_t *dev, struct core_desc *core)
{
    /* wait for dec interrupt event */
    vde_os_wait_dec_event(DEF_MAX_WAIT_MS);

    /* refresh registers */
    return DecRefreshRegs(dev, core);
}

static long PPFlushRegs(hx170dec_t *dev, struct core_desc *core)
{
    long ret = 0;
    u32 id = core->id;
    u32 i;

    /* copy original dec regs to kernal space*/
    //ret = copy_from_user(dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
    //        core->regs + HANTRO_PP_ORG_FIRST_REG,
    //        HANTRO_PP_ORG_REGS*4);
    memcpy(dec_regs[id] + HANTRO_PP_ORG_FIRST_REG, core->regs + HANTRO_PP_ORG_FIRST_REG, HANTRO_PP_ORG_REGS * 4);

#ifdef USE_64BIT_ENV
    /* copy extended dec regs to kernal space*/
    ret = copy_from_user(dec_regs[id] + HANTRO_PP_EXT_FIRST_REG,
                         core->regs + HANTRO_PP_EXT_FIRST_REG,
                         HANTRO_PP_EXT_REGS * 4);
#endif
    //if (ret)
    //{
    //    PDEBUG("copy_from_user failed, returned %li\n", ret);
    //    return -EFAULT;
    //}

    /* write all regs but the status reg[1] to hardware */
    /* both original and extended regs need to be written */
    for (i = HANTRO_PP_ORG_FIRST_REG + 1; i <= HANTRO_PP_ORG_LAST_REG; i++)
        // iowrite32(dec_regs[id][i], dev->hwregs[id] + i*4);
        outpw((void *)dev->hwregs[id] + i * 4, dec_regs[id][i]);

#ifdef USE_64BIT_ENV
    for (i = HANTRO_PP_EXT_FIRST_REG; i <= HANTRO_PP_EXT_LAST_REG; i++)
        iowrite32(dec_regs[id][i], dev->hwregs[id] + i * 4);
#endif

    /* write the stat reg, which may start the PP */
    //iowrite32(dec_regs[id][HANTRO_PP_ORG_FIRST_REG],
    //      dev->hwregs[id] + HANTRO_PP_ORG_FIRST_REG * 4);
    outpw((void *)dev->hwregs[id] + HANTRO_PP_ORG_FIRST_REG * 4, dec_regs[id][HANTRO_PP_ORG_FIRST_REG]);

    return 0;
}

static long _PPRefreshRegs(hx170dec_t *dev, struct core_desc *core)
{
    long i, ret;
    u32 id = core->id;
#ifdef USE_64BIT_ENV
    /* user has to know exactly what they are asking for */
    if (core->size != (HANTRO_PP_TOTAL_REGS * 4))
        return -EFAULT;
#else
    /* user has to know exactly what they are asking for */
    if (core->size != (HANTRO_PP_ORG_REGS * 4))
        return -1; //-EFAULT;
#endif

    /* read all registers from hardware */
    /* both original and extended regs need to be read */
    for (i = HANTRO_PP_ORG_FIRST_REG; i <= HANTRO_PP_ORG_LAST_REG; i++)
        // dec_regs[id][i] = ioread32(dev->hwregs[id] + i*4);
        dec_regs[id][i] = inpw((void *)dev->hwregs[id] + i * 4);
#ifdef USE_64BIT_ENV
    for (i = HANTRO_PP_EXT_FIRST_REG; i <= HANTRO_PP_EXT_LAST_REG; i++)
        dec_regs[id][i] = ioread32(dev->hwregs[id] + i * 4);
#endif
    /* put registers to user space*/
    /* put original registers to user space*/
    //ret = copy_to_user(core->regs + HANTRO_PP_ORG_FIRST_REG,
    //        dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
    //        HANTRO_PP_ORG_REGS*4);
    memcpy(core->regs + HANTRO_PP_ORG_FIRST_REG, dec_regs[id] + HANTRO_PP_ORG_FIRST_REG, HANTRO_PP_ORG_REGS * 4);
#ifdef USE_64BIT_ENV
    /* put extended registers to user space*/
    ret = copy_to_user(core->regs + HANTRO_PP_EXT_FIRST_REG,
                       dec_regs[id] + HANTRO_PP_EXT_FIRST_REG,
                       HANTRO_PP_EXT_REGS * 4);
#endif
    //if (ret)
    //{
    //    PDEBUG("copy_to_user failed, returned %li\n", ret);
    //    return -EFAULT;
    //}

    return 0;
}

static int CheckPPIrq(hx170dec_t *dev, int id)
{
    unsigned long flags;
    int rdy = 0;

    const u32 irq_mask = (1 << id);

    //spin_lock_irqsave(&owner_lock, flags);

    if (pp_irq & irq_mask)
    {
        /* reset the wait condition(s) */
        pp_irq &= ~irq_mask;
        rdy = 1;
    }

    //spin_unlock_irqrestore(&owner_lock, flags);

    return rdy;
}

long WaitPPReadyAndRefreshRegs(hx170dec_t *dev, struct core_desc *core)
{
    /* wait for pp interrupt event */
    vde_os_wait_pp_event(DEF_MAX_WAIT_MS);

    /* refresh registers */
    return _PPRefreshRegs(dev, core);
}

//static int CheckCoreIrq(hx170dec_t *dev, const struct file *filp, int *id)
static int CheckCoreIrq(hx170dec_t *dev, int *id)
{
    unsigned long flags;
    int rdy = 0, n = 0;

    do
    {
        u32 irq_mask = (1 << n);

        //spin_lock_irqsave(&owner_lock, flags);

        if (dec_irq & irq_mask)
        {
            //if (dec_owner[n] == filp)
            {
                /* we have an IRQ for our client */

                /* reset the wait condition(s) */
                dec_irq &= ~irq_mask;

                /* signal ready core no. for our client */
                *id = n;

                rdy = 1;

                break;
            }
            //else if(dec_owner[n] == NULL)
            //{
            //    /* zombie IRQ */
            //    sysprintf("IRQ on core[%d], but no owner!!!\n", n);

            //    /* reset the wait condition(s) */
            //    dec_irq &= ~irq_mask;
            //}
        }

        //spin_unlock_irqrestore(&owner_lock, flags);

        n++; /* next core */
    }
    while (n < dev->cores);

    return rdy;
}

//long WaitCoreReady(hx170dec_t *dev, const struct file *filp, int *id)
long WaitCoreReady(hx170dec_t *dev, int *id)
{
    while (!CheckCoreIrq(dev, 0));

    return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hx170dec_ioctl
 Description     : communication method to/from the user space

 Return type     : long
------------------------------------------------------------------------------*/

//long _ioctl(unsigned int cmd,  unsigned long arg)
long hx170dec_ioctl(unsigned int cmd, void *arg)
{
    int err = 0;
    long tmp;

#ifdef HW_PERFORMANCE
    struct timeval *end_time_arg;
#endif

    //PDEBUG("ioctl cmd 0x%08x\n", cmd);
    /*
     * extract the type and number bitfields, and don't decode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    //if (_IOC_TYPE(cmd) != HX170DEC_IOC_MAGIC)
    //    return -ENOTTY;
    //if (_IOC_NR(cmd) > HX170DEC_IOC_MAXNR)
    //    return -ENOTTY;

    /*
     * the direction is a bitmask, and VERIFY_WRITE catches R/W
     * transfers. `Type' is user-oriented, while
     * access_ok is kernel-oriented, so the concept of "read" and
     * "write" is reversed
     */
    //if (_IOC_DIR(cmd) & _IOC_READ)
    //    err = !access_ok(VERIFY_WRITE, (void *) arg, _IOC_SIZE(cmd));
    //else if (_IOC_DIR(cmd) & _IOC_WRITE)
    //    err = !access_ok(VERIFY_READ, (void *) arg, _IOC_SIZE(cmd));

    //if (err)
    //    return -EFAULT;

    switch (cmd)
    {
    case HX170DEC_IOC_CLI:
        //disable_irq(hx170dec_data.irq);
        break;
    case HX170DEC_IOC_STI:
        //enable_irq(hx170dec_data.irq);
        break;
    case HX170DEC_IOCGHWOFFSET:
        //__put_user(multicorebase[0], (unsigned long *) arg);
        outpw(arg, multicorebase[0]);
        break;
    case HX170DEC_IOCGHWIOSIZE:
        //__put_user(hx170dec_data.iosize, (unsigned int *) arg);
        outpw(arg, hx170dec_data.iosize);
        break;
    case HX170DEC_IOC_MC_OFFSETS:
    {
        memcpy(arg, multicorebase, sizeof(multicorebase));

        //tmp = copy_to_user((unsigned long *) arg, multicorebase, sizeof(multicorebase));
        //if (err)
        //{
        //    PDEBUG("copy_to_user failed, returned %li\n", tmp);
        //    return -EFAULT;
        //}
        break;
    }
    case HX170DEC_IOC_MC_CORES:
        //__put_user(hx170dec_data.cores, (unsigned int *) arg);
        outpw(arg, hx170dec_data.cores);
        break;

    case HX170DEC_IOCS_DEC_PUSH_REG:
    {
        struct core_desc core;

        memcpy(&core, (void *)arg, sizeof(struct core_desc));

        /* get registers from user space*/
        //tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
        //if (tmp)
        //{
        //    PDEBUG("copy_from_user failed, returned %li\n", tmp);
        //    return -EFAULT;
        //}

        DecFlushRegs(&hx170dec_data, &core);
        break;
    }
    case HX170DEC_IOCS_PP_PUSH_REG:
    {
        struct core_desc core;

        /* get registers from user space*/
        //tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
        //if (tmp)
        //{
        //    PDEBUG("copy_from_user failed, returned %li\n", tmp);
        //   return -EFAULT;
        //}
        memcpy(&core, (void *)arg, sizeof(struct core_desc));

        PPFlushRegs(&hx170dec_data, &core);
        break;
    }
    case HX170DEC_IOCS_DEC_PULL_REG:
    {
        struct core_desc core;

        /* get registers from user space*/
        //tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
        //if (tmp)
        //{
        //    PDEBUG("copy_from_user failed, returned %li\n", tmp);
        //    return -EFAULT;
        //}
        memcpy(&core, (void *)arg, sizeof(struct core_desc));

        return DecRefreshRegs(&hx170dec_data, &core);
    }
    case HX170DEC_IOCS_PP_PULL_REG:
    {
        struct core_desc core;

        /* get registers from user space*/
        //tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
        //if (tmp)
        //{
        //    PDEBUG("copy_from_user failed, returned %li\n", tmp);
        //    return -EFAULT;
        //}
        memcpy(&core, (void *)arg, sizeof(struct core_desc));

        return _PPRefreshRegs(&hx170dec_data, &core);
    }
    case HX170DEC_IOCH_DEC_RESERVE:
    {
        //PDEBUG("Reserve DEC core, format = %li\n", arg);
        return 0;  // ReserveDecoder(&hx170dec_data, filp, arg);
    }
    case HX170DEC_IOCT_DEC_RELEASE:
    {
        //if(arg >= hx170dec_data.cores || dec_owner[arg] != filp)
        //{
        //    PDEBUG("bogus DEC release, core = %li\n", arg);
        //    return -EFAULT;
        //}

        //PDEBUG("Release DEC, core = %li\n", arg);

        // ReleaseDecoder(&hx170dec_data, arg);

        break;
    }
    case HX170DEC_IOCQ_PP_RESERVE:
        return 0;  // ReservePostProcessor(&hx170dec_data, filp);
    case HX170DEC_IOCT_PP_RELEASE:
    {
        //if(arg != 0 || pp_owner[arg] != filp)
        //{
        //    PDEBUG("bogus PP release %li\n", arg);
        //    return -EFAULT;
        //}

        //ReleasePostProcessor(&hx170dec_data, arg);

        break;
    }
    case HX170DEC_IOCX_DEC_WAIT:
    {
        struct core_desc core;

        /* get registers from user space */
        //tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
        //if (tmp)
        //{
        //    PDEBUG("copy_from_user failed, returned %li\n", tmp);
        //    return -EFAULT;
        //}
        memcpy(&core, (void *)arg, sizeof(struct core_desc));

        return WaitDecReadyAndRefreshRegs(&hx170dec_data, &core);
    }
    case HX170DEC_IOCX_PP_WAIT:
    {
        struct core_desc core;

        /* get registers from user space */
        //tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
        //if (tmp)
        //{
        //    PDEBUG("copy_from_user failed, returned %li\n", tmp);
        //    return -EFAULT;
        //}
        memcpy(&core, (void *)arg, sizeof(struct core_desc));

        return WaitPPReadyAndRefreshRegs(&hx170dec_data, &core);
    }
    case HX170DEC_IOCG_CORE_WAIT:
    {
        int id;
        tmp = WaitCoreReady(&hx170dec_data, &id);
        //__put_user(id, (int *) arg);
        outpw(arg, id);

        return tmp;
    }
    case HX170DEC_IOX_ASIC_ID:
    {
        u32 id;

        // __get_user(id, (u32*)arg);
        id = inpw(arg);

        //if(id >= hx170dec_data.cores)
        //{
        //    return -EFAULT;
        //}
        id = inpw((void *)hx170dec_data.hwregs[id]);
        //__put_user(id, (u32 *) arg);
        outpw(arg, id);
    }
    case HX170DEC_DEBUG_STATUS:
    {
        sysprintf("hx170dec: dec_irq     = 0x%08x \n", dec_irq);
        sysprintf("hx170dec: pp_irq      = 0x%08x \n", pp_irq);

        //sysprintf("hx170dec: IRQs received/sent2user = %d / %d \n",
        //        atomic_read(&irq_rx), atomic_read(&irq_tx));

        for (tmp = 0; tmp < hx170dec_data.cores; tmp++)
        {
            sysprintf("hx170dec: dec_core[%li] %s\n",
                      tmp, dec_owner[tmp] == NULL ? "FREE" : "RESERVED");
            sysprintf("hx170dec: pp_core[%li]  %s\n",
                      tmp, pp_owner[tmp] == NULL ? "FREE" : "RESERVED");
        }
    }
    default:
        return -1; //-ENOTTY;
    }

    return 0;
}

#if 0
/*------------------------------------------------------------------------------
 Function name   : hx170dec_open
 Description     : open method

 Return type     : int
------------------------------------------------------------------------------*/
static int hx170dec_open(struct inode *inode, struct file *filp)
{
    PDEBUG("dev opened\n");
    return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hx170dec_release
 Description     : Release driver

 Return type     : int
------------------------------------------------------------------------------*/
static int hx170dec_release(struct inode *inode, struct file *filp)
{
    int n;
    hx170dec_t *dev = &hx170dec_data;

    PDEBUG("closing ...\n");

    for (n = 0; n < dev->cores; n++)
    {
        if (dec_owner[n] == filp)
        {
            PDEBUG("releasing dec core %i lock\n", n);
            ReleaseDecoder(dev, n);
        }
    }

    for (n = 0; n < 1; n++)
    {
        if (pp_owner[n] == filp)
        {
            PDEBUG("releasing pp core %i lock\n", n);
            ReleasePostProcessor(dev, n);
        }
    }

    PDEBUG("closed\n");
    return 0;
}

/* VFS methods */
static struct file_operations hx170dec_fops =
{
    .owner = THIS_MODULE,
    .open = hx170dec_open,
    .release = hx170dec_release,
    .unlocked_ioctl = hx170dec_ioctl,
    .fasync = NULL
};
#endif

/*------------------------------------------------------------------------------
 Function name   : hx170dec_init
 Description     : Initialize the driver

 Return type     : int
------------------------------------------------------------------------------*/

int  hx170dec_init(void)
{
    int result, i;

    sysprintf("hx170dec: dec/pp kernel module. \n");

    /* If base_port is set at load, use that for single core legacy mode */
    if (base_port != -1)
    {
        multicorebase[0] = base_port;
        elements = 1;
        sysprintf("hx170dec: Init single core at 0x%16lx IRQ=%i\n",
                  multicorebase[0], irq);
    }
    else
    {
        sysprintf("hx170dec: Init multi core[0] at 0x%16lx\n"
                  "                     core[1] at 0x%16lx\n"
                  "                     core[2] at 0x%16lx\n"
                  "                     core[3] at 0x%16lx\n"
                  "          IRQ=%i\n",
                  multicorebase[0], multicorebase[1],
                  multicorebase[2], multicorebase[3],
                  irq);
    }

    hx170dec_data.iosize = DEC_IO_SIZE;
    hx170dec_data.irq = irq;

    for (i = 0; i < HXDEC_MAX_CORES; i++)
    {
        hx170dec_data.hwregs[i] = 0;
        /* If user gave less core bases that we have by default,
         * invalidate default bases
         */
        if (elements && i >= elements)
        {
            multicorebase[i] = -1;
        }
    }

    //hx170dec_data.async_queue_dec = NULL;
    //hx170dec_data.async_queue_pp = NULL;

    //result = register_chrdev(hx170dec_major, "hx170dec", &hx170dec_fops);
    //if(result < 0)
    //{
    //    sysprintf("hx170dec: unable to get major %d\n", hx170dec_major);
    //    goto err;
    //}
    //else if(result != 0)    /* this is for dynamic major */
    //{
    //    hx170dec_major = result;
    //}

    result = ReserveIO();
    if (result < 0)
    {
        return -1; //goto err;
    }

    memset(dec_owner, 0, sizeof(dec_owner));
    memset(pp_owner, 0, sizeof(pp_owner));

    //sema_init(&dec_core_sem, hx170dec_data.cores);
    //sema_init(&pp_core_sem, 1);

    /* read configuration fo all cores */
    ReadCoreConfig(&hx170dec_data);

    /* Register ISR */
    rt_completion_init(&sComplPP);
    rt_completion_init(&sComplDEC);

    /* reset hardware */
    ResetAsic(&hx170dec_data);

    rt_hw_interrupt_install(VDE_IRQn, hx170dec_isr, NULL, "vde");
    rt_hw_interrupt_umask(VDE_IRQn);

    return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hx170dec_cleanup
 Description     : clean up

 Return type     : int
------------------------------------------------------------------------------*/
#if 0
void __exit hx170dec_cleanup(void)
{
    //hx170dec_t *dev = &hx170dec_data;

    /* reset hardware */
    //ResetAsic(dev);
    ResetAsic(&hx170dec_data);

    /* free the IRQ */
    //if(dev->irq != -1)
    //{
    //    free_irq(dev->irq, (void *) dev);
    //}

    //ReleaseIO();

    //unregister_chrdev(hx170dec_major, "hx170dec");

    sysprintf("hx170dec: module removed\n");
    return;
}
#endif

/*------------------------------------------------------------------------------
 Function name   : CheckHwId
 Return type     : int
------------------------------------------------------------------------------*/
static int CheckHwId(hx170dec_t *dev)
{
    long int hwid;
    int i;
    size_t numHw = sizeof(DecHwId) / sizeof(*DecHwId);

    int found = 0;

    for (i = 0; i < dev->cores; i++)
    {
        if (dev->hwregs[i] != NULL)
        {
            // hwid = readl(dev->hwregs[i]);
            hwid = inpw((void *)dev->hwregs[i]);
            sysprintf("hx170dec: Core %d HW ID=0x%08lx\n", i, hwid);
            hwid = (hwid >> 16) & 0xFFFF; /* product version only */

            while (numHw--)
            {
                if (hwid == DecHwId[numHw])
                {
                    sysprintf("hx170dec: Supported HW found at 0x%08lx\n",
                              multicorebase[i]);
                    found++;
                    break;
                }
            }
            if (!found)
            {
                sysprintf("hx170dec: Unknown HW found at 0x%08lx\n",
                          multicorebase[i]);
                return 0;
            }
            found = 0;
            numHw = sizeof(DecHwId) / sizeof(*DecHwId);
        }
    }

    return 1;
}

/*------------------------------------------------------------------------------
 Function name   : ReserveIO
 Description     : IO reserve

 Return type     : int
------------------------------------------------------------------------------*/
static int ReserveIO(void)
{
    int i;

    for (i = 0; i < HXDEC_MAX_CORES; i++)
    {
        if (multicorebase[i] != -1)
        {
            //if (!request_mem_region(multicorebase[i], hx170dec_data.iosize,
            //        "hx170dec0"))
            //{
            //    sysprintf("hx170dec: failed to reserve HW regs\n");
            //    return -EBUSY;
            //}

            //hx170dec_data.hwregs[i] = (volatile u8 *) ioremap_nocache(multicorebase[i],
            //        hx170dec_data.iosize);
            hx170dec_data.hwregs[i] = (u8 *)multicorebase[i];

            //if (hx170dec_data.hwregs[i] == NULL )
            //{
            //    sysprintf("hx170dec: failed to ioremap HW regs\n");
            //    ReleaseIO();
            //    return -EBUSY;
            //}
            hx170dec_data.cores++;
        }
    }

    /* check for correct HW */
    if (!CheckHwId(&hx170dec_data))
    {
        //ReleaseIO();
        return -1;  //-EBUSY;
    }

    return 0;
}

/*------------------------------------------------------------------------------
 Function name   : releaseIO
 Description     : release

 Return type     : void
------------------------------------------------------------------------------*/
#if 0
static void ReleaseIO(void)
{
    int i;
    for (i = 0; i < hx170dec_data.cores; i++)
    {
        if (hx170dec_data.hwregs[i])
            iounmap((void *) hx170dec_data.hwregs[i]);
        release_mem_region(multicorebase[i], hx170dec_data.iosize);
    }
}
#endif

/*------------------------------------------------------------------------------
 Function name   : hx170dec_isr
 Description     : interrupt handler

 Return type     : irqreturn_t
------------------------------------------------------------------------------*/
//#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
//irqreturn_t hx170dec_isr(int irq, void *dev_id, struct pt_regs *regs)
//#else
//irqreturn_t hx170dec_isr(int irq, void *dev_id)
//#endif
static void hx170dec_isr(int vector, void *param)
{
    unsigned long flags;
    unsigned int handled = 0;
    int i;
    volatile u8 *hwregs;

    hx170dec_t *dev = &hx170dec_data;   //  (hx170dec_t *) dev_id;
    u32 irq_status_dec;
    u32 irq_status_pp;

    for (i = 0; i < dev->cores; i++)
    {
        volatile u8 *hwregs = dev->hwregs[i];

        /* interrupt status register read */
        //irq_status_dec = ioread32(hwregs + HX170_IRQ_STAT_DEC_OFF);
        irq_status_dec = inpw((void *)hwregs + HX170_IRQ_STAT_DEC_OFF);

        if (irq_status_dec & HX170_DEC_IRQ)
        {
            /* clear dec IRQ */
            irq_status_dec &= (~HX170_DEC_IRQ);
            // iowrite32(irq_status_dec, hwregs + HX170_IRQ_STAT_DEC_OFF);
            outpw((void *)hwregs + HX170_IRQ_STAT_DEC_OFF, irq_status_dec);

            // PDEBUG("decoder IRQ received! core %d\n", i);

            dec_irq |= (1 << i);

            vde_os_signal_dec_event();

            handled++;
        }
    }

    /* check PP also */
    hwregs = dev->hwregs[0];
    irq_status_pp = inpw((void *)hwregs + HX170_IRQ_STAT_PP_OFF);   // ioread32(hwregs + HX170_IRQ_STAT_PP_OFF);
    if (irq_status_pp & HX170_PP_IRQ)
    {
        /* clear pp IRQ */
        irq_status_pp &= (~HX170_PP_IRQ);
        // iowrite32(irq_status_pp, hwregs + HX170_IRQ_STAT_PP_OFF);
        outpw((void *)hwregs + HX170_IRQ_STAT_PP_OFF, irq_status_pp);

        //PDEBUG("post-processor IRQ received!\n");

        pp_irq |= 1;

        vde_os_signal_pp_event();

        handled++;
    }

    if (!handled)
    {
        PDEBUG("IRQ received, but not x170's!\n");
    }
    else
    {
        // rt_kprintf("hx170dec_isr: dec_irq=0x%x, pp_irq=0x%x\n", dec_irq, pp_irq);
    }
}

/*------------------------------------------------------------------------------
 Function name   : ResetAsic
 Description     : reset asic

 Return type     :
------------------------------------------------------------------------------*/
void ResetAsic(hx170dec_t *dev)
{
    int i, j;
    u32 status;

    for (j = 0; j < dev->cores; j++)
    {
        //status = ioread32(dev->hwregs[j] + HX170_IRQ_STAT_DEC_OFF);
        status = inpw((void *)dev->hwregs[j] + HX170_IRQ_STAT_DEC_OFF);

        if (status & HX170_DEC_E)
        {
            /* abort with IRQ disabled */
            status = HX170_DEC_ABORT | HX170_DEC_IRQ_DISABLE;
            // iowrite32(status, dev->hwregs[j] + HX170_IRQ_STAT_DEC_OFF);
            outpw((void *)dev->hwregs[j] + HX170_IRQ_STAT_DEC_OFF, status);
        }

        /* reset PP */
        // iowrite32(0, dev->hwregs[j] + HX170_IRQ_STAT_PP_OFF);
        outpw((void *)dev->hwregs[j] + HX170_IRQ_STAT_PP_OFF, 0);

        for (i = 4; i < dev->iosize; i += 4)
        {
            // iowrite32(0, dev->hwregs[j] + i);
            outpw((void *)dev->hwregs[j] + i, 0);
        }
    }
}

/*------------------------------------------------------------------------------
 Function name   : dump_regs
 Description     : Dump registers

 Return type     :
------------------------------------------------------------------------------*/
#ifdef HX170DEC_DEBUG
void dump_regs(hx170dec_t *dev)
{
    int i, c;

    PDEBUG("Reg Dump Start\n");
    for (c = 0; c < dev->cores; c++)
    {
        for (i = 0; i < dev->iosize; i += 4 * 4)
        {
            PDEBUG("\toffset %04X: %08X  %08X  %08X  %08X\n", i,
                   inpw((void *)dev->hwregs[c] + i),
                   inpw((void *)dev->hwregs[c] + i + 4),
                   inpw((void *)dev->hwregs[c] + i + 16),
                   inpw((void *)dev->hwregs[c] + i + 24));
        }
    }
    PDEBUG("Reg Dump End\n");
}
#endif


//module_init( hx170dec_init);
//module_exit( hx170dec_cleanup);

/* module description */
//MODULE_LICENSE("Proprietary");
//MODULE_AUTHOR("Google Finland Oy");
//MODULE_DESCRIPTION("Driver module for Hantro Decoder/Post-Processor");

