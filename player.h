#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

void plr_stop();
void plr_volume(uint16_t lVol, uint16_t rVol);
int plr_pump();
int plr_length(const char *path);
int plr_play(const char *path);

#endif
