#ifndef RENDER_H
#define RENDER_H

#include "game.h"

// Virtual screen: left half is the minimap, right half is the 3D view + HUD.
#define SCREEN_W 1024
#define SCREEN_H 512

void render_init(void);
void render_frame(const Game *g);

#endif
