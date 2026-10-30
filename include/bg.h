#ifndef GUARD_BG_H
#define GUARD_BG_H

#include "global.h"

struct BGCntrlBitfield // for the I/O registers
{
    volatile u16 priority:2;
    volatile u16 charBaseBlock:2;
    volatile u16 field_0_2:4;
    volatile u16 field_1_0:5;
    volatile u16 areaOverflowMode:1;
    volatile u16 screenSize:2;
};

enum
{
    BG_CTRL_ATTR_VISIBLE = 1,
    BG_CTRL_ATTR_CHARBASEINDEX = 2,
    BG_CTRL_ATTR_MAPBASEINDEX = 3,
    BG_CTRL_ATTR_SCREENSIZE = 4,
    BG_CTRL_ATTR_PALETTEMODE = 5,
    BG_CTRL_ATTR_PRIORITY = 6,
    BG_CTRL_ATTR_MOSAIC = 7,
    BG_CTRL_ATTR_WRAPAROUND = 8,
};

enum
{
    BG_ATTR_CHARBASEINDEX = 1,
    BG_ATTR_MAPBASEINDEX = 2,
    BG_ATTR_SCREENSIZE = 3,
    BG_ATTR_PALETTEMODE = 4,
    BG_ATTR_MOSAIC = 5,
    BG_ATTR_WRAPAROUND = 6,
    BG_ATTR_PRIORITY = 7,
    BG_ATTR_MAPSIZE = 8,
    BG_ATTR_BGTYPE = 9,
    BG_ATTR_BASETILE = 10
};

// Modes for ChangeBgX / ChangeBgY
enum {
    BG_COORD_SET,
    BG_COORD_ADD,
    BG_COORD_SUB,
};

enum AdjustBgMosaicMode
{
    BG_MOSAIC_SET,
    BG_MOSAIC_SET_H,
    BG_MOSAIC_INC_H,
    BG_MOSAIC_DEC_H,
    BG_MOSAIC_SET_V,
    BG_MOSAIC_INC_V,
    BG_MOSAIC_DEC_V,
};

enum BgTileAllocMode
{
    BG_TILE_FIND_FREE_SPACE,
    BG_TILE_ALLOC,
    BG_TILE_FREE,
};

struct BgTemplate
{
    u16 bg:2;                   // 0x1, 0x2 -> 0x3
    u16 charBaseIndex:2;        // 0x4, 0x8 -> 0xC
    u16 mapBaseIndex:5;         // 0x10, 0x20, 0x40, 0x80, 0x100 -> 0x1F0
    u16 screenSize:2;           // 0x200, 0x400 -> 0x600
    u16 paletteMode:1;          // 0x800
    u16 priority:2;             // 0x1000, 0x2000 > 0x3000
    u16 baseTile:10;
};

void ResetBgs(void);
u8 GetBgMode(void);
void ResetBgControlStructs(void);
void Unused_ResetBgControlStruct(u8 bg);
void SetBgControlAttributes(u8 bg, u8 charBaseIndex, u8 mapBaseIndex, u8 screenSize, u8 paletteMode, u8 priority, u8 mosaic, u8 wraparound);
u16 GetBgControlAttribute(u8 bg, u8 attributeId);
u8 LoadBgVram(u8 bg, const void *src, u16 size, u16 destOffset, u8 mode);
void SetTextModeAndHideBgs(void);
bool8 IsInvalidBg(u8 bg);
int BgTileAllocOp(int bg, int offset, int count, int mode);
void ResetBgsAndClearDma3BusyFlags(bool32 enableWindowTileAutoAlloc);
void InitBgsFromTemplates(u8 bgMode, const struct BgTemplate *templates, u8 numTemplates);
void InitBgFromTemplate(const struct BgTemplate *template);
void SetBgMode(u8 bgMode);
u16 LoadBgTiles(u8 bg, const void *src, u16 size, u16 destOffset);
u16 LoadBgTilemap(u8 bg, const void *src, u16 size, u16 destOffset);
u16 Unused_LoadBgPalette(u8 bg, const void *src, u16 size, u16 destOffset);
bool8 IsDma3ManagerBusyWithBgCopy(void);
void ShowBg(u8 bg);
void HideBg(u8 bg);
void SetBgAttribute(u8 bg, u8 attributeId, u8 value);
u16 GetBgAttribute(u8 bg, u8 attributeId);
u32 ChangeBgX(u8 bg, u32 value, u8 op);
u32 GetBgX(u8 bg);
u32 ChangeBgY(u8 bg, u32 value, u8 op);
u32 ChangeBgY_ScreenOff(u8 bg, u32 value, u8 op);
u32 GetBgY(u8 bg);
void SetBgAffine(u8 bg, u32 srcCenterX, u32 srcCenterY, s16 dispCenterX, s16 dispCenterY, s16 scaleX, s16 scaleY, u16 rotationAngle);
u8 AdjustBgMosaic(u8 value, u8 mode);
void SetBgTilemapBuffer(u8 bg, void *tilemap);
void UnsetBgTilemapBuffer(u8 bg);
void *GetBgTilemapBuffer(u8 bg);
void CopyToBgTilemapBuffer(u8 bg, const void *src, u16 mode, u16 destOffset);
void CopyBgTilemapBufferToVram(u8 bg);
void CopyToBgTilemapBufferRect(u8 bg, const void *src, u8 destX, u8 destY, u8 width, u8 height);
void CopyToBgTilemapBufferRect_ChangePalette(u8 bg, const void *src, u8 destX, u8 destY, u8 rectWidth, u8 rectHeight, u8 palette);
void CopyRectToBgTilemapBufferRect(u8 bg, const void *src, u8 srcX, u8 srcY, u8 srcWidth, u8 srcHeight, u8 destX, u8 destY, u8 rectWidth, u8 rectHeight, u8 palette1, s16 tileOffset, s16 palette2);
void FillBgTilemapBufferRect_Palette0(u8 bg, u16 tileNum, u8 x, u8 y, u8 width, u8 height);
void FillBgTilemapBufferRect(u8 bg, u16 tileNum, u8 x, u8 y, u8 width, u8 height, u8 palette);
void WriteSequenceToBgTilemapBuffer(u8 bg, u16 firstTileNum, u8 x, u8 y, u8 width, u8 height, u8 paletteSlot, s16 tileNumDelta);
u16 GetBgMetricTextMode(u8 bg, u8 whichMetric);
u32 GetBgMetricAffineMode(u8 bg, u8 whichMetric);
u32 GetTileMapIndexFromCoords(s32 x, s32 y, s32 screenSize, u32 screenWidth, u32 screenHeight);
void CopyTileMapEntry(const u16 *src, u16 *dest, s32 palette1, s32 tileOffset, s32 palette2);
u32 GetBgType(u8 bg);
bool32 IsInvalidBg32(u8 bg);
bool32 IsTileMapOutsideWram(u8 bg);

extern bool32 gWindowTileAutoAllocEnabled;

#ifdef PORTABLE
extern bool8 gWideViewMargins;
// Backgrounds (bits 0-3) whose narrower maps are made to repeat, so they fill
// the margins from their own next columns
extern u8 gWideViewRepeatingBgs;
// Show the GBA screen's middle twice the size instead, for letterboxed scenes
// that can't be widened
extern bool8 gWideViewZoom;
// Backgrounds (bits 0-3) whose art stops at the GBA screen's edges, like the
// title's Charizard: the margins show what's behind them instead of carrying
// their edge on
extern u8 gWideViewEndsAtEdgeBgs;
// Backgrounds (bits 0-3) that fill the margins with their art's first and last
// columns, moving with their scroll, like the title's copyright bar sliding in
extern u8 gWideViewClampBgs;
// Backgrounds (bits 0-3) whose 256-pixel map shows in the margins only where
// it carries on without wrapping round, like the intro's Gengar: the rest of
// him past the screen's left edge shows, but not his own left side again
// past the right edge
extern u8 gWideViewNoWrapBgs;
// Backgrounds (bits 0-3) that fill only the left margin from their map, like
// the shop's view of the room, which has one more column of the room drawn in
// the 16 pixels left of the screen (further left carries that column's first
// pixels on); the right margin carries on their edge as usual
extern u8 gWideViewLeftOnlyBgs;
// Backgrounds (bits 0-3) whose maps don't fill the margins however wide they
// are (unless made to repeat), like the battle's BG1 and BG2, whose maps past
// the GBA's screen hold whatever was left there
extern u8 gWideViewNarrowBgs;
// Art that carries on past the GBA screen's left and right edges, like the
// Town Map's rollers, of which the GBA shows the inner halves: for each side,
// a 32-pixel-wide picture (color indexes, 0 clear) for every line, starting at
// game column gWideViewEdgeArtX[side]. In the margins its pixels take the
// colors the same indexes have on the screen where the art overlaps it, so
// they match (fades included). Elsewhere the margins take the color at game
// column gWideViewEdgeBarX on the first gWideViewEdgeBarLines lines (a bar
// across the top) while gWideViewEdgeArtShown, and are black otherwise.
extern const u8 (*gWideViewEdgeArt)[DISPLAY_HEIGHT][32];
extern s16 gWideViewEdgeArtX[2];
extern u8 gWideViewEdgeBarLines;
extern u8 gWideViewEdgeBarX;
extern bool8 gWideViewEdgeArtShown;
// The bar's gone (the Town Map closing): the margins keep its last color,
// brightened as the screen whitens (BLDY)
extern bool8 gWideViewEdgeBarFading;
// How bright the margins are, 0 (black) to 16 (as drawn): a full-screen
// picture over the field, like Viridian Forest's on the way in, keeps them
// dark while it shows and lets them come up as it fades
extern u8 gWideViewMarginLevel;
// Sprites reach into the margins, which stay black behind them instead of
// carrying on the screen's edges (and gWideViewEdgeArt waits, unless it's
// shown): like the Town Map's rollers while they're sprites, moving in or out
extern bool8 gWideViewSpritesInMargins;
#endif

#endif // GUARD_BG_H
