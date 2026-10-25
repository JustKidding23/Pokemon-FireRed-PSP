// Renders one of the game's songs to a WAV file with the game's own sound
// engine, on a PC: for the PSP menu's music (psp/make_art.sh), so it can be
// made without the ROM's sound being recorded in an emulator.
//
// usage: songrender SONG_ID SECONDS OUT.wav
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "m4a.h"
#include "platform.h"
#include "platform/system.h"
#include "cgb_audio.h"

// What src/platform/system.c defines for the game: the sound engine's state
// and the GBA's registers, which the Game Boy channels read
struct SoundInfo *SOUND_INFO_PTR;
unsigned char REG_BASE[0x400] __attribute__((aligned(4)));

static FILE *sOut;
static unsigned long sBytes;

void Platform_QueueAudio(float *audioBuffer, s32 bytes)
{
    // The mixer's frame of stereo floats, as 16-bit PCM
    for (s32 i = 0; i < bytes / (s32)sizeof(float); i++)
    {
        float v = audioBuffer[i];
        s16 sample;

        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        sample = (s16)(v * 32767.0f);
        fwrite(&sample, 2, 1, sOut);
        sBytes += 2;
    }
}

static void WriteHeader(u32 rate)
{
    u32 n;
    u16 h;

    fseek(sOut, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, sOut);
    n = 36 + sBytes; fwrite(&n, 4, 1, sOut);
    fwrite("WAVEfmt ", 1, 8, sOut);
    n = 16; fwrite(&n, 4, 1, sOut);
    h = 1; fwrite(&h, 2, 1, sOut);
    h = 2; fwrite(&h, 2, 1, sOut);
    fwrite(&rate, 4, 1, sOut);
    n = rate * 4; fwrite(&n, 4, 1, sOut);
    h = 4; fwrite(&h, 2, 1, sOut);
    h = 16; fwrite(&h, 2, 1, sOut);
    fwrite("data", 1, 4, sOut);
    n = sBytes; fwrite(&n, 4, 1, sOut);
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s SONG_ID SECONDS OUT.wav\n", argv[0]);
        return 1;
    }
    sOut = fopen(argv[3], "wb");
    if (sOut == NULL)
        return 1;
    WriteHeader(0);
    // As the game's platform code does before the sound engine starts
    cgb_audio_init(AUDIO_SAMPLE_RATE);
    m4aSoundInit();
    m4aSongNumStart(strtoul(argv[1], NULL, 0));
    for (long frame = 0; frame < (long)(atof(argv[2]) * 60); frame++)
    {
        m4aSoundMain();
        m4aSoundVSync();
    }
    WriteHeader(AUDIO_SAMPLE_RATE);
    fclose(sOut);
    return 0;
}
