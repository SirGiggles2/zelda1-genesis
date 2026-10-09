#ifndef CELLAR_MODE_H
#define CELLAR_MODE_H

/* Native mode $10 -> 9 -> A -> 4 cellar lifecycle. The host owns only
 * plane publication, existing room initialization and the Link mirror. */
unsigned char cellar_try_enter(void);
unsigned char cellar_check_exit(void);
unsigned char cellar_mode_tick(void);
void cellar_host_layout(unsigned char cellar);
void cellar_host_enter(void);
void cellar_host_walk(void);
void cellar_host_prepare(void);
void cellar_host_return_layout(void);
unsigned char cellar_host_clock_busy(void);
void cellar_host_draw(void);

#endif
