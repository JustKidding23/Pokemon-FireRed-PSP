#ifndef GUARD_FRAMEDRAW_H
#define GUARD_FRAMEDRAW_H

#include "global.h"

// The wider view: the renderer draws RENDER_MARGIN more columns left and right
// of the GBA's 240. Game coordinates stay the GBA's, so column i of a line is
// the game's x = i - RENDER_MARGIN. 282 columns at 1.7x fill the PSP's 480x272.
#ifdef __PSP__
#define RENDER_MARGIN 21
#else
#define RENDER_MARGIN 0
#endif
#define RENDER_WIDTH (DISPLAY_WIDTH + 2 * RENDER_MARGIN)
// Lines are this far apart in the frame, a multiple of 8 pixels, which the
// PSP's GE needs for a texture's rows
#define RENDER_STRIDE ((RENDER_WIDTH + 7) & ~7)

void DrawFrame(uint16_t *pixels);
// The fade to black (3 << 8 | strength out of 16) or white (2 << 8 | strength)
// over the whole frame DrawFrame drew, which it left for the screen to do, or 0
unsigned int DrawFrameFade(void);
// Runs a frame's line interrupts and HBlank DMAs like DrawFrame, without
// drawing it, for a frame that's skipped
void SkipFrame(void);
#ifdef __PS2__
// Draws the frame with the GS (see ps2_gs.c), for PS2GS_Present to show
void DrawFramePS2(void);
#endif
#endif