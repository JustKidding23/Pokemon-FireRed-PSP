#ifndef GUARD_FIELDMAP_H
#define GUARD_FIELDMAP_H

#include "global.h"

#define NUM_TILES_IN_PRIMARY 640
#define NUM_TILES_TOTAL 1024
#define NUM_METATILES_IN_PRIMARY 640
#define NUM_METATILES_TOTAL 1024
#define NUM_PALS_IN_PRIMARY 7
#define NUM_PALS_TOTAL 13
#define MAX_MAP_DATA_SIZE 0x2800
#define VIRTUAL_MAP_SIZE (MAX_MAP_DATA_SIZE)

#define NUM_TILES_PER_METATILE 8

// Map coordinates are offset by 7 when using the map
// buffer because it needs to load sufficient border
// metatiles to fill the player's view (the player has
// 7 metatiles of view horizontally in either direction).
#define MAP_OFFSET 7
#define MAP_OFFSET_W (MAP_OFFSET * 2 + 1)
#define MAP_OFFSET_H (MAP_OFFSET * 2)

#if defined(PORTABLE) && defined(__PSP__)
// The PSP's wider view (see platform/framedraw.h) draws the overworld on
// 64x32-tile backgrounds, with MAP_FILL_EXTRA_X metatiles drawn left of the
// GBA's view. Everything else, the camera's position, collision, events and
// the save, stays the GBA's. From pokeemerald-dualscreen's widescreen.
#define WIDE_OVERWORLD 1
#define BG_MAP_TILES_X 64
#define MAP_FILL_EXTRA_X 8
// The columns the wider view shows past each side of the GBA's (RENDER_MARGIN
// in platform/framedraw.h), where sprites need to show too
#define WIDE_VIEW_MARGIN 21
// Objects are loaded and kept this many metatiles further out each side, so
// they come and go out of sight even when they wander from their spot
#define WIDE_VIEW_EXTRA_METATILES 5
#else
#define WIDE_VIEW_MARGIN 0
#define WIDE_VIEW_EXTRA_METATILES 0
#define BG_MAP_TILES_X 32
#define MAP_FILL_EXTRA_X 0
#endif
#define BG_MAP_TILES_Y 32
#define OVERWORLD_BG_TILEMAP_SCREENBLOCKS (BG_MAP_TILES_X / 32)

extern struct BackupMapLayout VMap;
extern const struct MapLayout Route1_Layout;

u32 MapGridGetMetatileIdAt(s32, s32);
bool8 UseWideOverworldBg(void);
u32 GetBgMapTilesX(void);
u32 GetMapFillExtraX(void);
u32 MapGridGetMetatileIdForDraw(s32 x, s32 y);
u8 MapGridGetMetatileLayerTypeForDraw(s32 x, s32 y);
u32 MapGridGetMetatileBehaviorAt(s16, s16);
u8 MapGridGetMetatileLayerTypeAt(s16 x, s16 y);
void MapGridSetMetatileIdAt(s32, s32, u16);
void MapGridSetMetatileEntryAt(s32, s32, u16);
u8 MapGridGetElevationAt(s32 x, s32 y);
void GetCameraCoords(u16 *, u16 *);
bool8 MapGridGetCollisionAt(s32, s32);
s32 GetMapBorderIdAt(s32, s32);
bool32 CanCameraMoveInDirection(s32);
const struct MapHeader * GetMapHeaderFromConnection(const struct MapConnection * connection);
const struct MapConnection * GetMapConnectionAtPos(s16 x, s16 y);
void ApplyGlobalTintToPaletteSlot(u8 slot, u8 count);
void SaveMapView(void);
u32 ExtractMetatileAttribute(u32 attributes, u8 attributeType);
u32 MapGridGetMetatileAttributeAt(s16 x, s16 y, u8 attributeType);
void MapGridSetMetatileImpassabilityAt(s32 x, s32 y, bool32 arg2);
bool8 CameraMove(s32 x, s32 y);
void CopyMapTilesetsToVram(struct MapLayout const * mapLayout);
void LoadMapTilesetPalettes(struct MapLayout const * mapLayout);
void InitMap(void);
void CopySecondaryTilesetToVramUsingHeap(const struct MapLayout * mapLayout);
void LoadSecondaryTilesetPalette(const struct MapLayout * mapLayout);
void InitMapFromSavedGame(void);
void CopyPrimaryTilesetToVram(const struct MapLayout *mapLayout);
void CopySecondaryTilesetToVram(const struct MapLayout *mapLayout);
void GetCameraFocusCoords(u16 *x, u16 *y);
void SetCameraFocusCoords(u16 x, u16 y);

#endif //GUARD_FIELDMAP_H
