#ifdef PORTABLE
#include "global.h"
#include "cgb_audio.h"

// The GBA samples the Game Boy channels at 65536 Hz (with SOUNDBIAS set up the
// way the sound engine does it), so they're made at that rate here too, from
// the channels' CPU cycle timings, and then filtered down to the output rate.
// Point sampling the channels at the output rate aliases their harmonics to
// frequencies that aren't in the tune, which garbles high notes.
#define APU_RATE 65536
#define CYCLES_PER_SAMPLE 64    // of the 4194304 Hz CPU clock
#define SEQUENCER_SAMPLES 128   // the frame sequencer steps at 512 Hz

// The resampling filter: TAPS input samples per output sample, with its
// coefficients worked out for PHASES positions between input samples
#if defined(__PS2__) || defined(__PSP__)
// Half the taps, which is half the work, for a softer cutoff: the full filter
// alone takes a fifth of the PS2's time, and the PSP is about as fast
#define TAPS 32
#else
#define TAPS 64
#endif
#define PHASES 512
#define HIGH_RATE_CAPACITY 2048

static struct AudioCGB gb;
static u32 sampleRate;

static float sCoefficients[PHASES + 1][TAPS];
static bool8 sCoefficientsReady;
static float sHighRate[HIGH_RATE_CAPACITY][2];
static u32 sHighRateCount;
// Just past the last sample in sHighRate that isn't +0. Most of the time the
// channels are quiet, and filtering nothing but +0 gives +0.
static u32 sHighRateSoundEnd;
// Where the next output sample is in sHighRate: the sample before it, and how
// far past that one it is in units of 1/sampleRate samples. Kept apart instead
// of as one count, which would need a 64-bit division every sample, and that's
// slow on 32-bit CPUs without one, like the PS2's.
static u32 sResampleIndex;
static u32 sResampleFrac;

// The duty cycles' steps, with each at the same level on average, as the GBA's
// output has its DC removed, so a volume change doesn't click
static const float sDutyLevels[4][8] = {
    {-0.25f, -0.25f, -0.25f, -0.25f, -0.25f, -0.25f, -0.25f, 1.75f},  // 12.5%
    {1.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 1.5f},           // 25%
    {1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f},             // 50%
    {-1.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -1.5f},               // 75%
};

#define PI 3.14159265358979323846

// sin(x) worked out the same way everywhere, unlike libm's
static double Sine(double x){
    x -= 2 * PI * (double)(s64)(x / (2 * PI) + (x >= 0 ? 0.5 : -0.5));
    double term = x, sum = x;
    for(int n = 1; n < 14; n++){
        term *= -x * x / ((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

// A windowed sinc low-pass, cutting off below the output's Nyquist frequency
static void MakeCoefficients(void){
    double cutoff = 0.45 * sampleRate / APU_RATE;  // in cycles per input sample

    for(int phase = 0; phase <= PHASES; phase++){
        double sum = 0;
        for(int tap = 0; tap < TAPS; tap++){
            // How far the tap's input sample is from the output sample
            double t = (double)phase / PHASES + TAPS / 2 - 1 - tap;
            double window = 0.42 + 0.5 * Sine(PI * t / (TAPS / 2) + PI / 2) + 0.08 * Sine(2 * PI * t / (TAPS / 2) + PI / 2);
            double sinc = t == 0 ? 1 : Sine(2 * PI * cutoff * t) / (2 * PI * cutoff * t);
            if(t <= -TAPS / 2 || t >= TAPS / 2)
                window = 0;
            sCoefficients[phase][tap] = window * sinc;
            sum += window * sinc;
        }
        for(int tap = 0; tap < TAPS; tap++)
            sCoefficients[phase][tap] /= sum;
    }
}

void cgb_audio_init(u32 rate){
    // Everything starts from zero, also after a soft reset
    memset(&gb, 0, sizeof(gb));
    gb.lfsr = 0x7FFF;
    gb.sequencerSamples = SEQUENCER_SAMPLES;
    if(!sCoefficientsReady || rate != sampleRate){
        sampleRate = rate;
        MakeCoefficients();
        sCoefficientsReady = TRUE;
    }
    // Silence to start, so the filter has what came before the first sample
    memset(sHighRate, 0, sizeof(sHighRate));
    sHighRateCount = TAPS / 2 - 1;
    sHighRateSoundEnd = 0;
    sResampleIndex = TAPS / 2 - 1;
    sResampleFrac = 0;
}


void cgb_set_sweep(u8 sweep){
    gb.ch1SweepDir = (sweep & 0x08) >> 3;
    gb.ch1SweepCounter = gb.ch1SweepCounterI = (sweep & 0x70) >> 4;
    gb.ch1SweepShift = (sweep & 0x07);
}


void cgb_set_wavram(){
    for(u8 wavi = 0; wavi < 0x10; wavi++){
        gb.wave[(wavi << 1)] = ((*(REG_ADDR_WAVE_RAM0 + wavi)) & 0xF0) >> 4;
        gb.wave[(wavi << 1) + 1] = (*(REG_ADDR_WAVE_RAM0 + wavi)) & 0x0F;
    }
}


void cgb_toggle_length(u8 channel, bool8 state){
    gb.LenOn[channel] = state;
}


// The length written counts up to 64 (256 for the wave channel)
void cgb_set_length(u8 channel, u8 length){
    gb.Len[channel] = (channel == 2 ? 256 : 64) - length;
}


void cgb_set_envelope(u8 channel, u8 envelope){
    if(channel == 2){
        switch((envelope & 0xE0)){
            case 0x00:  // mute
                gb.Vol[2] = gb.VolI[2] = 0;
            break;
            case 0x20:  // full
                gb.Vol[2] = gb.VolI[2] = 4;
            break;
            case 0x40:  // half
                gb.Vol[2] = gb.VolI[2] = 2;
            break;
            case 0x60:  // quarter
                gb.Vol[2] = gb.VolI[2] = 1;
            break;
            case 0x80:  // 3 quarters
                gb.Vol[2] = gb.VolI[2] = 3;
            break;
        }
    }else{
        gb.DAC[channel] = (envelope & 0xF8) > 0;
        gb.Vol[channel] = gb.VolI[channel] = (envelope & 0xF0) >> 4;
        gb.EnvDir[channel] = (envelope & 0x08) >> 3;
        gb.EnvCounter[channel] = gb.EnvCounterI[channel] = (envelope & 0x07);
    }
}


static u32 SquarePeriod(u16 freq){
    return (2048 - (freq & 0x7FF)) * 4;
}

static u32 WavePeriod(void){
    return (2048 - (REG_SOUND3CNT_X & 0x7FF)) * 2;
}

// 0 if the noise channel doesn't clock at all
static u32 NoisePeriod(void){
    u8 ratio = REG_NR43 & 7;
    u8 shift = REG_NR43 >> 4;
    if(shift >= 14)
        return 0;
    return (ratio ? ratio * 16 : 8) << shift;
}


// What writing NRx4 with bit 7 set does
void cgb_trigger_note(u8 channel){
    REG_NR52 |= 1 << channel;
    if(gb.Len[channel] == 0)
        gb.Len[channel] = channel == 2 ? 256 : 64;
    gb.Vol[channel] = gb.VolI[channel];
    switch(channel){
        case 0:
            gb.timer[0] = SquarePeriod(REG_SOUND1CNT_X);
            gb.EnvCounter[0] = gb.EnvCounterI[0];
        break;
        case 1:
            gb.timer[1] = SquarePeriod(REG_SOUND2CNT_H);
            gb.EnvCounter[1] = gb.EnvCounterI[1];
        break;
        case 2:
            gb.timer[2] = WavePeriod();
            gb.position[2] = 0;
        break;
        case 3:
            gb.timer[3] = NoisePeriod();
            gb.EnvCounter[3] = gb.EnvCounterI[3];
            gb.lfsr = 0x7FFF;
        break;
    }
}


// The length counters at 256 Hz, the sweep at 128 Hz and the envelopes at 64 Hz
static void StepSequencer(void){
    u8 step = gb.sequencerStep;
    gb.sequencerStep = (step + 1) & 7;

    if((step & 1) == 0){  // Length
        for(u8 ch = 0; ch < 4; ch++){
            if(gb.LenOn[ch] && gb.Len[ch]){
                if(--gb.Len[ch] == 0){
                    REG_NR52 &= (0xFF ^ (1 << ch));
                }
            }
        }
    }

    if(step == 7){  // Envelope
        for(u8 ch = 0; ch < 4; ch++){
            if(ch == 2) continue;  // Skip wave channel
            if(gb.EnvCounter[ch]){
                if(--gb.EnvCounter[ch] == 0){
                    if(gb.Vol[ch] && !gb.EnvDir[ch]){
                        gb.Vol[ch]--;
                        gb.EnvCounter[ch] = gb.EnvCounterI[ch];
                    }else if(gb.Vol[ch] < 0x0F && gb.EnvDir[ch]){
                        gb.Vol[ch]++;
                        gb.EnvCounter[ch] = gb.EnvCounterI[ch];
                    }
                }
            }
        }
    }

    if((step & 3) == 2){  // Sweep
        if(gb.ch1SweepCounterI && gb.ch1SweepShift){
            if(--gb.ch1SweepCounter == 0){
                gb.ch1Freq = REG_SOUND1CNT_X & 0x7FF;
                if(gb.ch1SweepDir){
                    gb.ch1Freq -= gb.ch1Freq >> gb.ch1SweepShift;
                    if(gb.ch1Freq & 0xF800) gb.ch1Freq = 0;
                }else{
                    gb.ch1Freq += gb.ch1Freq >> gb.ch1SweepShift;
                    if(gb.ch1Freq & 0xF800){
                        gb.ch1Freq = 0;
                        gb.EnvCounter[0] = 0;
                        gb.Vol[0] = 0;
                    }
                }
                REG_NR13 = gb.ch1Freq & 0xFF;
                REG_NR14 &= 0xF8;
                REG_NR14 += (gb.ch1Freq >> 8) & 0x07;
                gb.ch1SweepCounter = gb.ch1SweepCounterI;
            }
        }
    }
}


// Runs a channel's timer for one sample, returning how many times it ran out
static u32 RunTimer(u8 channel, u32 period){
    u32 over;

    if(period == 0)
        return 0;
    if(gb.timer[channel] > CYCLES_PER_SAMPLE){
        gb.timer[channel] -= CYCLES_PER_SAMPLE;
        return 0;
    }
    over = CYCLES_PER_SAMPLE - gb.timer[channel];
    // Usually it runs out once, which needs no division (slow on the PSP)
    if(over < period){
        gb.timer[channel] = period - over;
        return 1;
    }
    gb.timer[channel] = period - over % period;
    return 1 + over / period;
}


// The wave channel's sample at its volume, which the hardware does by shifting
// the 4-bit samples, centered like the other channels
static float WaveLevel(u8 sample){
    switch(gb.Vol[2]){
        case 4:  // full
            return (2 * sample - 15) * (1.0f / 15.0f);
        case 3:  // 3 quarters
            return (2 * ((sample * 3) >> 2) - 11) * (1.0f / 15.0f);
        case 2:  // half
            return (2 * (sample >> 1) - 7) * (1.0f / 15.0f);
        case 1:  // quarter
            return (2 * (sample >> 2) - 3) * (1.0f / 15.0f);
        default:
            return 0.0f;
    }
}


// What the channels' registers say, worked out once a frame and after each
// sequencer step instead of for every sample: they only change when the game
// writes them, which it does between frames, or in StepSequencer
static struct {
    u32 period[4];
    bool8 on[4];        // making sound: enabled, DAC on, master on
    u8 leftMask, rightMask;
    bool8 noise7Bit;
    float duty[2][8];   // the square channels' levels at their volume
    float wave[16];     // the wave channel's level for each 4-bit sample
    float noise;        // the noise channel's level when its bit is 0
    u32 noiseQuotient;  // CYCLES_PER_SAMPLE / and % the noise's period, so
    u32 noiseRemainder; // its timer needs no division (see RunNoiseTimer)
} sMix;

static void UpdateMix(void){
    u8 nr52 = REG_NR52;
    bool8 master = (nr52 & 0x80) != 0;

    sMix.period[0] = SquarePeriod(REG_SOUND1CNT_X);
    sMix.period[1] = SquarePeriod(REG_SOUND2CNT_H);
    sMix.period[2] = WavePeriod();
    sMix.period[3] = NoisePeriod();
    sMix.on[0] = master && gb.DAC[0] && (nr52 & 0x01);
    sMix.on[1] = master && gb.DAC[1] && (nr52 & 0x02);
    sMix.on[2] = master && (REG_NR30 & 0x80) && (nr52 & 0x04);
    sMix.on[3] = master && gb.DAC[3] && (nr52 & 0x08);
    sMix.leftMask = REG_NR51 >> 4;
    sMix.rightMask = REG_NR51 & 0x0F;
    sMix.noise7Bit = (REG_NR43 & 0x08) != 0;
    // The levels as GenerateSample used to add them, with the final quarter
    for(u32 pos = 0; pos < 8; pos++){
        sMix.duty[0][pos] = gb.Vol[0] * sDutyLevels[REG_NR11 >> 6][pos] * (1.0f / 15.0f) * 0.25f;
        sMix.duty[1][pos] = gb.Vol[1] * sDutyLevels[REG_NR21 >> 6][pos] * (1.0f / 15.0f) * 0.25f;
    }
    for(u32 sample = 0; sample < 16; sample++)
        sMix.wave[sample] = WaveLevel(sample) * 0.25f;
    sMix.noise = gb.Vol[3] * (1.0f / 15.0f) * 0.25f;
    if(sMix.period[3] != 0){
        sMix.noiseQuotient = CYCLES_PER_SAMPLE / sMix.period[3];
        sMix.noiseRemainder = CYCLES_PER_SAMPLE % sMix.period[3];
    }
}

// RunTimer for the noise channel, which at its fastest runs out 8 times a
// sample: the same results, from the quotient and remainder worked out once
static u32 RunNoiseTimer(void){
    u32 period = sMix.period[3];
    u32 t = gb.timer[3];

    if(period == 0)
        return 0;
    if(t > CYCLES_PER_SAMPLE){
        gb.timer[3] = t - CYCLES_PER_SAMPLE;
        return 0;
    }
    if(t > period){
        // Left over from a slower period: the plain way
        return RunTimer(3, period);
    }
    // 64 - t cycles after it next runs out at t: (64 - t) / period more times
    if(t <= sMix.noiseRemainder){
        gb.timer[3] = period - (sMix.noiseRemainder - t);
        return 1 + sMix.noiseQuotient;
    }
    gb.timer[3] = t - sMix.noiseRemainder;
    return sMix.noiseQuotient;
}

// Clocks the noise's LFSR. In 15-bit mode up to 13 steps at once: each new
// bit is the XOR of two of the register's bits that are still the original
// ones, and they all go in at the top
static void ClockNoise(u32 clocks){
    if(!sMix.noise7Bit){
        while(clocks > 0){
            u32 k = clocks > 13 ? 13 : clocks;
            u32 feedback = (gb.lfsr ^ (gb.lfsr >> 1)) & ((1 << k) - 1);

            gb.lfsr = (gb.lfsr >> k) | (feedback << (15 - k));
            clocks -= k;
        }
        return;
    }
    for(; clocks > 0; clocks--){
        u16 bit = (gb.lfsr ^ (gb.lfsr >> 1)) & 1;
        gb.lfsr = (gb.lfsr >> 1) | (bit << 14);
        gb.lfsr = (gb.lfsr & ~0x40) | (bit << 6);
    }
}

// One 65536 Hz sample of all four channels
static void GenerateSample(float *out){
    float outputL = 0;
    float outputR = 0;
    float level;

    if(--gb.sequencerSamples == 0){
        gb.sequencerSamples = SEQUENCER_SAMPLES;
        StepSequencer();
        UpdateMix();
    }

    gb.position[0] = (gb.position[0] + RunTimer(0, sMix.period[0])) & 7;
    gb.position[1] = (gb.position[1] + RunTimer(1, sMix.period[1])) & 7;
    gb.position[2] = (gb.position[2] + RunTimer(2, sMix.period[2])) & 31;
    // The noise only matters while it plays: triggering it resets the LFSR
    if(sMix.on[3])
        ClockNoise(RunNoiseTimer());

    if(sMix.on[0]){
        level = sMix.duty[0][gb.position[0]];
        if(sMix.leftMask & 1) outputL += level;
        if(sMix.rightMask & 1) outputR += level;
    }
    if(sMix.on[1]){
        level = sMix.duty[1][gb.position[1]];
        if(sMix.leftMask & 2) outputL += level;
        if(sMix.rightMask & 2) outputR += level;
    }
    if(sMix.on[2]){
        level = sMix.wave[gb.wave[gb.position[2]]];
        if(sMix.leftMask & 4) outputL += level;
        if(sMix.rightMask & 4) outputR += level;
    }
    if(sMix.on[3]){
        level = (gb.lfsr & 1) ? -sMix.noise : sMix.noise;
        if(sMix.leftMask & 8) outputL += level;
        if(sMix.rightMask & 8) outputR += level;
    }
    out[0] = outputL;
    out[1] = outputR;
}


#ifdef PSP_PROFILE
extern unsigned int gSoundProfile[6];
unsigned int sceKernelGetSystemTimeLow(void);
#endif

#ifdef CGB_BENCH
// Microseconds for COUNT samples of each part, for the test build's log
unsigned int sceKernelGetSystemTimeLow(void);
void cgb_bench(unsigned int *results){
    static float out[2];
    unsigned int t0, t1, t2, t3;

    UpdateMix();
    t0 = sceKernelGetSystemTimeLow();
    for(int i = 0; i < 100000; i++) GenerateSample(out);
    t1 = sceKernelGetSystemTimeLow();
    for(int i = 0; i < 100000; i++) RunTimer(0, 1000);
    t2 = sceKernelGetSystemTimeLow();
    for(int i = 0; i < 100000; i++) StepSequencer();
    t3 = sceKernelGetSystemTimeLow();
    results[0] = t1 - t0; results[1] = t2 - t1; results[2] = t3 - t2;
}
#endif

void cgb_audio_generate(u16 samplesPerFrame){
    float *outBuffer = gb.outBuffer;
    u32 need = 0;

    UpdateMix();

    // First every 65536 Hz sample the frame's output needs, then the filter
    // over them: done together, the filter's big coefficient table pushed the
    // channels' state out of the PSP's cache for every sample
    {
        u32 index = sResampleIndex, frac = sResampleFrac;

        for(u16 i = 0; i < samplesPerFrame; i++){
            need = index + TAPS / 2 + 1;
            frac += APU_RATE;
            while(frac >= sampleRate){
                frac -= sampleRate;
                index++;
            }
        }
    }
    if(need > HIGH_RATE_CAPACITY){
        // Drop the samples that have been filtered for the last time
        u32 first = sResampleIndex - (TAPS / 2 - 1);

        memmove(sHighRate, sHighRate[first], (sHighRateCount - first) * sizeof(sHighRate[0]));
        sHighRateCount -= first;
        sHighRateSoundEnd = sHighRateSoundEnd > first ? sHighRateSoundEnd - first : 0;
        sResampleIndex -= first;
        need -= first;
    }
#ifdef PSP_PROFILE
    unsigned int profStart = sceKernelGetSystemTimeLow();
#endif
    while(sHighRateCount < need){
        u32 bits[2];

        GenerateSample(sHighRate[sHighRateCount]);
        memcpy(bits, sHighRate[sHighRateCount], sizeof(bits));
        sHighRateCount++;
        if(bits[0] != 0 || bits[1] != 0)
            sHighRateSoundEnd = sHighRateCount;
    }
#ifdef PSP_PROFILE
    gSoundProfile[4] += sceKernelGetSystemTimeLow() - profStart;
#endif

    for(u16 i = 0; i < samplesPerFrame; i++, outBuffer += 2){
        u32 first = sResampleIndex - (TAPS / 2 - 1);
        const float *coefficients;
        float outputL = 0;
        float outputR = 0;

        if(first < sHighRateSoundEnd){
            // Four sums at a time, so the PSP's FPU doesn't wait on each one
            const float *in = sHighRate[first];
            float l0 = 0, l1 = 0, r0 = 0, r1 = 0;

            coefficients = sCoefficients[(sResampleFrac * PHASES + sampleRate / 2) / sampleRate];
            for(u32 tap = 0; tap < TAPS; tap += 2, in += 4){
                l0 += coefficients[tap] * in[0];
                r0 += coefficients[tap] * in[1];
                l1 += coefficients[tap + 1] * in[2];
                r1 += coefficients[tap + 1] * in[3];
            }
            outputL = l0 + l1;
            outputR = r0 + r1;
        }
        outBuffer[0] = outputL;
        outBuffer[1] = outputR;
        sResampleFrac += APU_RATE;
        while(sResampleFrac >= sampleRate){
            sResampleFrac -= sampleRate;
            sResampleIndex++;
        }
    }
}


float *cgb_get_buffer(){
    return gb.outBuffer;
}
#endif //PORTABLE
