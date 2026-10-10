#ifndef CIRCLE_TRANSITION_H
#define CIRCLE_TRANSITION_H
void circle_transition_set_mode(unsigned char enhanced);
unsigned char circle_transition_enabled(void);
unsigned char circle_transition_active(void);
unsigned char circle_transition_closing(void);
unsigned char circle_transition_waiting(void);
void circle_transition_close(short x, short y, short horizontal, short vertical);
void circle_transition_open(short x, short y, short horizontal, short vertical);
void circle_transition_tick(void);
#endif
