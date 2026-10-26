#ifdef PORTABLE
#include "global.h"
#include <stdbool.h>
#include "platform/dma.h"
#include "platform/framedraw.h"

#define mosaicBGEffectX (REG_MOSAIC & 0xF)
#define mosaicBGEffectY ((REG_MOSAIC >> 4) & 0xF)
#define mosaicSpriteEffectX ((REG_MOSAIC >> 8) & 0xF)
#define mosaicSpriteEffectY ((REG_MOSAIC >> 12) & 0xF)
#define applyBGHorizontalMosaicEffect(x) (x - (x % (mosaicBGEffectX+1)))
#define applyBGVerticalMosaicEffect(y) (y - (y % (mosaicBGEffectY+1)))
#define applySpriteHorizontalMosaicEffect(x) (x - (x % (mosaicSpriteEffectX+1)))
#define applySpriteVerticalMosaicEffect(y) (y - (y % (mosaicSpriteEffectY+1)))

#define getAlphaBit(x) ((x >> 15) & 1)
#define getRedChannel(x) ((x >>  0) & 0x1F)
#define getGreenChannel(x) ((x >>  5) & 0x1F)
#define getBlueChannel(x) ((x >>  10) & 0x1F)
// A window over the whole line (the overworld keeps WIN0 over the whole
// screen) works like no windows with the layers and effects it lets through:
// sLineLayers (BG0-3, OBJ) and sLineEffects, set for each line
static uint8_t sLineLayers = 0x1F;
static bool sLineEffects = true;
#define isbgEnabled(x) (((REG_DISPCNT >> 8) & 0xF) & sLineLayers & (1 << (x)))

#define WINMASK_BG0    (1 << 0)
#define WINMASK_BG1    (1 << 1)
#define WINMASK_BG2    (1 << 2)
#define WINMASK_BG3    (1 << 3)
#define WINMASK_OBJ    (1 << 4)
#define WINMASK_CLR    (1 << 5)
#define WINMASK_WINOUT  (1 << 6)

extern void (*gIntrTable[])(void);

uint16_t *memsetu16(uint16_t *dst, uint16_t fill, size_t count);

struct scanlineData {
    // Only the enabled backgrounds' layers are drawn, and read, each line
    uint16_t layers[4][RENDER_WIDTH];
    // Clear at the start of each line: DrawScanline clears the ones that
    // spritePriorities says were drawn on
    uint16_t spriteLayers[4][RENDER_WIDTH];
    // Which sprite pixels blend with what's below them (semi-transparent
    // sprites, or sprites as first targets): done when the line is put
    // together. Only written and read on lines that have any (sLineSpriteBlend).
    uint8_t spriteBlend[4][RENDER_WIDTH];
    uint8_t spritePriorities;
    // The columns each priority's sprites were drawn in on this line, so
    // putting the line together and clearing only go over those
    uint16_t spriteFirstX[4], spriteEndX[4];
    uint16_t bgcnts[4];
    uint16_t winMask[RENDER_WIDTH];
    //priority bookkeeping
    char bgtoprio[4]; //background to priority
    char prioritySortedBgs[4][4];
    char prioritySortedBgsCount[4];
};

struct bgPriority {
    char priority;
    char subPriority;
};

// The columns of the line a text background draws on: all of them for a map
// at least as wide as the line, like the overworld's, and only the GBA's 240
// for a narrower one, which would just repeat itself in the margins. A screen
// can also name narrower maps that are made to repeat, like the intro's
// scrolling forest (gWideViewRepeatingBgs).
extern bool8 gWideViewMargins;
extern u8 gWideViewRepeatingBgs;
extern u8 gWideViewEndsAtEdgeBgs;
extern u8 gWideViewClampBgs;
extern u8 gWideViewNoWrapBgs;
extern u8 gWideViewLeftOnlyBgs;
extern u8 gWideViewNarrowBgs;
extern bool8 gWideViewFlashActive;
extern s16 gWideViewFlashSpans[2][DISPLAY_HEIGHT + 1][2];

static inline bool BgFillsMargins(int bgNum, unsigned int mapWidthInPixels)
{
    return (gWideViewMargins && mapWidthInPixels >= RENDER_WIDTH && !(gWideViewNarrowBgs & (1 << bgNum)))
        || ((gWideViewRepeatingBgs | gWideViewClampBgs | gWideViewNoWrapBgs) & (1 << bgNum));
}

// Whether it fills the left margin, or the right one (see gWideViewLeftOnlyBgs)
static inline bool BgFillsMargin(int bgNum, unsigned int mapWidthInPixels, int side)
{
    return BgFillsMargins(bgNum, mapWidthInPixels) || (side == 0 && (gWideViewLeftOnlyBgs & (1 << bgNum)));
}

static inline unsigned int BgFirstColumn(int bgNum, unsigned int mapWidthInPixels)
{
    return BgFillsMargin(bgNum, mapWidthInPixels, 0) ? 0 : RENDER_MARGIN;
}

static inline unsigned int BgEndColumn(int bgNum, unsigned int mapWidthInPixels)
{
    return BgFillsMargins(bgNum, mapWidthInPixels) ? RENDER_WIDTH : RENDER_MARGIN + DISPLAY_WIDTH;
}

// The columns a background draws on (see BgFirstColumn and BgEndColumn),
// narrowed for one that stops where its map would wrap round
// (gWideViewNoWrapBgs): on each side, the map seen at the screen's edge
// carries on only up to its end. The GBA's own columns are always drawn.
static inline void BgColumns(int bgNum, unsigned int mapWidthInPixels, unsigned int hoffs,
                             unsigned int *first, unsigned int *end)
{
    *first = BgFirstColumn(bgNum, mapWidthInPixels);
    *end = BgEndColumn(bgNum, mapWidthInPixels);
    if (gWideViewNoWrapBgs & (1 << bgNum))
    {
        int left = RENDER_MARGIN - (int)(hoffs % mapWidthInPixels);
        unsigned int right = RENDER_MARGIN + DISPLAY_WIDTH
                           + (mapWidthInPixels - 1 - (hoffs + DISPLAY_WIDTH - 1) % mapWidthInPixels);

        if (left > (int)*first)
            *first = left;
        if (right < *end)
            *end = right;
    }
}

static const uint16_t bgMapSizes[][2] =
{
    {32, 32},
    {64, 32},
    {32, 64},
    {64, 64},
};

// A 4bpp tile row's 8 pixels in the opposite order, for a flipped tile
static inline uint32_t FlipTileRow4(uint32_t row)
{
    row = ((row & 0x0F0F0F0F) << 4) | ((row >> 4) & 0x0F0F0F0F);
    row = ((row & 0x00FF00FF) << 8) | ((row >> 8) & 0x00FF00FF);
    return (row << 16) | (row >> 16);
}

// Whether RenderBGScanlineDirect can draw a background: a text one without
// horizontal mosaic
static bool CanDrawBGDirectly(uint16_t control)
{
    return !(control & BGCNT_MOSAIC) || mosaicBGEffectX == 0;
}

// A 256-color background's run of pixels (the title's logo), from row, a tile
// row's 8 palette entries: see RenderBGScanlineDirect
static inline void DrawRow8bpp(const uint8_t *row, bool flipX, unsigned int tileX, unsigned int x, unsigned int end,
                               uint16_t *line, const uint16_t *pal, int bgNum, const uint16_t *winMask, uint8_t *below)
{
    uint16_t bgBit = 1 << bgNum;

    if (!flipX && winMask == NULL && end - x == 8)
    {
        uint32_t lo = ((const uint32_t *)row)[0], hi = ((const uint32_t *)row)[1];

        // A whole row with no clear pixel
        if (((lo - 0x01010101) & ~lo & 0x80808080) == 0 && ((hi - 0x01010101) & ~hi & 0x80808080) == 0)
        {
            uint16_t *out = &line[x];

            out[0] = pal[lo & 0xFF] | 0x8000;
            out[1] = pal[(lo >> 8) & 0xFF] | 0x8000;
            out[2] = pal[(lo >> 16) & 0xFF] | 0x8000;
            out[3] = pal[lo >> 24] | 0x8000;
            out[4] = pal[hi & 0xFF] | 0x8000;
            out[5] = pal[(hi >> 8) & 0xFF] | 0x8000;
            out[6] = pal[(hi >> 16) & 0xFF] | 0x8000;
            out[7] = pal[hi >> 24] | 0x8000;
            if (below != NULL)
                memset(&below[x], bgNum, 8);
            return;
        }
    }
    for (; x < end; x++, tileX++)
    {
        unsigned int pixel = row[flipX ? 7 - tileX : tileX];

        // Color 0 is clear in 256-color tiles too
        if (pixel != 0 && (winMask == NULL || (winMask[x] & bgBit)))
        {
            line[x] = pal[pixel] | 0x8000;
            if (below != NULL)
                below[x] = bgNum;
        }
    }
}

// Draws a text background's opaque pixels straight onto the line, where winMask
// (if not NULL) has the background's bit. That's what RenderBGScanline and
// copying its layer onto the line do, for a background that isn't blended.
// below (if not NULL) gets the background's number where it drew, for
// blending another layer over it.
static void RenderBGScanlineDirect(int bgNum, uint16_t control, uint16_t hoffs, uint16_t voffs, int lineNum, uint16_t *line, const uint16_t *winMask,
                                   uint8_t *below)
{
    unsigned int charBaseBlock = (control >> 2) & 3;
    unsigned int screenBaseBlock = (control >> 8) & 0x1F;
    unsigned int mapWidthInPixels = bgMapSizes[control >> 14][0] * 8;
    unsigned int mapHeightInPixels = bgMapSizes[control >> 14][1] * 8;
    uint8_t *bgtiles = (uint8_t *)BG_CHAR_ADDR(charBaseBlock);
    uint16_t *pal = (uint16_t *)PLTT;
    uint16_t *bgmapRow = (uint16_t *)BG_SCREEN_ADDR(screenBaseBlock);
    uint16_t bgBit = 1 << bgNum;
    unsigned int yy;

    if (control & BGCNT_MOSAIC)
        lineNum = applyBGVerticalMosaicEffect(lineNum);
    hoffs &= 0x1FF;
    voffs &= 0x1FF;

    yy = (lineNum + voffs) & 0x1FF;
    if (yy > 255 && mapHeightInPixels > 256)
        bgmapRow += (mapWidthInPixels > 256) ? 0x800 : 0x400;
    yy &= 0xFF;
    bgmapRow += (yy / 8) * 32;

    unsigned int firstColumn, lineEnd;

    BgColumns(bgNum, mapWidthInPixels, hoffs, &firstColumn, &lineEnd);
    for (unsigned int x = firstColumn; x < lineEnd;)
    {
        unsigned int xx = (x - RENDER_MARGIN + hoffs) & 0x1FF;
        uint16_t *bgmap = bgmapRow;

        if (xx > 255 && mapWidthInPixels > 256)
            bgmap += 0x400;
        xx &= 0xFF;

        uint16_t entry = bgmap[xx / 8];
        unsigned int tileY = (entry & (1 << 11)) ? 7 - (yy % 8) : yy % 8;
        unsigned int tileX = xx % 8;
        unsigned int run = 8 - tileX;

        if (run > lineEnd - x)
            run = lineEnd - x;
        if (control & BGCNT_256COLOR)
        {
            const uint8_t *row = bgtiles + (uint16_t)((entry & 0x3FF) * 64) + tileY * 8;

            // A clear row draws nothing
            if ((((const uint32_t *)row)[0] | ((const uint32_t *)row)[1]) != 0)
                DrawRow8bpp(row, entry & (1 << 10), tileX, x, x + run, line, pal, bgNum, winMask, below);
            x += run;
            continue;
        }

        uint32_t pixels = *(uint32_t *)(bgtiles + (uint16_t)((entry & 0x3FF) * 32) + tileY * 4);

        if (pixels == 0)
        {
            x += run;
            continue;
        }
        if (entry & (1 << 10))
            pixels = FlipTileRow4(pixels);
        pixels >>= tileX * 4;

        const uint16_t *subPal = pal + 16 * ((entry >> 12) & 0xF);
        unsigned int end = x + run;

        if (winMask == NULL)
        {
            // A whole row with no clear pixel (most of the overworld's bottom
            // layer): no need to check each one
            if (run == 8 && ((pixels - 0x11111111) & ~pixels & 0x88888888) == 0)
            {
                uint16_t *out = &line[x];

                out[0] = subPal[pixels & 0xF] | 0x8000;
                out[1] = subPal[(pixels >> 4) & 0xF] | 0x8000;
                out[2] = subPal[(pixels >> 8) & 0xF] | 0x8000;
                out[3] = subPal[(pixels >> 12) & 0xF] | 0x8000;
                out[4] = subPal[(pixels >> 16) & 0xF] | 0x8000;
                out[5] = subPal[(pixels >> 20) & 0xF] | 0x8000;
                out[6] = subPal[(pixels >> 24) & 0xF] | 0x8000;
                out[7] = subPal[pixels >> 28] | 0x8000;
                if (below != NULL)
                    memset(&below[x], bgNum, 8);
                x = end;
                continue;
            }
            for (; x < end; x++, pixels >>= 4)
            {
                if (pixels & 0xF)
                {
                    line[x] = subPal[pixels & 0xF] | 0x8000;
                    if (below != NULL)
                        below[x] = bgNum;
                }
            }
        }
        else
        {
            for (; x < end; x++, pixels >>= 4)
            {
                if ((pixels & 0xF) && (winMask[x] & bgBit))
                {
                    line[x] = subPal[pixels & 0xF] | 0x8000;
                    if (below != NULL)
                        below[x] = bgNum;
                }
            }
        }
    }
}

// Writes every pixel of the line, 0 where the layer is clear
static void RenderBGScanline(int bgNum, uint16_t control, uint16_t hoffs, uint16_t voffs, int lineNum, uint16_t *line)
{
    unsigned int charBaseBlock = (control >> 2) & 3;
    unsigned int screenBaseBlock = (control >> 8) & 0x1F;
    unsigned int bitsPerPixel = ((control >> 7) & 1) ? 8 : 4;
    unsigned int mapWidth = bgMapSizes[control >> 14][0];
    unsigned int mapHeight = bgMapSizes[control >> 14][1];
    unsigned int mapWidthInPixels = mapWidth * 8;
    unsigned int mapHeightInPixels = mapHeight * 8;

    uint8_t *bgtiles = (uint8_t *)BG_CHAR_ADDR(charBaseBlock);
    uint16_t *pal = (uint16_t *)PLTT;
     
    if (control & BGCNT_MOSAIC)
        lineNum = applyBGVerticalMosaicEffect(lineNum);

    hoffs &= 0x1FF;
    voffs &= 0x1FF;

    // Without horizontal mosaic, whole runs of pixels come from the same map
    // entry, so it only needs looking up once per tile instead of for every
    // pixel. The overworld keeps mosaic on for BG1-3 with a size of 0, which
    // changes nothing.
    if ((!(control & BGCNT_MOSAIC) || mosaicBGEffectX == 0) && !((gWideViewClampBgs | gWideViewLeftOnlyBgs) & (1 << bgNum)))
    {
        unsigned int yy = (lineNum + voffs) & 0x1FF;
        uint16_t *bgmapRow = (uint16_t *)BG_SCREEN_ADDR(screenBaseBlock);

        if (yy > 255 && mapHeightInPixels > 256)
            bgmapRow += (mapWidthInPixels > 256) ? 0x800 : 0x400;
        yy &= 0xFF;
        bgmapRow += (yy / 8) * 32;

        unsigned int firstColumn, lineEnd;

        BgColumns(bgNum, mapWidthInPixels, hoffs, &firstColumn, &lineEnd);
        // A narrow map leaves the margins clear
        if (firstColumn != 0)
            memset(line, 0, firstColumn * sizeof(line[0]));
        if (lineEnd < RENDER_WIDTH)
            memset(&line[lineEnd], 0, (RENDER_WIDTH - lineEnd) * sizeof(line[0]));
        for (unsigned int x = firstColumn; x < lineEnd;)
        {
            unsigned int xx = (x - RENDER_MARGIN + hoffs) & 0x1FF;
            uint16_t *bgmap = bgmapRow;

            if (xx > 255 && mapWidthInPixels > 256)
                bgmap += 0x400;
            xx &= 0xFF;

            uint16_t entry = bgmap[xx / 8];
            unsigned int tileY = (entry & (1 << 11)) ? 7 - (yy % 8) : yy % 8;
            uint8_t *row = bgtiles + (uint16_t)((entry & 0x3FF) * (bitsPerPixel * 8)) + tileY * bitsPerPixel;
            bool flipX = entry & (1 << 10);
            unsigned int run = 8 - (xx % 8);

            if (run > lineEnd - x)
                run = lineEnd - x;
            if (bitsPerPixel == 4)
            {
                uint16_t *subPal = pal + 16 * ((entry >> 12) & 0xF);
                // The row's 8 pixels, the first in the lowest bits. Rows are
                // 4 bytes and so aligned, and a clear row draws nothing.
                uint32_t pixels = *(uint32_t *)row;

                if (pixels == 0)
                {
                    memset(&line[x], 0, run * sizeof(line[0]));
                    x += run;
                    continue;
                }
                if (flipX)
                    pixels = FlipTileRow4(pixels);
                for (unsigned int tileX = xx % 8, end = tileX + run; tileX < end; tileX++, x++)
                {
                    uint8_t pixel = (pixels >> (tileX * 4)) & 0xF;

                    line[x] = pixel != 0 ? (subPal[pixel] | 0x8000) : 0;
                }
            }
            else
            {
                for (unsigned int tileX = xx % 8, end = tileX + run; tileX < end; tileX++, x++)
                {
                    uint8_t pixel = row[flipX ? 7 - tileX : tileX];

                    // Color 0 is clear in 256-color tiles too
                    line[x] = pixel != 0 ? (pal[pixel] | 0x8000) : 0;
                }
            }
        }
        return;
    }

    // This way only draws the pixels that aren't clear
    memset(line, 0, RENDER_WIDTH * sizeof(line[0]));
    for (unsigned int x = BgFirstColumn(bgNum, mapWidthInPixels); x < BgEndColumn(bgNum, mapWidthInPixels); x++)
    {
        uint16_t *bgmap = (uint16_t *)BG_SCREEN_ADDR(screenBaseBlock);
        // adjust for scroll, in the game's columns (which start left of 0
        // in the margin), with the mosaic grid anchored on the GBA's 0
        int gx = (int)x - RENDER_MARGIN;
        unsigned int xx;
        if (control & BGCNT_MOSAIC)
        {
            int size = mosaicBGEffectX + 1;

            xx = (gx - (((gx % size) + size) % size) + hoffs) & 0x1FF;
        }
        else if (gWideViewClampBgs & (1 << bgNum))
        {
            // Past its art's edges the background carries on its first or
            // last column, wherever it's scrolled to (see gWideViewClampBgs)
            int col = gx + (hoffs > 255 ? (int)hoffs - 512 : (int)hoffs);

            xx = (col < 0 ? 0 : col > DISPLAY_WIDTH - 1 ? DISPLAY_WIDTH - 1 : col) & 0x1FF;
        }
        else if ((gWideViewLeftOnlyBgs & (1 << bgNum)) && gx < -16)
            // Past the 16 pixels of map left of the screen, their first column
            // carries on (see gWideViewLeftOnlyBgs)
            xx = (-16 + hoffs) & 0x1FF;
        else
            xx = (gx + hoffs) & 0x1FF;
        // In the margins, only where the map seen at the screen's edge
        // carries on without wrapping round (see gWideViewNoWrapBgs)
        if (gWideViewNoWrapBgs & (1 << bgNum))
        {
            if (gx < 0 && (int)(hoffs % mapWidthInPixels) + gx < 0)
                continue;
            if (gx >= DISPLAY_WIDTH
             && (hoffs + DISPLAY_WIDTH - 1) % mapWidthInPixels + (unsigned int)(gx - (DISPLAY_WIDTH - 1)) >= mapWidthInPixels)
                continue;
        }
        
        unsigned int yy = (lineNum + voffs) & 0x1FF;
        
        //if x or y go above 255 pixels it goes to the next screen base which are 0x400 WORDs long
        if (xx > 255 && mapWidthInPixels > 256) {
            bgmap += 0x400;
        }
        
        if (yy > 255 && mapHeightInPixels > 256) {
            //the width check is for 512x512 mode support, it jumps by two screen bases instead
            bgmap += (mapWidthInPixels > 256) ? 0x800 : 0x400;
        }
        
        //maximum width for bgtile block is 256
        xx &= 0xFF;
        yy &= 0xFF;

        unsigned int mapX = xx / 8;
        unsigned int mapY = yy / 8;
        uint16_t entry = bgmap[mapY * 32 + mapX];

        unsigned int tileNum = entry & 0x3FF;
        unsigned int paletteNum = (entry >> 12) & 0xF;
        
        unsigned int tileX = xx % 8;
        unsigned int tileY = yy % 8;

        // Flip if necessary
        if (entry & (1 << 10))
            tileX = 7 - tileX;
        if (entry & (1 << 11))
            tileY = 7 - tileY;

        uint16_t tileLoc = tileNum * (bitsPerPixel * 8);
        uint16_t tileLocY = tileY * bitsPerPixel;
        uint16_t tileLocX = tileX;
        if (bitsPerPixel == 4)
            tileLocX /= 2;

        uint8_t pixel = bgtiles[tileLoc + tileLocY + tileLocX];

        if (bitsPerPixel == 4) {
            if (tileX & 1)
                pixel >>= 4;
            else
                pixel &= 0xF;

            if (pixel != 0)
                line[x] = pal[16 * paletteNum + pixel] | 0x8000;
        }
        else if (pixel != 0) {
            // Color 0 is clear in 256-color tiles too
            line[x] = pal[pixel] | 0x8000;
        }
    }
}

static inline uint32_t getBgX(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2X;
    }
    return REG_BG3X;
}

static inline uint32_t getBgY(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2Y;
    }
    return REG_BG3Y;
}

static inline uint16_t getBgPA(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PA;
    }
    return REG_BG3PA;
}

static inline uint16_t getBgPB(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PB;
    }
    return REG_BG3PB;
}

static inline uint16_t getBgPC(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PC;
    }
    return REG_BG3PC;
}

static inline uint16_t getBgPD(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PD;
    }
    return REG_BG3PD;
}

static void RenderRotScaleBGScanline(int bgNum, uint16_t control, uint16_t x, uint16_t y, int lineNum, uint16_t *line)
{
    vBgCnt *bgcnt = (vBgCnt *)&control;
    unsigned int charBaseBlock = bgcnt->charBaseBlock;
    unsigned int screenBaseBlock = bgcnt->screenBaseBlock;

    uint8_t *bgtiles = (uint8_t *)(VRAM_ + charBaseBlock * 0x4000);
    uint8_t *bgmap = (uint8_t *)(VRAM_ + screenBaseBlock * 0x800);
    uint16_t *pal = (uint16_t *)PLTT;

    if (control & BGCNT_MOSAIC)
        lineNum = applyBGVerticalMosaicEffect(lineNum);
    

    s16 pa = getBgPA(bgNum);
    s16 pb = getBgPB(bgNum);
    s16 pc = getBgPC(bgNum);
    s16 pd = getBgPD(bgNum);

    int sizeX = 128;
    int sizeY = 128;

    switch (bgcnt->screenSize)
    {
    case 0:
        break;
    case 1:
        sizeX = sizeY = 256;
        break;
    case 2:
        sizeX = sizeY = 512;
        break;
    case 3:
        sizeX = sizeY = 1024;
        break;
    }

    int maskX = sizeX - 1;
    int maskY = sizeY - 1;

    int yshift = ((control >> 14) & 3) + 4;

    /*int dx = pa & 0x7FFF;
    if (pa & 0x8000)
        dx |= 0xFFFF8000;
    int dmx = pb & 0x7FFF;
    if (pb & 0x8000)
        dmx |= 0xFFFF8000;
    int dy = pc & 0x7FFF;
    if (pc & 0x8000)
        dy |= 0xFFFF8000;
    int dmy = pd & 0x7FFF;
    if (pd & 0x8000)
        dmy |= 0xFFFF8000;*/

    s32 currentX = getBgX(bgNum);
    s32 currentY = getBgY(bgNum);
    //sign extend 28 bit number
    currentX = ((currentX & (1 << 27)) ? currentX | 0xF0000000 : currentX);
    currentY = ((currentY & (1 << 27)) ? currentY | 0xF0000000 : currentY);

    currentX += lineNum * pb;
    currentY += lineNum * pd;

    // The walk starts at the line's first column, RENDER_MARGIN columns left
    // of the GBA's. A background that wraps would repeat itself out there,
    // so it only draws the GBA's columns.
    int firstColumn = bgcnt->areaOverflowMode ? RENDER_MARGIN : 0;
    int endColumn = bgcnt->areaOverflowMode ? RENDER_MARGIN + DISPLAY_WIDTH : RENDER_WIDTH;
    int realX = currentX + (firstColumn - RENDER_MARGIN) * pa;
    int realY = currentY + (firstColumn - RENDER_MARGIN) * pc;

    memset(line, 0, RENDER_WIDTH * sizeof(line[0]));
    if (bgcnt->areaOverflowMode)
    {
        for (int x = firstColumn; x < endColumn; x++)
        {
            int xxx = (realX >> 8) & maskX;
            int yyy = (realY >> 8) & maskY;

            int tile = bgmap[(xxx >> 3) + ((yyy >> 3) << yshift)];

            int tileX = xxx & 7;
            int tileY = yyy & 7;

            uint8_t pixel = bgtiles[(tile << 6) + (tileY << 3) + tileX];

            if (pixel != 0) {
                line[x] = pal[pixel] | 0x8000;
            }

            realX += pa;
            realY += pc;
        }
    }
    else
    {
        for (int x = firstColumn; x < endColumn; x++)
        {
            int xxx = (realX >> 8);
            int yyy = (realY >> 8);

            if (xxx < 0 || yyy < 0 || xxx >= sizeX || yyy >= sizeY)
            {
                //line[x] = 0x80000000;
            }
            else
            {
                int tile = bgmap[(xxx >> 3) + ((yyy >> 3) << yshift)];

                int tileX = xxx & 7;
                int tileY = yyy & 7;

                uint8_t pixel = bgtiles[(tile << 6) + (tileY << 3) + tileX];

                if (pixel != 0) {
                    line[x] = pal[pixel] | 0x8000;
                }
            }
            realX += pa;
            realY += pc;
        }
    }
    //the only way i could figure out how to get accurate mosaic on affine bgs 
    //luckily i dont think pokemon emerald uses mosaic on affine bgs
    if (control & BGCNT_MOSAIC && mosaicBGEffectX > 0)
    {
        // The mosaic grid is anchored on the GBA's column 0
        int size = mosaicBGEffectX + 1;

        for (int x = 0; x < RENDER_WIDTH; x++)
        {
            int gx = x - RENDER_MARGIN;
            int from = gx - (((gx % size) + size) % size) + RENDER_MARGIN;

            line[x] = line[from < 0 ? 0 : from];
        }
    }
}

const u8 spriteSizes[][2] =
{
    {8, 16},
    {8, 32},
    {16, 32},
    {32, 64},
};

static uint16_t alphaBlendColor(uint16_t targetA, uint16_t targetB)
{
    unsigned int eva = REG_BLDALPHA & 0x1F;
    unsigned int evb = (REG_BLDALPHA >> 8) & 0x1F;
    // shift right by 4 = division by 16
    unsigned int r = ((getRedChannel(targetA) * eva) + (getRedChannel(targetB) * evb)) >> 4;
    unsigned int g = ((getGreenChannel(targetA) * eva) + (getGreenChannel(targetB) * evb)) >> 4;
    unsigned int b = ((getBlueChannel(targetA) * eva) + (getBlueChannel(targetB) * evb)) >> 4;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;

     return r | (g << 5) | (b << 10) | (1 << 15);
}

// alphaBlendColor with the line's coefficients worked out once (each 0 to 16;
// the GBA takes 17 to 31 as 16), blending the three channels at once: spread
// out, each has room for the sum of two products
static inline uint16_t BlendColors(uint16_t a, uint16_t b, unsigned int eva, unsigned int evb)
{
    uint32_t sa = (a | ((uint32_t)a << 16)) & 0x03E07C1F;
    uint32_t sb = (b | ((uint32_t)b << 16)) & 0x03E07C1F;
    uint32_t sum = (sa * eva + sb * evb) >> 4;
    uint32_t over = sum & ((1u << 5) | (1u << 15) | (1u << 26));

    sum = (sum & 0x03E07C1F) | ((over >> 5) * 31);
    return (sum & 0x7C1F) | ((sum >> 16) & 0x03E0) | 0x8000;
}

static uint16_t alphaBrightnessIncrease(uint16_t targetA)
{
    unsigned int evy = (REG_BLDY & 0x1F);
    unsigned int r = getRedChannel(targetA) + (31 - getRedChannel(targetA)) * evy / 16;
    unsigned int g = getGreenChannel(targetA) + (31 - getGreenChannel(targetA)) * evy / 16;
    unsigned int b = getBlueChannel(targetA) + (31 - getBlueChannel(targetA)) * evy / 16;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;
    
     return r | (g << 5) | (b << 10) | (1 << 15);
}

static uint16_t alphaBrightnessDecrease(uint16_t targetA)
{
    unsigned int evy = (REG_BLDY & 0x1F);
    unsigned int r = getRedChannel(targetA) - getRedChannel(targetA) * evy / 16;
    unsigned int g = getGreenChannel(targetA) - getGreenChannel(targetA) * evy / 16;
    unsigned int b = getBlueChannel(targetA) - getBlueChannel(targetA) * evy / 16;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;
    
     return r | (g << 5) | (b << 10) | (1 << 15);
}

//outputs the blended pixel in colorOutput, the prxxx are the bg priority and subpriority, pixelpos is pixel offset in scanline
#ifdef PSP_PROFILE
// Pixels blended, layers looked at for their second target, lines with
// windows, lines drawn (for the speed log)
unsigned int gBlendStats[4];
#define BLEND_STAT(n, v) (gBlendStats[n] += (v))
#else
#define BLEND_STAT(n, v) ((void)0)
#endif

static bool alphaBlendSelectTargetB(struct scanlineData* scanline, uint16_t* colorOutput, char prnum, char prsub, int pixelpos, bool spriteBlendEnabled)
{   
    BLEND_STAT(0, 1);
    //iterate trough every possible bg to blend with, starting from specified priorities from arguments
    for (unsigned int blndprnum = prnum; blndprnum <= 3; blndprnum++)
    {
        //check if sprite is available to blend with, if sprite blending is enabled
        if (spriteBlendEnabled == true && getAlphaBit(scanline->spriteLayers[blndprnum][pixelpos]) == 1)
        {
            *colorOutput = scanline->spriteLayers[blndprnum][pixelpos];
            return true;
        }
            
        for (unsigned int blndprsub = prsub; blndprsub < scanline->prioritySortedBgsCount[blndprnum]; blndprsub++)
        {
            char currLayer = scanline->prioritySortedBgs[blndprnum][blndprsub];
            BLEND_STAT(1, 1);
            if (getAlphaBit( scanline->layers[currLayer][pixelpos] ) == 1 && REG_BLDCNT & ( 1 << (8 + currLayer)) && isbgEnabled(currLayer))
            {
                *colorOutput = scanline->layers[currLayer][pixelpos];
                return true;
            }
            //if we hit a non target layer we should bail
            if ( getAlphaBit( scanline->layers[currLayer][pixelpos] ) == 1 && isbgEnabled(currLayer) && prnum != blndprnum )
            {
                return false;
            }
        }
        prsub = 0; //start from zero in the next iteration
    }
    //no background got hit, check if backdrop is enabled and return it if enabled otherwise fail
    if (REG_BLDCNT & BLDCNT_TGT2_BD)
    {
        *colorOutput = *(uint16_t*)PLTT;
        return true;
    }
    else
    {
        return false;
    }
}

// Window registers only reach the GBA's 240 columns, so a window meant to
// cover the whole screen (the overworld's WIN0H of 0x00FF) can't say it covers
// the margins too. On lines with a background wide enough to fill the margins,
// an edge on the screen's left or right edge carries on to the line's edge.
// Edges inside the screen (a cave's flash circle, battle transitions) stay
// where they are. Borrowed from pokeemerald-dualscreen's widescreen renderer.
// (idea from Goldoire/pokeemerald-dualscreen)
static bool sWinExtendMargins;
// Whether a sprite on this line blends (see spriteBlend)
static bool sLineSpriteBlend;
#ifdef PSP_TEST
// Test builds can draw each line the old way too (blending by searching the
// layers below) and count the pixels that come out different: the test
// input's RENDERAB
bool gTestRenderAB;
unsigned int gTestRenderABLines, gTestRenderABPixels, gTestRenderABFirst;
static bool sTestOldBlend;
#define OLD_BLEND sTestOldBlend
#else
#define OLD_BLEND false
#endif
// Whether a sprite covers the GBA screen's left or right column on this line,
// so ExtendEdges carries on the background under it rather than the sprite
static bool sSpriteAtEdge[2];
// Whether the line's left and right margins were filled by carrying on the
// GBA's edge columns (see ExtendEdges), so DrawLine leaves them
static bool sExtendedLeft, sExtendedRight;

static void FillColumns(uint16_t *mask, int from, int to, uint16_t value)
{
    from += RENDER_MARGIN;
    to += RENDER_MARGIN;
    if (from < 0)
        from = 0;
    if (to > RENDER_WIDTH)
        to = RENDER_WIDTH;
    for (int x = from; x < to; x++)
        mask[x] = value;
}

//fills the pixels a window covers horizontally, taking account of WIN wraparound
static void FillWindowSpan(uint16_t *mask, u16 left, u16 right, uint16_t value)
{
    int lineStart = sWinExtendMargins ? -RENDER_MARGIN : 0;
    int lineEnd = sWinExtendMargins ? DISPLAY_WIDTH + RENDER_MARGIN : DISPLAY_WIDTH;

    if (right > DISPLAY_WIDTH)
        right = DISPLAY_WIDTH;
    // An empty window, like WIN_RANGE(0, 0) at the start of a barn door wipe,
    // covers nothing, even at the screen's edge
    if (left == right)
        return;
    if (left > right)
    {
        FillColumns(mask, lineStart, right, value);
        FillColumns(mask, left, lineEnd, value);
    }
    else
    {
        FillColumns(mask, left == 0 ? lineStart : left, right == DISPLAY_WIDTH ? lineEnd : right, value);
    }
}

// The sprites in OAM, decoded. OAM doesn't change while a frame is drawn, so
// this is only done again when it does.
struct DecodedSprite
{
    struct OamData *oam;
    unsigned int width;
    unsigned int height;
    int half_width;
    int half_height;
    int32_t x;
    int32_t y;
    int16_t matrix[2][2];
    bool isAffine;
};

static struct DecodedSprite sSprites[128];
static int sNumSprites;
// Whether any of them is semi-transparent, which blends it with what's below
static bool sAnySemiTransparent;
// The ones on each line, in the order they're drawn
static uint8_t sLineSprites[DISPLAY_HEIGHT][128];
static uint8_t sLineSpriteCount[DISPLAY_HEIGHT];
static uint8_t sDecodedOam[OAM_SIZE];
static bool sSpritesDecoded;

static void DecodeSprites(void)
{
    int i;

    sNumSprites = 0;
    sAnySemiTransparent = false;
    for (i = 127; i >= 0; i--)
    {
        struct OamData *oam = &((struct OamData *)OAM)[i];
        struct DecodedSprite *sprite = &sSprites[sNumSprites];
        unsigned int width;
        unsigned int height;

        bool isAffine  = oam->affineMode & 1;
        bool doubleSizeOrDisabled = (oam->affineMode >> 1) & 1;

        if (!(isAffine) && doubleSizeOrDisabled) // disable for non-affine
        {
            continue;
        }

        if (oam->shape == 0)
        {
            width = (1 << oam->size) * 8;
            height = (1 << oam->size) * 8;
        }
        else if (oam->shape == 1) // wide
        {
            width = spriteSizes[oam->size][1];
            height = spriteSizes[oam->size][0];
        }
        else if (oam->shape == 2) // tall
        {
            width = spriteSizes[oam->size][0];
            height = spriteSizes[oam->size][1];
        }
        else
        {
            continue; // prohibited, do not draw
        }

        int half_width = width / 2;
        int half_height = height / 2;

        int32_t x = oam->x;
        int32_t y = oam->y;

        // OAM's x is 9 bits. 384 and up are sprites coming in from the left;
        // 240 to 383 are off the right, unless they're in the right margin.
        // (from pokeemerald-dualscreen)
        if (x >= 512 - 128)
            x -= 512;
        if (y >= DISPLAY_HEIGHT)
            y -= 256;

        if (isAffine)
        {
            u8 matrixNum = oam->matrixNum * 4;

            struct OamData *oam1 = &((struct OamData *)OAM)[matrixNum];
            struct OamData *oam2 = &((struct OamData *)OAM)[matrixNum + 1];
            struct OamData *oam3 = &((struct OamData *)OAM)[matrixNum + 2];
            struct OamData *oam4 = &((struct OamData *)OAM)[matrixNum + 3];

            sprite->matrix[0][0] = oam1->affineParam;
            sprite->matrix[0][1] = oam2->affineParam;
            sprite->matrix[1][0] = oam3->affineParam;
            sprite->matrix[1][1] = oam4->affineParam;

            if (doubleSizeOrDisabled) // double size for affine
            {
                half_width *= 2;
                half_height *= 2;
            }
        }
        else
        {
            // Identity
            sprite->matrix[0][0] = 0x100;
            sprite->matrix[0][1] = 0;
            sprite->matrix[1][0] = 0;
            sprite->matrix[1][1] = 0x100;
        }

        sprite->oam = oam;
        sprite->width = width;
        sprite->height = height;
        sprite->half_width = half_width;
        sprite->half_height = half_height;
        sprite->x = x + half_width;
        sprite->y = y + half_height;
        sprite->isAffine = isAffine;
        if (oam->objMode == 1)
            sAnySemiTransparent = true;
        sNumSprites++;
    }
    memset(sLineSpriteCount, 0, sizeof(sLineSpriteCount));
    for (i = 0; i < sNumSprites; i++)
    {
        int top = sSprites[i].y - sSprites[i].half_height;
        int bottom = sSprites[i].y + sSprites[i].half_height;

        if (top < 0)
            top = 0;
        if (bottom > DISPLAY_HEIGHT)
            bottom = DISPLAY_HEIGHT;
        for (int line = top; line < bottom; line++)
            sLineSprites[line][sLineSpriteCount[line]++] = i;
    }
    memcpy(sDecodedOam, OAM, OAM_SIZE);
    sSpritesDecoded = true;
}

// OAM only changes between frames, so it's compared once a frame (at line 0)
static bool sCheckOam = true;
static void UpdateSprites(void)
{
    if (!sCheckOam)
        return;
    sCheckOam = false;
    if (!sSpritesDecoded || memcmp(sDecodedOam, OAM, OAM_SIZE) != 0)
        DecodeSprites();
}

#ifdef PSP_PROFILE
// Sprite pixels looked at: plain, turned or scaled, semi-transparent, window
unsigned int gSpriteStats[4];
#endif

// Parts of this code heavily borrowed from NanoboyAdvance.
static void DrawSprites(struct scanlineData* scanline, uint16_t vcount, bool windowsEnabled)
{
    void *objtiles = VRAM_ + 0x10000;
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
    bool winShouldBlendPixel = true;

    if (!(REG_DISPCNT & (1 << 6)))
    {
        puts("2-D OBJ Character mapping not supported.");
    }

    UpdateSprites();
    if (vcount >= DISPLAY_HEIGHT)
        return;

    for (int n = 0; n < sLineSpriteCount[vcount]; n++)
    {
        const struct DecodedSprite *sprite = &sSprites[sLineSprites[vcount][n]];
        struct OamData *oam = sprite->oam;
        unsigned int width = sprite->width;
        unsigned int height = sprite->height;
        int half_width = sprite->half_width;
        int half_height = sprite->half_height;
        int32_t x = sprite->x;
        int32_t y = sprite->y;
        const int16_t (*matrix)[2] = sprite->matrix;
        bool isAffine = sprite->isAffine;
        bool isSemiTransparent = (oam->objMode == 1);
        bool isObjWin = (oam->objMode == 2);
        uint16_t *pixels = scanline->spriteLayers[oam->priority];

        // Does this sprite actually draw on this scanline?
        if (vcount >= (y - half_height) && vcount < (y + half_height))
        {
            int local_y = (oam->mosaic == 1) ? applySpriteVerticalMosaicEffect(vcount) - y : vcount - y;
            bool flipX  = !isAffine && ((oam->matrixNum >> 3) & 1);
            bool flipY  = !isAffine && ((oam->matrixNum >> 4) & 1);
            bool is8BPP  = oam->bpp & 1;
            // Most sprites aren't turned or scaled (a battler is often an affine
            // sprite with no change): then the texture is the screen less an
            // offset, with no matrix sums for each pixel
            bool identity = matrix[0][0] == 0x100 && matrix[0][1] == 0 && matrix[1][0] == 0
                         && matrix[1][1] == 0x100 && oam->mosaic != 1;
            int firstX = -half_width, lastX = half_width;
            // only the columns on the line
            int minGlobalX = sWinExtendMargins ? -RENDER_MARGIN : 0;
            int endGlobalX = sWinExtendMargins ? DISPLAY_WIDTH + RENDER_MARGIN : DISPLAY_WIDTH;

            if (firstX < minGlobalX - x)
                firstX = minGlobalX - x;
            if (lastX > endGlobalX - 1 - x)
                lastX = endGlobalX - 1 - x;
            if (identity)
            {
                int texRow = local_y + (int)(height / 2);

                // only the texture's own columns, and nothing if the line misses it
                if (texRow < 0 || texRow >= (int)height)
                    continue;
                if (firstX < -(int)(width / 2))
                    firstX = -(int)(width / 2);
                if (lastX > (int)(width / 2) - 1)
                    lastX = (int)(width / 2) - 1;
            }

#ifdef PSP_PROFILE
            if (lastX >= firstX)
                gSpriteStats[isObjWin ? 3 : isSemiTransparent ? 2 : identity ? 0 : 1] += lastX - firstX + 1;
#endif
            // A sprite window's shape, like Encore's spotlight: where it isn't
            // clear, outside the other windows, the line takes the sprite
            // window's settings. The texture's coordinates step along as for
            // the turned and scaled sprites below.
            if (isObjWin && oam->mosaic != 1)
            {
                const uint8_t *tiles = (const uint8_t *)objtiles;
                unsigned int rowTiles = (REG_DISPCNT & 0x40) ? width / 8 : 16;
                unsigned int tileNum = oam->tileNum;
                uint16_t inside = (REG_WINOUT >> 8) & 0x3F;
                int32_t pa = matrix[0][0], pc = matrix[1][0];
                int32_t accX = pa * firstX + matrix[0][1] * local_y;
                int32_t accY = pc * firstX + matrix[1][1] * local_y;

                for (int local_x = firstX; local_x <= lastX; local_x++, accX += pa, accY += pc)
                {
                    unsigned int texX = (unsigned int)((accX >> 8) + (int)(width / 2));
                    unsigned int texY = (unsigned int)((accY >> 8) + (int)(height / 2));
                    unsigned int pixel;
                    int bx = x + local_x + RENDER_MARGIN;

                    if (texX >= width || texY >= height || !(scanline->winMask[bx] & WINMASK_WINOUT))
                        continue;
                    if (flipX)
                        texX = width - 1 - texX;
                    if (flipY)
                        texY = height - 1 - texY;
                    if (!is8BPP)
                        pixel = (tiles[((texY / 8) * rowTiles + texX / 8 + tileNum) * 32 + (texY % 8) * 4 + (texX % 8) / 2]
                                 >> ((texX & 1) * 4)) & 0xF;
                    else
                        pixel = tiles[(((texY / 8) * rowTiles + texX / 8) * 2 + tileNum) * 32 + (texY % 8) * 8 + texX % 8];
                    if (pixel != 0)
                        scanline->winMask[bx] = inside;
                }
                continue;
            }
            // A plain sprite that nothing blends (most of them): its row of
            // texels is copied straight, without the checks below for each pixel.
            // The windows don't matter here: whether one shows the sprite is
            // looked at when the line is put together. (With windows on the
            // line, sLineEffects is true, so a sprite that's a first target
            // for an effect, which the windows may allow, goes the slow way.)
            if (identity && !isObjWin && !isSemiTransparent
             && !((REG_BLDCNT & BLDCNT_TGT1_OBJ) && blendMode != 0 && sLineEffects))
            {
                int texY = local_y + (int)(height / 2);
                const uint8_t *tiles = (const uint8_t *)objtiles;
                const uint16_t *pal = (const uint16_t *)(PLTT + 0x200);
                unsigned int rowTiles = (REG_DISPCNT & 0x40) ? width / 8 : 16;
                bool drew = false;

                if (lastX < firstX)
                    continue;
                if (flipY)
                    texY = height - 1 - texY;
                if (!is8BPP)
                {
                    // A tile's row of 8 pixels at a time, read as one word
                    const uint8_t *rowBase = tiles + ((texY / 8) * rowTiles + oam->tileNum) * 32 + (texY % 8) * 4;
                    const uint16_t *subPal = pal + oam->paletteNum * 16;
                    uint16_t *out = &pixels[x + firstX + RENDER_MARGIN];
                    int local_x = firstX;

                    while (local_x <= lastX)
                    {
                        unsigned int texX = local_x + width / 2;
                        uint32_t row;
                        unsigned int sub, n;

                        if (flipX)
                            texX = width - 1 - texX;
                        row = *(const uint32_t *)(rowBase + (texX / 8) * 32);
                        // Mirrored, the row is flipped so it still runs left to right
                        if (flipX)
                        {
                            row = FlipTileRow4(row);
                            sub = 7 - texX % 8;
                        }
                        else
                        {
                            sub = texX % 8;
                        }
                        n = 8 - sub;
                        if (n > (unsigned int)(lastX - local_x + 1))
                            n = lastX - local_x + 1;
                        row >>= sub * 4;
                        local_x += n;
                        if (row == 0)
                        {
                            out += n;
                            continue;
                        }
                        for (; n > 0; n--, out++, row >>= 4)
                        {
                            if (row & 0xF)
                            {
                                *out = subPal[row & 0xF] | (1 << 15);
                                // Over a blending sprite's pixel, this one doesn't blend
                                if (sLineSpriteBlend)
                                    scanline->spriteBlend[oam->priority][out - pixels] = 0;
                                drew = true;
                            }
                        }
                    }
                }
                else for (int local_x = firstX; local_x <= lastX; local_x++)
                {
                    unsigned int texX = local_x + width / 2;
                    unsigned int pixel;

                    if (flipX)
                        texX = width - 1 - texX;
                    if (!is8BPP)
                    {
                        const uint8_t *row = tiles + ((texY / 8) * rowTiles + texX / 8 + oam->tileNum) * 32 + (texY % 8) * 4;

                        pixel = row[(texX % 8) / 2];
                        pixel = (texX & 1) ? pixel >> 4 : pixel & 0xF;
                        if (pixel != 0)
                            pixel += oam->paletteNum * 16;
                    }
                    else
                    {
                        pixel = tiles[(((texY / 8) * rowTiles + texX / 8) * 2 + oam->tileNum) * 32 + (texY % 8) * 8 + texX % 8];
                    }
                    if (pixel != 0)
                    {
                        pixels[x + local_x + RENDER_MARGIN] = pal[pixel] | (1 << 15);
                        if (sLineSpriteBlend)
                            scanline->spriteBlend[oam->priority][x + local_x + RENDER_MARGIN] = 0;
                        drew = true;
                    }
                }
                if (drew)
                {
                    unsigned int first = x + firstX + RENDER_MARGIN, end = x + lastX + RENDER_MARGIN + 1;

                    if (!(scanline->spritePriorities & (1 << oam->priority)))
                    {
                        scanline->spritePriorities |= 1 << oam->priority;
                        scanline->spriteFirstX[oam->priority] = first;
                        scanline->spriteEndX[oam->priority] = end;
                    }
                    else
                    {
                        if (first < scanline->spriteFirstX[oam->priority])
                            scanline->spriteFirstX[oam->priority] = first;
                        if (end > scanline->spriteEndX[oam->priority])
                            scanline->spriteEndX[oam->priority] = end;
                    }
                }
                continue;
            }
            // A turned or scaled sprite that nothing blends, like a Pokémon
            // coming out of its ball: the texture's coordinates step along by
            // the matrix's first column instead of being worked out for each
            // pixel, with the same results
            if (!identity && oam->mosaic != 1 && !isObjWin && !isSemiTransparent
             && !((REG_BLDCNT & BLDCNT_TGT1_OBJ) && blendMode != 0 && sLineEffects))
            {
                const uint8_t *tiles = (const uint8_t *)objtiles;
                const uint16_t *pal = (const uint16_t *)(PLTT + 0x200);
                unsigned int rowTiles = (REG_DISPCNT & 0x40) ? width / 8 : 16;
                unsigned int tileNum = oam->tileNum;
                unsigned int palBase = oam->paletteNum * 16;
                int32_t pa = matrix[0][0], pc = matrix[1][0];
                int32_t accX = pa * firstX + matrix[0][1] * local_y;
                int32_t accY = pc * firstX + matrix[1][1] * local_y;
                int first = -1, last = -1;

                for (int local_x = firstX; local_x <= lastX; local_x++, accX += pa, accY += pc)
                {
                    unsigned int texX = (unsigned int)((accX >> 8) + (int)(width / 2));
                    unsigned int texY = (unsigned int)((accY >> 8) + (int)(height / 2));
                    unsigned int pixel;

                    if (texX >= width || texY >= height)
                        continue;
                    if (!is8BPP)
                    {
                        pixel = tiles[((texY / 8) * rowTiles + texX / 8 + tileNum) * 32 + (texY % 8) * 4 + (texX % 8) / 2];
                        pixel = (texX & 1) ? pixel >> 4 : pixel & 0xF;
                        if (pixel != 0)
                            pixel += palBase;
                    }
                    else
                    {
                        pixel = tiles[(((texY / 8) * rowTiles + texX / 8) * 2 + tileNum) * 32 + (texY % 8) * 8 + texX % 8];
                    }
                    if (pixel != 0)
                    {
                        int bx = x + local_x + RENDER_MARGIN;

                        pixels[bx] = pal[pixel] | (1 << 15);
                        if (sLineSpriteBlend)
                            scanline->spriteBlend[oam->priority][bx] = 0;
                        if (first < 0)
                            first = bx;
                        last = bx;
                    }
                }
                if (first >= 0)
                {
                    unsigned int end = last + 1;

                    if (!(scanline->spritePriorities & (1 << oam->priority)))
                    {
                        scanline->spritePriorities |= 1 << oam->priority;
                        scanline->spriteFirstX[oam->priority] = first;
                        scanline->spriteEndX[oam->priority] = end;
                    }
                    else
                    {
                        if ((unsigned int)first < scanline->spriteFirstX[oam->priority])
                            scanline->spriteFirstX[oam->priority] = first;
                        if (end > scanline->spriteEndX[oam->priority])
                            scanline->spriteEndX[oam->priority] = end;
                    }
                }
                continue;
            }
            for (int local_x = firstX; local_x <= lastX; local_x++)
            {
                uint8_t *tiledata = (uint8_t *)objtiles;
                uint16_t *palette = (uint16_t *)(PLTT + 0x200);
                int local_mosaicX;
                int tex_x;
                int tex_y;

                // The game's column, and the line's (bx)
                int global_x = local_x + x;
                int bx = global_x + RENDER_MARGIN;

                // Sprites go out into the margins only on lines where a
                // background fills them (the overworld). Elsewhere they stop
                // at the GBA's edges, as on the GBA, so nothing coming on from
                // off screen shows up early.
                if (global_x < (sWinExtendMargins ? -RENDER_MARGIN : 0)
                 || global_x >= (sWinExtendMargins ? DISPLAY_WIDTH + RENDER_MARGIN : DISPLAY_WIDTH))
                    continue;

                if (identity)
                {
                    tex_x = local_x + (int)(width / 2);
                    tex_y = local_y + (int)(height / 2);
                }
                else if (oam->mosaic == 1)
                {
                    //mosaic effect has to be applied to global coordinates otherwise the mosaic will scroll
                    local_mosaicX = applySpriteHorizontalMosaicEffect(global_x) - x;
                    tex_x = ((matrix[0][0] * local_mosaicX + matrix[0][1] * local_y) >> 8) + (width / 2);
                    tex_y = ((matrix[1][0] * local_mosaicX + matrix[1][1] * local_y) >> 8) + (height / 2);
                }else{
                    tex_x = ((matrix[0][0] * local_x + matrix[0][1] * local_y) >> 8) + (width / 2);
                    tex_y = ((matrix[1][0] * local_x + matrix[1][1] * local_y) >> 8) + (height / 2);
                }

                /* Check if transformed coordinates are inside bounds. */

                if (tex_x >= width || tex_y >= height || tex_x < 0 || tex_y < 0)
                    continue;

                if (flipX)
                    tex_x = width  - tex_x - 1;
                if (flipY)
                    tex_y = height - tex_y - 1;

                int tile_x = tex_x % 8;
                int tile_y = tex_y % 8;
                int block_x = tex_x / 8;
                int block_y = tex_y / 8;
                int block_offset = ((block_y * (REG_DISPCNT & 0x40 ? (width / 8) : 16)) + block_x);
                uint16_t pixel = 0;

                if (!is8BPP)
                {
                    pixel = tiledata[(block_offset + oam->tileNum) * 32 + (tile_y * 4) + (tile_x / 2)];
                    if (tile_x & 1)
                        pixel >>= 4;
                    else
                        pixel &= 0xF;
                    palette += oam->paletteNum * 16;
                }
                else
                {
                    pixel = tiledata[(block_offset * 2 + oam->tileNum) * 32 + (tile_y * 8) + tile_x];
                }

                if (pixel != 0)
                {
                    uint16_t color = palette[pixel];;
                    
                    //if sprite mode is 2 then write to the window mask instead
                    if (isObjWin)
                    {
                        if (scanline->winMask[bx] & WINMASK_WINOUT)
                        scanline->winMask[bx] = (REG_WINOUT >> 8) & 0x3F;
                        continue;
                    }
                    //this code runs if pixel is to be drawn
                    {
                        //check if its enabled in the window (if window is enabled)
                        winShouldBlendPixel = windowsEnabled ? (scanline->winMask[bx] & WINMASK_CLR) != 0 : sLineEffects;
                        
                        // Blending with what's below is done when the line is
                        // put together (see spriteBlend)
                        bool blendLater = (blendMode == 1 && REG_BLDCNT & BLDCNT_TGT1_OBJ && winShouldBlendPixel) || isSemiTransparent;

                        if (sLineSpriteBlend)
                            scanline->spriteBlend[oam->priority][bx] = blendLater;
                        if (blendLater && OLD_BLEND)
                        {
                            uint16_t targetB = 0;

                            if (alphaBlendSelectTargetB(scanline, &targetB, oam->priority, 0, bx, false))
                                color = alphaBlendColor(color, targetB);
                        }
                        else if (blendLater)
                            ;
                        else if (REG_BLDCNT & BLDCNT_TGT1_OBJ && winShouldBlendPixel)
                        {
                            switch (blendMode)
                            {
                            case 2:
                                color = alphaBrightnessIncrease(color);
                                break;
                            case 3:
                                color = alphaBrightnessDecrease(color);
                                break;
                            }
                        }
                        
                        //write pixel to pixel framebuffer
                        pixels[bx] = color | (1 << 15);
                        if (!(scanline->spritePriorities & (1 << oam->priority)))
                        {
                            scanline->spritePriorities |= 1 << oam->priority;
                            scanline->spriteFirstX[oam->priority] = bx;
                            scanline->spriteEndX[oam->priority] = bx + 1;
                        }
                        else if (bx < scanline->spriteFirstX[oam->priority])
                            scanline->spriteFirstX[oam->priority] = bx;
                        else if (bx >= scanline->spriteEndX[oam->priority])
                            scanline->spriteEndX[oam->priority] = bx + 1;
                    }
                }
            }
        }
    }
}

// The palette entry of a text background's pixel at the game's column gx, on
// line lineNum, or 0 where it's clear
static unsigned int TextBgColorIndexAt(uint16_t control, uint16_t hoffs, uint16_t voffs, int lineNum, int gx)
{
    unsigned int charBaseBlock = (control >> 2) & 3;
    unsigned int screenBaseBlock = (control >> 8) & 0x1F;
    bool is8bpp = (control >> 7) & 1;
    unsigned int mapWidthInPixels = bgMapSizes[control >> 14][0] * 8;
    unsigned int mapHeightInPixels = bgMapSizes[control >> 14][1] * 8;
    uint16_t *bgmap = (uint16_t *)BG_SCREEN_ADDR(screenBaseBlock);
    unsigned int xx = (gx + hoffs) & 0x1FF;
    unsigned int yy = (lineNum + voffs) & 0x1FF;
    uint8_t *tiles = (uint8_t *)BG_CHAR_ADDR(charBaseBlock);

    if (xx > 255 && mapWidthInPixels > 256)
        bgmap += 0x400;
    if (yy > 255 && mapHeightInPixels > 256)
        bgmap += (mapWidthInPixels > 256) ? 0x800 : 0x400;
    xx &= 0xFF;
    yy &= 0xFF;

    uint16_t entry = bgmap[(yy / 8) * 32 + xx / 8];
    unsigned int tileX = (entry & (1 << 10)) ? 7 - xx % 8 : xx % 8;
    unsigned int tileY = (entry & (1 << 11)) ? 7 - yy % 8 : yy % 8;

    if (is8bpp)
        return tiles[(uint16_t)((entry & 0x3FF) * 64) + tileY * 8 + tileX];
    unsigned int pixel = (tiles[(uint16_t)((entry & 0x3FF) * 32) + tileY * 4 + tileX / 2] >> ((tileX & 1) * 4)) & 0xF;
    return pixel != 0 ? (entry >> 12) * 16 + pixel : 0;
}

static bool TextBgOpaqueAt(uint16_t control, uint16_t hoffs, uint16_t voffs, int lineNum, int gx)
{
    return TextBgColorIndexAt(control, hoffs, voffs, lineNum, gx) != 0;
}

// Boxes and backgrounds drawn to the GBA screen's edges, like the quest log's
// bars, the start menu's help and menus' backgrounds, carry on across the
// margins: where a background narrower than the line is the top one showing at
// the screen's edge column, the line's margin on that side takes that column's
// color. Boxes that stop short of the edges, like dialogue, stay as they are.
// Where no background covers the edge column, the GBA shows the backdrop
// there, like the controls guide's blue page, so the margin keeps it too.
static void ExtendEdges(uint16_t *pixels, int lineNum, const uint16_t *bgcnts, bool windowsEnabled, const uint16_t *winMask,
                        uint16_t (*layers)[RENDER_WIDTH])
{
    static const int sEdgeColumns[2] = {0, DISPLAY_WIDTH - 1};
    unsigned int mode = REG_DISPCNT & 3;

    for (int side = 0; side < 2; side++)
    {
        int gx = sEdgeColumns[side];
        int bestNarrow = 4, bestWide = 4, bestNarrowBg = 0;
        bool endsAtEdgeOnTop = false;

        for (int bgnum = 0; bgnum < (mode == 0 ? 4 : 2); bgnum++)
        {
            uint16_t control = bgcnts[bgnum];
            int priority = control & 3;
            bool wide = BgFillsMargin(bgnum, bgMapSizes[control >> 14][0] * 8, side);

            if (!isbgEnabled(bgnum))
                continue;
            if (windowsEnabled && !(winMask[gx + RENDER_MARGIN] & (1 << bgnum)))
                continue;
            // Drawn over whatever the margin carries on, below
            if (gWideViewNoWrapBgs & (1 << bgnum))
                continue;
            if (gWideViewEndsAtEdgeBgs & (1 << bgnum))
            {
                // Not carried on, but what's showing at the edge column may
                // be it rather than the background that is
                if (TextBgOpaqueAt(control, *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4),
                                   *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4), lineNum, gx))
                    endsAtEdgeOnTop = true;
                continue;
            }
            if (wide)
            {
                if (priority < bestWide)
                    bestWide = priority;
            }
            else if (priority < bestNarrow
                  && TextBgOpaqueAt(control, *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4),
                                    *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4), lineNum, gx))
            {
                bestNarrow = priority;
                bestNarrowBg = bgnum;
            }
        }
        // A background that stops at the edge, like the title's Charizard,
        // has nothing past it but what's behind the screen: the margin shows
        // that rather than carrying on what's under him at the edge column
        if (endsAtEdgeOnTop)
            bestNarrow = 4;
        if (bestNarrow < 4 && bestNarrow <= bestWide)
        {
            uint16_t color = pixels[gx + RENDER_MARGIN];

            // Under a sprite, take the color from the carried background itself
            if (sSpriteAtEdge[side])
            {
                uint16_t control = bgcnts[bestNarrowBg];

                color = ((uint16_t *)PLTT)[TextBgColorIndexAt(control, *(uint16_t *)(REG_ADDR_BG0HOFS + bestNarrowBg * 4),
                                                             *(uint16_t *)(REG_ADDR_BG0VOFS + bestNarrowBg * 4), lineNum, gx)];
                if ((REG_BLDCNT & (1 << bestNarrowBg)) && ((REG_BLDCNT >> 6) & 3) == 2)
                    color = alphaBrightnessIncrease(color);
                else if ((REG_BLDCNT & (1 << bestNarrowBg)) && ((REG_BLDCNT >> 6) & 3) == 3)
                    color = alphaBrightnessDecrease(color);
            }

            if (side == 0)
                memsetu16(pixels, color, RENDER_MARGIN);
            else
                memsetu16(&pixels[RENDER_MARGIN + DISPLAY_WIDTH], color, RENDER_MARGIN);

            // Backgrounds that show their own map in the margins (see
            // gWideViewNoWrapBgs) go over what's carried on, where they're
            // above it, from the bottom up
            for (int priority = bestNarrow - 1; priority >= 0; priority--)
            {
                for (int bgnum = 3; bgnum >= 0; bgnum--)
                {
                    unsigned int first = side == 0 ? 0 : RENDER_MARGIN + DISPLAY_WIDTH;

                    if (!(gWideViewNoWrapBgs & (1 << bgnum)) || !isbgEnabled(bgnum) || (bgcnts[bgnum] & 3) != priority
                     || (windowsEnabled && !(winMask[gx + RENDER_MARGIN] & (1 << bgnum))))
                        continue;
                    for (unsigned int x = first; x < first + RENDER_MARGIN; x++)
                    {
                        if (layers[bgnum][x] & (1 << 15))
                            pixels[x] = layers[bgnum][x];
                    }
                }
            }
        }
        // (a background that fills the margin from its map counts too, for
        // screens that only fill one, see gWideViewLeftOnlyBgs)
        bool extended = (bestNarrow < 4 && bestNarrow <= bestWide) || (bestNarrow == 4 && bestWide == 4)
                     || bestWide < bestNarrow;

        if (side == 0)
            sExtendedLeft = extended;
        else
            sExtendedRight = extended;
    }
}

#ifdef PSP_PROFILE
// Time spent in each part of drawing, in microseconds, for the speed log
unsigned int gDrawProfile[8];
unsigned int sceKernelGetSystemTimeLow(void);
static unsigned int sProfMark;
#define PROF(n) do { unsigned int t_ = sceKernelGetSystemTimeLow(); gDrawProfile[n] += t_ - sProfMark; sProfMark = t_; } while (0)
#define PROF_START() (sProfMark = sceKernelGetSystemTimeLow())
#else
#define PROF(n) ((void)0)
#define PROF_START() ((void)0)
#endif

// Each line's fade over everything that's still to be done, as mode << 8 |
// strength (see DrawScanline), or 0
static uint16_t sLineFade[DISPLAY_HEIGHT];
// The whole frame's, when every line has the same one, for the PSP's GE to do
// as it puts the frame on the screen (see DrawFrameFade)
static uint16_t sFrameFade;

static void DrawScanline(uint16_t *pixels, uint16_t vcount)
{
    unsigned int mode = REG_DISPCNT & 3;
    unsigned char numOfBgs = (mode == 0 ? 4 : 3);
    int bgnum, prnum;
    // Kept from line to line, so that its buffers only need clearing where
    // they were drawn on. winMask is only read when the windows are on, which
    // fills it first.
    static struct scanlineData scanline;
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
    unsigned int xpos;

    sSpriteAtEdge[0] = sSpriteAtEdge[1] = false;
    PROF_START();

    bool windowsEnabled = false;
    uint16_t WIN0bottom, WIN0top, WIN0right, WIN0left;
    uint16_t WIN1bottom, WIN1top, WIN1right, WIN1left;
    bool WIN0enable, WIN1enable;
    WIN0enable = false;
    WIN1enable = false;

    //figure out if WIN0 masks on this scanline
    if (REG_DISPCNT & DISPCNT_WIN0_ON)
    {
        //acquire the window coordinates
        WIN0bottom = (REG_WIN0V & 0xFF); //y2;
        WIN0top = (REG_WIN0V & 0xFF00) >> 8; //y1;
        WIN0right = (REG_WIN0H & 0xFF); //x2
        WIN0left = (REG_WIN0H & 0xFF00) >> 8; //x1
        
        //figure out WIN Y wraparound and check bounds accordingly
        if (WIN0top > WIN0bottom) {
            if (vcount >= WIN0top || vcount < WIN0bottom)
                WIN0enable = true;
        } else {
            if (vcount >= WIN0top && vcount < WIN0bottom)
                WIN0enable = true;
        }
        
        windowsEnabled = true;
    }
    //figure out if WIN1 masks on this scanline
    if (REG_DISPCNT & DISPCNT_WIN1_ON)
    {
        WIN1bottom = (REG_WIN1V & 0xFF); //y2;
        WIN1top = (REG_WIN1V & 0xFF00) >> 8; //y1;
        WIN1right = (REG_WIN1H & 0xFF); //x2
        WIN1left = (REG_WIN1H & 0xFF00) >> 8; //x1
        
        if (WIN1top > WIN1bottom) {
            if (vcount >= WIN1top || vcount < WIN1bottom)
                WIN1enable = true;
        } else {
            if (vcount >= WIN1top && vcount < WIN1bottom)
                WIN1enable = true;
        }
        
        windowsEnabled = true;
    }
    //enable windows if OBJwin is enabled
    if (REG_DISPCNT & DISPCNT_OBJWIN_ON && REG_DISPCNT & DISPCNT_OBJ_ON)
    {
        windowsEnabled = true;
    }
    
    // A dark cave's light (Flash): WIN0 is the circle, clamped to the GBA's
    // screen. Where the line's WIN0 is what the game worked out for it, the
    // circle's own span carries on round into the margins
    bool win0Wide = false;
    int win0WideLeft = 0, win0WideRight = 0;
    if (WIN0enable && gWideViewFlashActive && vcount < DISPLAY_HEIGHT)
    {
        for (int b = 0; b < 2 && !win0Wide; b++)
        {
            int l = gWideViewFlashSpans[b][vcount][0], r = gWideViewFlashSpans[b][vcount][1];
            int cl = l < 0 ? 0 : l > 255 ? 255 : l, cr = r < 0 ? 0 : r > 255 ? 255 : r;

            if (((cl << 8) | cr) == REG_WIN0H)
            {
                win0Wide = true;
                win0WideLeft = l < -RENDER_MARGIN ? -RENDER_MARGIN : l;
                win0WideRight = r > DISPLAY_WIDTH + RENDER_MARGIN ? DISPLAY_WIDTH + RENDER_MARGIN : r;
            }
        }
    }

    sLineLayers = 0x1F;
    sLineEffects = true;
    if (windowsEnabled)
    {
        // Where one window's settings cover the whole line, like the
        // battle's: WIN0 or WIN1 across it, or neither on it and no sprite
        // window, so it's all outside. Then the line is drawn as if there
        // were no windows, with that window's layers and effects.
        bool win0Full = win0Wide ? (win0WideLeft <= -RENDER_MARGIN && win0WideRight >= DISPLAY_WIDTH + RENDER_MARGIN)
                                 : (WIN0enable && WIN0left == 0 && WIN0right >= DISPLAY_WIDTH && WIN0right > WIN0left);
        bool win0None = !WIN0enable || (win0Wide ? win0WideLeft >= win0WideRight : WIN0left == WIN0right);
        bool win1Full = WIN1enable && WIN1left == 0 && WIN1right >= DISPLAY_WIDTH && WIN1right > WIN1left;
        bool win1None = !WIN1enable || WIN1left == WIN1right;
        int mask = -1;

        if (win0Full)
            mask = REG_WININ & 0x3F;
        else if (win0None && win1Full)
            mask = (REG_WININ >> 8) & 0x3F;
        else if (win0None && win1None)
        {
            bool objWin = false;

            if ((REG_DISPCNT & DISPCNT_OBJWIN_ON) && (REG_DISPCNT & DISPCNT_OBJ_ON) && vcount < DISPLAY_HEIGHT)
            {
                UpdateSprites();
                for (int n = 0; n < sLineSpriteCount[vcount] && !objWin; n++)
                    objWin = sSprites[sLineSprites[vcount][n]].oam->objMode == 2;
            }
            if (!objWin)
                mask = REG_WINOUT & 0x3F;
        }
        if (mask >= 0)
        {
            sLineLayers = mask & 0x1F;
            sLineEffects = (mask & WINMASK_CLR) != 0;
            windowsEnabled = false;
            if (!sLineEffects)
                blendMode = 0;
        }
    }


    // The layers every window region on the line shows, like the summary
    // screen's backgrounds, which only its sprites differ on: those need no
    // window check for each pixel
    uint8_t winAll = 0x3F;
    if (windowsEnabled)
    {
        winAll = REG_WINOUT & 0x3F;
        if (WIN0enable && WIN0left != WIN0right)
            winAll &= REG_WININ & 0x3F;
        if (WIN1enable && WIN1left != WIN1right)
            winAll &= (REG_WININ >> 8) & 0x3F;
        if ((REG_DISPCNT & DISPCNT_OBJWIN_ON) && (REG_DISPCNT & DISPCNT_OBJ_ON))
            winAll &= (REG_WINOUT >> 8) & 0x3F;
    }

    // A fade to or from black or white over everything (all layers, sprites
    // and the backdrop are first targets, nothing on the line limits the
    // effect, no sprite is semi-transparent), like the battle's after the
    // party menu: the line is drawn as without it, which keeps the fast
    // paths, and every finished pixel is brightened or darkened once. On the
    // GBA the effect changes the top pixel wherever it's a first target,
    // which here is every pixel, so it's the same.
    uint16_t savedBldcnt = 0;
    unsigned int lateBrightness = 0;
    if ((blendMode == 2 || blendMode == 3) && (REG_BLDCNT & 0x3F) == 0x3F && !windowsEnabled && sLineEffects)
    {
        UpdateSprites();
        if (!sAnySemiTransparent)
        {
            lateBrightness = blendMode;
            savedBldcnt = REG_BLDCNT;
            REG_BLDCNT = 0;
            blendMode = 0;
            // DrawLine filled it with the changed backdrop
            memsetu16(pixels, *(uint16_t *)PLTT, RENDER_WIDTH);
        }
    }

    //initialize all priority bookkeeping data
    memset(scanline.prioritySortedBgsCount, 0, sizeof(scanline.prioritySortedBgsCount));

    for (bgnum = 0; bgnum < numOfBgs; bgnum++)
    {
        uint16_t bgcnt = *(uint16_t*)(REG_ADDR_BG0CNT + bgnum * 2);
        uint16_t priority;
        scanline.bgcnts[bgnum] = bgcnt;
        scanline.bgtoprio[bgnum] = priority = (bgcnt & 3);
        
        char priorityCount = scanline.prioritySortedBgsCount[priority];
        scanline.prioritySortedBgs[priority][priorityCount] = bgnum;
        scanline.prioritySortedBgsCount[priority]++;
    }

    // A line is wide when a text background on it is wide enough to fill the
    // margins, like the overworld's 512-pixel maps. Then the windows and the
    // sprites reach into the margins too (see FillWindowSpan).
    // Only on screens that say they fill the margins (gWideViewMargins):
    // others, like the summary screen, have wide maps for scrolling whose
    // other parts aren't meant to be seen.
    sWinExtendMargins = false;
    for (bgnum = 0; bgnum < (mode == 0 ? 4 : 2); bgnum++)
    {
        if (isbgEnabled(bgnum) && BgFillsMargins(bgnum, bgMapSizes[scanline.bgcnts[bgnum] >> 14][0] * 8))
            sWinExtendMargins = true;
    }

    // Most of the overworld's lines have nothing blended with what's below it.
    // Then nothing reads the backgrounds' layers, and the 4bpp text ones that
    // don't change brightness are drawn straight onto the line when it's put
    // together, instead of into their layers first.
    uint8_t directBgs = 0;
    {
        bool spritesBlend = false;
        bool layersRead;

        if (REG_DISPCNT & DISPCNT_OBJ_ON)
        {
            UpdateSprites();
            spritesBlend = blendMode == 1 && (REG_BLDCNT & BLDCNT_TGT1_OBJ);
            // A semi-transparent sprite blends with the layers below only on
            // its own lines, like the summary screen's
            if (sAnySemiTransparent && !spritesBlend && vcount < DISPLAY_HEIGHT)
            {
                for (int n = 0; n < sLineSpriteCount[vcount] && !spritesBlend; n++)
                    spritesBlend = sSprites[sLineSprites[vcount][n]].oam->objMode == 1;
            }
        }
        // Blending reads the line drawn so far (see sBelow), not the layers
        // below, so the backgrounds that don't blend can be drawn straight
        // onto the line.
        sLineSpriteBlend = spritesBlend && !OLD_BLEND;
        layersRead = OLD_BLEND && (spritesBlend || (blendMode == 1 && (REG_BLDCNT & (REG_DISPCNT >> 8) & 0xF)));
        if (!layersRead)
        {
            for (bgnum = 0; bgnum < (mode == 0 ? 4 : mode == 1 ? 2 : 0); bgnum++)
            {
                if (CanDrawBGDirectly(scanline.bgcnts[bgnum]) && !(blendMode != 0 && (REG_BLDCNT & (1 << bgnum)))
                 && !((gWideViewClampBgs | gWideViewLeftOnlyBgs | gWideViewNoWrapBgs) & (1 << bgnum)))
                    directBgs |= 1 << bgnum;
            }
        }
    }

    PROF(0);   // setup and sprite decoding
    switch (mode)
    {
    case 0:
        // All backgrounds are text mode
        for (bgnum = 3; bgnum >= 0; bgnum--)
        {
            if (isbgEnabled(bgnum) && !(directBgs & (1 << bgnum)))
            {
                uint16_t bghoffs = *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4);
                uint16_t bgvoffs = *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4);
                
                RenderBGScanline(bgnum, scanline.bgcnts[bgnum], bghoffs, bgvoffs, vcount, scanline.layers[bgnum]);
            }
        }
        
        break;
    case 1:
        // BG2 is affine
        bgnum = 2;
        if (isbgEnabled(bgnum))
        {
            RenderRotScaleBGScanline(bgnum, scanline.bgcnts[bgnum], REG_BG2X, REG_BG2Y, vcount, scanline.layers[bgnum]);
        }
        // BG0 and BG1 are text mode
        for (bgnum = 1; bgnum >= 0; bgnum--)
        {
            if (isbgEnabled(bgnum) && !(directBgs & (1 << bgnum)))
            {
                uint16_t bghoffs = *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4);
                uint16_t bgvoffs = *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4);
                
                RenderBGScanline(bgnum, scanline.bgcnts[bgnum], bghoffs, bgvoffs, vcount, scanline.layers[bgnum]);
            }
        }
        break;
    default:
        DBGPRINTF("Video mode %u is unsupported.\n", mode);
        memset(scanline.layers, 0, sizeof(scanline.layers));
        break;
    }
    
    PROF(1);   // backgrounds drawn into layers
    BLEND_STAT(2, windowsEnabled);
    BLEND_STAT(3, 1);
    //draw to pixel mask: outside, then WIN1 over it, then WIN0, which wins
    //where they overlap
    if (windowsEnabled)
    {
        memsetu16(scanline.winMask, (REG_WINOUT & 0x3F) | WINMASK_WINOUT, RENDER_WIDTH);
        if (WIN1enable)
            FillWindowSpan(scanline.winMask, WIN1left, WIN1right, (REG_WININ >> 8) & 0x3F);
        if (win0Wide)
            FillColumns(scanline.winMask, win0WideLeft, win0WideRight, REG_WININ & 0x3F);
        else if (WIN0enable)
            FillWindowSpan(scanline.winMask, WIN0left, WIN0right, REG_WININ & 0x3F);
    }

    PROF(2);   // windows
    if ((REG_DISPCNT & DISPCNT_OBJ_ON) && (sLineLayers & 0x10))
        DrawSprites(&scanline, vcount, windowsEnabled);
    PROF(3);   // sprites into their layers

    // Blending looks at the pixel below, which the line so far already has.
    // Where it came from (background 0-3, 4 for sprites, 5 for the backdrop)
    // says whether it's a second target. A pixel below that was itself
    // blended (BELOW_BLENDED) has its own color in sRaw: the GBA only blends
    // the top pixel, with the one under it as it is.
    static uint8_t sBelow[RENDER_WIDTH];
    static uint16_t sRaw[RENDER_WIDTH];
    bool trackBelow = (blendMode == 1 || sLineSpriteBlend) && !OLD_BLEND;
    // What was below a sprite pixel that went over another (sprites don't
    // blend with each other: a sprite blends with the background under both)
    static uint16_t sBgColor[RENDER_WIDTH];
    static uint8_t sBgSrc[RENDER_WIDTH];
    unsigned int blendTargets = REG_BLDCNT >> 8;
    unsigned int eva = REG_BLDALPHA & 0x1F, evb = (REG_BLDALPHA >> 8) & 0x1F;

    if (eva > 16)
        eva = 16;
    if (evb > 16)
        evb = 16;
    if (trackBelow)
        memset(sBelow, 5, sizeof(sBelow));
#define BELOW_SOURCE 7
#define BELOW_BLENDED 0x80

    //iterate trough every priority in order
    for (prnum = 3; prnum >= 0; prnum--)
    {
        for (char prsub = scanline.prioritySortedBgsCount[prnum] - 1; prsub >= 0; prsub--)
        {
            char bgnum = scanline.prioritySortedBgs[prnum][prsub];
            //if background is enabled then draw it
            if (isbgEnabled(bgnum))
            {
                uint16_t *src = scanline.layers[bgnum];

                // Without a blend on this layer, every opaque pixel is copied
                // as it is, where the windows let the layer show
                if (blendMode == 0 || !(REG_BLDCNT & (1 << bgnum)))
                {
                    if (directBgs & (1 << bgnum))
                    {
                        uint16_t bghoffs = *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4);
                        uint16_t bgvoffs = *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4);

                        RenderBGScanlineDirect(bgnum, scanline.bgcnts[bgnum], bghoffs, bgvoffs, vcount, pixels,
                                               (windowsEnabled && !(winAll & (1 << bgnum))) ? scanline.winMask : NULL,
                                               trackBelow ? sBelow : NULL);
                    }
                    else if (trackBelow)
                    {
                        bool allShow = !windowsEnabled || (winAll & (1 << bgnum));

                        for (xpos = 0; xpos < RENDER_WIDTH; xpos++)
                        {
                            if (getAlphaBit(src[xpos]) && (allShow || (scanline.winMask[xpos] & (1 << bgnum))))
                            {
                                pixels[xpos] = src[xpos];
                                sBelow[xpos] = bgnum;
                            }
                        }
                    }
                    else if (!windowsEnabled || (winAll & (1 << bgnum)))
                    {
                        for (xpos = 0; xpos < RENDER_WIDTH; xpos++)
                        {
                            if (getAlphaBit(src[xpos]))
                                pixels[xpos] = src[xpos];
                        }
                    }
                    else
                    {
                        for (xpos = 0; xpos < RENDER_WIDTH; xpos++)
                        {
                            if (getAlphaBit(src[xpos]) && (scanline.winMask[xpos] & (1 << bgnum)))
                                pixels[xpos] = src[xpos];
                        }
                    }
                    continue;
                }
                // A blended layer, blended with what's below it where that's a
                // second target (and where the windows allow effects)
                if (trackBelow && blendMode == 1)
                {
                    unsigned int bit = 1 << bgnum;

                    for (xpos = 0; xpos < RENDER_WIDTH; xpos++)
                    {
                        uint16_t color = src[xpos];

                        if (!getAlphaBit(color))
                            continue;
                        if (windowsEnabled)
                        {
                            uint16_t win = scanline.winMask[xpos];

                            if (!(win & bit))
                                continue;
                            if (!(win & WINMASK_CLR))
                            {
                                pixels[xpos] = color;
                                sBelow[xpos] = bgnum;
                                continue;
                            }
                        }
                        unsigned int under = sBelow[xpos];

                        BLEND_STAT(0, 1);
                        if (blendTargets & (1 << (under & BELOW_SOURCE)))
                        {
                            pixels[xpos] = BlendColors(color, (under & BELOW_BLENDED) ? sRaw[xpos] : pixels[xpos], eva, evb);
                            sRaw[xpos] = color;
                            sBelow[xpos] = bgnum | BELOW_BLENDED;
                            continue;
                        }
                        pixels[xpos] = color;
                        sBelow[xpos] = bgnum;
                    }
                    continue;
                }
                //copy all pixels to framebuffer 
                for (xpos = 0; xpos < RENDER_WIDTH; xpos++)
                {
                    uint16_t color = src[xpos];
                    bool winEffectEnable = true;
                    
                    if (!getAlphaBit(color))
                        continue; //do nothing if alpha bit is not set
                    
                    if (windowsEnabled)
                    {
                        winEffectEnable = ((scanline.winMask[xpos] & WINMASK_CLR) >> 5);
                        //if bg is disabled inside the window then do not draw the pixel
                        if ( !(scanline.winMask[xpos] & 1 << bgnum) )
                            continue;
                    }
                    
                    //blending code
                    if (blendMode != 0 && REG_BLDCNT & (1 << bgnum) && winEffectEnable)
                    {
                        uint16_t targetA = color;
                        uint16_t targetB = 0;
                        char isSpriteBlendingEnabled;
                        
                        switch (blendMode)
                        {
                        case 1:
                            isSpriteBlendingEnabled = REG_BLDCNT & BLDCNT_TGT2_OBJ ? 1 : 0;
                            //find targetB and blend it
                            if (alphaBlendSelectTargetB(&scanline, &targetB, prnum, prsub+1, xpos, isSpriteBlendingEnabled))
                            {
                                color = alphaBlendColor(targetA, targetB);
                            }
                            break;
                        case 2:
                            color = alphaBrightnessIncrease(targetA);
                            break;
                        case 3:
                            color = alphaBrightnessDecrease(targetA);
                            break;
                        }
                    }
                    //write the pixel to scanline buffer output
                    if (trackBelow)
                    {
                        sBelow[xpos] = bgnum;
                        if (color != src[xpos])
                        {
                            sRaw[xpos] = src[xpos];
                            sBelow[xpos] = bgnum | BELOW_BLENDED;
                        }
                    }
                    pixels[xpos] = color;
                }
            }
        }
        //draw sprites on current priority
        if (!(scanline.spritePriorities & (1 << prnum)))
            continue;
        uint16_t *src = scanline.spriteLayers[prnum];
        for (xpos = scanline.spriteFirstX[prnum]; xpos < scanline.spriteEndX[prnum]; xpos++)
        {
            if (getAlphaBit(src[xpos]))
            {
                //check if sprite pixel draws inside window
                if (windowsEnabled && !(scanline.winMask[xpos] & WINMASK_OBJ))
                        continue;
                //draw the pixel
                uint16_t color = src[xpos];

                if (trackBelow)
                {
                    unsigned int under = sBelow[xpos];

                    // What's under, as it is (not blended itself)
                    if ((under & BELOW_SOURCE) != 4)
                    {
                        sBgColor[xpos] = (under & BELOW_BLENDED) ? sRaw[xpos] : pixels[xpos];
                        sBgSrc[xpos] = under & BELOW_SOURCE;
                    }
                    sBelow[xpos] = 4;
                    if (sLineSpriteBlend && scanline.spriteBlend[prnum][xpos] && (blendTargets & (1 << sBgSrc[xpos])))
                    {
                        sRaw[xpos] = color;
                        sBelow[xpos] = 4 | BELOW_BLENDED;
                        color = BlendColors(color, sBgColor[xpos], eva, evb);
                    }
                }
                pixels[xpos] = color;
                if (xpos == RENDER_MARGIN)
                    sSpriteAtEdge[0] = true;
                else if (xpos == RENDER_MARGIN + DISPLAY_WIDTH - 1)
                    sSpriteAtEdge[1] = true;
            }
        }
    }

    PROF(4);   // putting the line together (direct backgrounds included)
    for (prnum = 0; prnum < 4; prnum++)
    {
        if (scanline.spritePriorities & (1 << prnum))
            memset(&scanline.spriteLayers[prnum][scanline.spriteFirstX[prnum]], 0,
                   (scanline.spriteEndX[prnum] - scanline.spriteFirstX[prnum]) * sizeof(scanline.spriteLayers[prnum][0]));
    }
    scanline.spritePriorities = 0;

    ExtendEdges(pixels, vcount, scanline.bgcnts, windowsEnabled, scanline.winMask, scanline.layers);
    sLineFade[vcount] = 0;
    if (lateBrightness != 0)
    {
        unsigned int evy = REG_BLDY & 0x1F;

        REG_BLDCNT = savedBldcnt;
        // Done once the frame's drawn (see FinishFades)
        sLineFade[vcount] = (lateBrightness << 8) | (evy > 16 ? 16 : evy);
    }
    PROF(5);   // clearing sprite layers, carrying edges on
}

uint16_t *memsetu16(uint16_t *dst, uint16_t fill, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        dst[i] = fill;
    }
    return dst;
}

// What a line does before it's drawn: the VCOUNT interrupt
static void BeginLine(int i)
{
    REG_VCOUNT = i;
    if(((REG_DISPSTAT >> 8) & 0xFF) == REG_VCOUNT)
    {
        REG_DISPSTAT |= INTR_FLAG_VCOUNT;
        if(REG_DISPSTAT & DISPSTAT_VCOUNT_INTR)
                gIntrTable[0]();
    }
}

static void DrawLineOnce(uint16_t *line, int i);

static void DrawLine(uint16_t *line, int i)
{
#ifdef PSP_TEST
    if (gTestRenderAB)
    {
        static uint16_t oldLine[RENDER_WIDTH];

        sTestOldBlend = true;
        DrawLineOnce(oldLine, i);
        sTestOldBlend = false;
        DrawLineOnce(line, i);
        gTestRenderABLines++;
        for (int x = 0; x < RENDER_WIDTH; x++)
        {
            if (oldLine[x] != line[x])
            {
                if (gTestRenderABPixels++ == 0)
                {
                    void TestLog(const char *format, ...);

                    gTestRenderABFirst = (i << 16) | x;
                    TestLog("renderab first: line %d x %d old %04x new %04x dispcnt %04x bldcnt %04x bldalpha %04x bldy %04x"
                            " bg %04x %04x %04x %04x win %04x/%04x\n", i, x, oldLine[x], line[x], REG_DISPCNT, REG_BLDCNT,
                            REG_BLDALPHA, REG_BLDY, REG_BG0CNT, REG_BG1CNT, REG_BG2CNT, REG_BG3CNT, REG_WININ, REG_WINOUT);
                }
            }
        }
        return;
    }
#endif
    DrawLineOnce(line, i);
}

static void DrawLineOnce(uint16_t *line, int i)
{
    // Render the backdrop color before the each individual scanline.
    // backdrop color brightness effects
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
    uint16_t backdropColor = *(uint16_t *)PLTT;
    if (REG_BLDCNT & BLDCNT_TGT1_BD)
    {
        switch (blendMode)
        {
        case 2:
            backdropColor = alphaBrightnessIncrease(backdropColor);
            break;
        case 3:
            backdropColor = alphaBrightnessDecrease(backdropColor);
            break;
        }
    }

    memsetu16(line, backdropColor, RENDER_WIDTH);
    DrawScanline(line, i);
    // Screens made for the GBA's width leave the margins black, except where
    // ExtendEdges carried the edges on. A backdrop color seen only behind
    // boxes that stop short of the edges was never meant to fill the screen.
    if (!sWinExtendMargins)
    {
        if (!sExtendedLeft)
            memset(line, 0, RENDER_MARGIN * sizeof(line[0]));
        if (!sExtendedRight)
            memset(&line[RENDER_MARGIN + DISPLAY_WIDTH], 0, RENDER_MARGIN * sizeof(line[0]));
    }
}

// What a line does after it's drawn: HBlank DMAs and interrupt
static void EndLine(void)
{
    REG_DISPSTAT |= INTR_FLAG_HBLANK;

    RunDMAs(DMA_HBLANK);
    
    if (REG_DISPSTAT & DISPSTAT_HBLANK_INTR)
        gIntrTable[3]();

    REG_DISPSTAT &= ~INTR_FLAG_HBLANK;
    REG_DISPSTAT &= ~INTR_FLAG_VCOUNT;
}

// A fade over everything is the same change to every pixel of the line. When
// the whole frame has the same one (most fades) the GE does it for nothing;
// otherwise the lines that have one are changed here.
static void FinishFades(uint16_t *pixels)
{
    bool same = true;

    for (int i = 1; i < DISPLAY_HEIGHT && same; i++)
        same = sLineFade[i] == sLineFade[0];
#ifdef __PSP__
    if (same)
    {
        sFrameFade = sLineFade[0];
        return;
    }
#endif
    sFrameFade = 0;
    for (int i = 0; i < DISPLAY_HEIGHT; i++)
    {
        uint16_t *line = &pixels[i * RENDER_STRIDE];
        unsigned int evy = sLineFade[i] & 0xFF;
        uint8_t channel[32];

        if (sLineFade[i] == 0)
            continue;
        for (unsigned int c = 0; c < 32; c++)
            channel[c] = (sLineFade[i] >> 8) == 2 ? c + (31 - c) * evy / 16 : c - c * evy / 16;
        for (unsigned int x = 0; x < RENDER_WIDTH; x++)
        {
            uint16_t color = line[x];

            line[x] = channel[color & 0x1F] | (channel[(color >> 5) & 0x1F] << 5)
                    | (channel[(color >> 10) & 0x1F] << 10) | (1 << 15);
        }
    }
}

unsigned int DrawFrameFade(void)
{
    return sFrameFade;
}

void DrawFrame(uint16_t *pixels)
{
    int i;

    sCheckOam = true;

    for (i = 0; i < DISPLAY_HEIGHT; i++)
    {
        BeginLine(i);
        DrawLine(&pixels[i * RENDER_STRIDE], i);
        EndLine();
    }
    FinishFades(pixels);
}

void SkipFrame(void)
{
    int i;

    for (i = 0; i < DISPLAY_HEIGHT; i++)
    {
        BeginLine(i);
        EndLine();
    }
}

#ifdef __PS2__
#include "platform/ps2_gs.h"

static void SaveLineRegs(struct GsLineState *s)
{
    s->dispcnt = REG_DISPCNT;
    for (int bg = 0; bg < 4; bg++)
    {
        s->bgcnt[bg] = *(uint16_t *)(REG_ADDR_BG0CNT + bg * 2);
        s->hofs[bg] = *(uint16_t *)(REG_ADDR_BG0HOFS + bg * 4);
        s->vofs[bg] = *(uint16_t *)(REG_ADDR_BG0VOFS + bg * 4);
    }
    s->bldcnt = REG_BLDCNT;
    s->bldalpha = REG_BLDALPHA;
    s->bldy = REG_BLDY;
    s->winin = REG_WININ;
    s->winout = REG_WINOUT;
    s->win0h = REG_WIN0H;
    s->win0v = REG_WIN0V;
    s->win1h = REG_WIN1H;
    s->win1v = REG_WIN1V;
    s->mosaic = REG_MOSAIC;
    memcpy(s->bg2Affine, (const void *)REG_ADDR_BG2PA, sizeof(s->bg2Affine));
}

static void LoadLineRegs(const struct GsLineState *s)
{
    REG_DISPCNT = s->dispcnt;
    for (int bg = 0; bg < 4; bg++)
    {
        *(uint16_t *)(REG_ADDR_BG0CNT + bg * 2) = s->bgcnt[bg];
        *(uint16_t *)(REG_ADDR_BG0HOFS + bg * 4) = s->hofs[bg];
        *(uint16_t *)(REG_ADDR_BG0VOFS + bg * 4) = s->vofs[bg];
    }
    REG_BLDCNT = s->bldcnt;
    REG_BLDALPHA = s->bldalpha;
    REG_BLDY = s->bldy;
    REG_WININ = s->winin;
    REG_WINOUT = s->winout;
    REG_WIN0H = s->win0h;
    REG_WIN0V = s->win0v;
    REG_WIN1H = s->win1h;
    REG_WIN1V = s->win1v;
    REG_MOSAIC = s->mosaic;
    memcpy((void *)REG_ADDR_BG2PA, s->bg2Affine, sizeof(s->bg2Affine));
}

// Runs the lines like DrawFrame, keeping each one's video registers, and then
// has the GS draw them. What the GS can't draw like the GBA is drawn in
// software, from the registers kept.
void DrawFramePS2(void)
{
    static struct GsLineState lines[DISPLAY_HEIGHT];
    static uint16_t image[RENDER_STRIDE * DISPLAY_HEIGHT];
    struct GsLineState after;

    for (int i = 0; i < DISPLAY_HEIGHT; i++)
    {
        BeginLine(i);
        SaveLineRegs(&lines[i]);
        EndLine();
    }
    if (PS2GS_CanDraw(lines, (const uint8_t *)VRAM_, (const uint16_t *)OAM))
    {
        PS2GS_DrawLines(lines, (const uint8_t *)VRAM_, (const uint16_t *)PLTT, (const uint16_t *)OAM);
        return;
    }
    SaveLineRegs(&after);
    for (int i = 0; i < DISPLAY_HEIGHT; i++)
    {
        LoadLineRegs(&lines[i]);
        DrawLine(&image[i * RENDER_STRIDE], i);
    }
    LoadLineRegs(&after);
    FinishFades(image);
    PS2GS_DrawImage(image);
}
#endif // __PS2__
#endif // PORTABLE
