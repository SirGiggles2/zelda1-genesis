/* T-294: shared title/story extracted from the authenticated user Redux ROM.
 * Presentation does not depend on the selected gameplay/save profile. */
#ifndef INTRO_REDUX_ASSETS_H
#define INTRO_REDUX_ASSETS_H
extern const unsigned char core_redux_common_chr[7616];
extern const unsigned char core_redux_demo_chr[8768];
extern const unsigned char core_redux_intro_sprite_chr[8192];
extern const unsigned char core_redux_intro_misc_chr[448];
extern const unsigned char core_redux_intro_blink_chr[256];
extern const unsigned short core_redux_intro_story_map[960];
extern const unsigned short core_redux_intro_items_map[5632];
extern const unsigned short core_redux_intro_title_map[960];
extern const unsigned short core_redux_intro_story_palette[64];
extern const unsigned short core_redux_intro_title_palette[64];
extern const unsigned short core_redux_intro_title_fade[896];
extern const unsigned short core_redux_intro_title_glow[8];
#endif
