/* T-393 frame pipeline (presentation only; no game state).
 *
 * NES source: Z_07.asm NMI publishes the finished frame each VBlank.
 * Drained C: none (Genesis presentation layer).
 * Coverage: SGDK DMA queue contents of ordinary play updates.
 * Stance: EXTEND render adapter. SYS_doVBlankProcessEx(ON_VBLANK) flushes
 * the queue only after the main thread reaches the wait, so an update that
 * runs past VBlank idles to the next one. Here the queue is frozen at
 * commit and replayed from V-Int, exactly as DMA_flushQueue would send it
 * (same op words, same Z80 halt and bus protection, autoinc restored). */
#include <genesis.h>
#include "frame_pipeline.h"

#define FP_MAX_OPS     32u
#define FP_ARENA_WORDS 1024u     /* 2 KB: SAT + CRAM + sub-palette tile copies */

/* Heap blocks above the walled NES mirror: .bss has no 2.5 KB to spare. */
static DMAOpInfo *s_ops;
static unsigned short s_op_count;
static unsigned short *s_arena;
static volatile unsigned char s_pending;
volatile unsigned long frame_pipeline_presented;
unsigned long frame_pipeline_commits;
unsigned char frame_pipeline_ready;

static unsigned long op_source(const DMAOpInfo *op)
{
    /* Inverse of DMA_queueDmaFast's regAddrMStep/regAddrHAddrL packing. */
    return (((op->regAddrHAddrL >> 16) & 0x7Fu) << 17) |
           (((op->regAddrMStep >> 16) & 0xFFu) << 9) |
           ((op->regAddrHAddrL & 0xFFu) << 1);
}

static unsigned short op_words(const DMAOpInfo *op)
{
    return (unsigned short)((op->regLenL & 0xFFu) | ((op->regLenH & 0xFFu) << 8));
}

unsigned char frame_pipeline_init(void)
{
    if (!s_ops) s_ops = (DMAOpInfo *)MEM_alloc(FP_MAX_OPS * sizeof(DMAOpInfo));
    if (!s_arena) s_arena = (unsigned short *)MEM_alloc(FP_ARENA_WORDS * 2u);
    frame_pipeline_ready = (unsigned char)(s_ops && s_arena);
    return frame_pipeline_ready;
}

unsigned char frame_pipeline_try_commit(void)
{
    const unsigned short n = DMA_getQueueSize();
    unsigned short i, used = 0u;
    if (s_pending || !frame_pipeline_ready || n > FP_MAX_OPS) return 0u;
    /* Over-capacity queues truncate inside DMA_flushQueue; keep that path. */
    if (DMA_getMaxTransferSize() && DMA_getQueueTransferSize() > DMA_getMaxTransferSize())
        return 0u;
    for (i = 0u; i < n; ++i) {
        const unsigned long from = op_source(&dmaQueues[i]);
        if (from >= 0xE00000UL) {
            const unsigned short len = op_words(&dmaQueues[i]);
            if (len > (unsigned short)(FP_ARENA_WORDS - used)) return 0u;
            used = (unsigned short)(used + len);
        }
    }
    used = 0u;
    for (i = 0u; i < n; ++i) {
        DMAOpInfo op = dmaQueues[i];
        const unsigned long from = op_source(&op);
        if (from >= 0xE00000UL) {
            const unsigned short len = op_words(&op);
            unsigned short *dst = &s_arena[used];
            const unsigned long to = (unsigned long)dst;
            memcpy(dst, (const void *)from, (u16)(len * 2u));
            used = (unsigned short)(used + len);
            op.regAddrMStep = (op.regAddrMStep & 0xFF00FFFFUL) | ((to << 7) & 0xFF0000UL);
            op.regAddrHAddrL = 0x97009500UL + ((to >> 1) & 0x7F00FFUL);
        }
        s_ops[i] = op;
    }
    s_op_count = n;
    DMA_clearQueue();
    frame_pipeline_commits++;
    __asm__ volatile("" ::: "memory");   /* snapshot complete before V-Int sees it */
    s_pending = 1u;
    return 1u;
}

unsigned char frame_pipeline_pending(void)
{
    return s_pending;
}

void frame_pipeline_wait(void)
{
    unsigned short sr;
    if (!s_pending) return;
    __asm__ volatile ("move.w %%sr,%0" : "=d"(sr));
    if ((sr & 0x0700u) >= 0x0600u) {
        /* V-Int is masked by the caller: publish at VBlank from here. */
        while (!(*(volatile unsigned short *)0xC00004 & 0x0008u)) { }
        frame_pipeline_vint();
        return;
    }
    while (s_pending) { /* V-Int presents and clears it. */ }
}

/* noinline: probes stop at its return (presented-frame video, T-393 A/B). */
__attribute__((noinline)) void frame_pipeline_vint(void)
{
    if (!s_pending) return;
    {
        volatile unsigned long *const ctrl = (volatile unsigned long *)0xC00004;
        volatile unsigned short *const ctrlw = (volatile unsigned short *)0xC00004;
        const unsigned long *w = (const unsigned long *)(const void *)s_ops;
        unsigned short i;
        bool bus_taken;
        Z80_enableBusProtection();
        if (Z80_getForceDelayDMA()) waitSubTick(10);
        bus_taken = Z80_isBusTaken();
        if (!bus_taken) Z80_requestBus(FALSE);
        for (i = 0u; i < s_op_count; ++i) {
            /* flushQueue: three longs then two words per DMAOpInfo. */
            *ctrl = w[0];
            *ctrl = w[1];
            *ctrl = w[2];
            *ctrlw = ((const unsigned short *)(const void *)w)[6];
            *ctrlw = ((const unsigned short *)(const void *)w)[7];
            w += 4;
        }
        if (!bus_taken) Z80_releaseBus();
        *ctrlw = (unsigned short)(0x8F00u | VDP_getAutoInc());
        Z80_disableBusProtection();
    }
    s_op_count = 0u;
    frame_pipeline_presented++;
    __asm__ volatile("" ::: "memory");
    s_pending = 0u;
}
