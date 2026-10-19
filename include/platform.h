#ifndef GUARD_PLATFORM_H
#define GUARD_PLATFORM_H

#include "global.h"

void Platform_StoreSaveFile(void);
void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size);
void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame);
u16 Platform_GetKeyInput(void);
// A clock in microseconds, for the GBA timers the game reads (see StartTimer1)
u64 Platform_GetMicroseconds(void);

// The PSP's trace (see psp.c) notes each task and callback before it runs
#ifdef __PSP__
void Platform_TraceCall(const void *func);
// Opens the PSP's save file screen over the game, from FILE SELECT in the
// start menu (inGame) or the main menu. The game keeps running without the
// buttons.
void Platform_OpenFileSelect(bool8 inGame);
// Whether Circle confirms and Cross cancels (the button prompts follow it)
bool8 Platform_ConfirmIsCircle(void);
#else
#define Platform_TraceCall(func) ((void)0)
#endif

#endif