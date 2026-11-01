/* T-393 frame pipeline: V-Int presentation of a committed game frame.
 *
 * A committed frame is an immutable snapshot of the SGDK DMA queue (RAM
 * sources copied into a private arena). The V-Int callback replays it at
 * the next VBlank, so the next game update can start without idling to
 * that VBlank. Mutual exclusion: while a frame is pending, the main thread
 * must not touch the VDP ports outside an interrupt-masked section. */
#ifndef FRAME_PIPELINE_H
#define FRAME_PIPELINE_H

/* Boot, after a successful heap wall: snapshot buffers come from the SGDK
 * heap. Returns frame_pipeline_ready (0 = pipeline disabled, probe-visible). */
unsigned char frame_pipeline_init(void);
extern unsigned char frame_pipeline_ready;
/* Incremented by every commit (each empties the SGDK DMA queue). */
extern unsigned long frame_pipeline_commits;
unsigned char frame_pipeline_try_commit(void);
unsigned char frame_pipeline_pending(void);
void frame_pipeline_wait(void);
void frame_pipeline_vint(void);

/* Count of frames presented by V-Int; probes key presented video by it. */
extern volatile unsigned long frame_pipeline_presented;

#endif
