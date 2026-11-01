#ifndef CONTROLLER_ABI_H
#define CONTROLLER_ABI_H
/* Initialize detected adapter support before the title/file-select loop.
 * Reads return raw SGDK button masks sampled by its VBlank polling. */
void controller_party_init(void);
/* Apply file-mode convenience controls after profile selection. */
void controller_party_profile(unsigned char remix);
unsigned short controller_read_party(unsigned char player);
#endif
