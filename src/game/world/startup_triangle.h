#ifndef STARTUP_TRIANGLE_H
#define STARTUP_TRIANGLE_H
void startup_triangle_arm(unsigned char enhanced);
unsigned char startup_triangle_preserve_scene(void);
void startup_triangle_prepare(short horizontal, short vertical);
unsigned char startup_triangle_active(void);
/* Returns one when the full screen is ready for the normal mode handoff. */
unsigned char startup_triangle_tick(void);
#endif
