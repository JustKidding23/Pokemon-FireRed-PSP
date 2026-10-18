#include "global.h"
#include "gflib.h"
#include "field_camera.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "event_object_movement.h"
#include "new_menu_helpers.h"
#include "overworld.h"

EWRAM_DATA bool8 gBikeCameraAheadPanback = FALSE;

// Static type declarations
struct FieldCameraOffset
{
#ifdef WIDE_OVERWORLD
    // The wide backgrounds are 512 pixels across, which a u8 can't count to
    u16 xPixelOffset;
#else
    u8 xPixelOffset;
#endif
    u8 yPixelOffset;
    u8 xTileOffset;
    u8 yTileOffset;
    bool8 copyBGToVRAM;
};

// static functions
static void RedrawMapSliceNorth(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout);
static void RedrawMapSliceSouth(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout);
static void RedrawMapSliceEast(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout);
static void RedrawMapSliceWest(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout);
static s32 MapPosToBgTilemapOffset(struct FieldCameraOffset *a, s32 x, s32 y);
static void DrawWholeMapViewInternal(int x, int y, const struct MapLayout *mapLayout);
static void DrawMetatileAt(const struct MapLayout *mapLayout, u16, int, int);
static void DrawMetatile(s32 a, const u16 *b, u16 c);
static void CameraPanningCB_PanAhead(void);

// IWRAM bss vars
static struct FieldCameraOffset sFieldCameraOffset;
static s16 sHorizontalCameraPan;
static s16 sVerticalCameraPan;
static u8 sBikeCameraPanFlag;
static void (*sFieldCameraPanningCallback)(void);

COMMON_DATA struct CameraObject gFieldCamera = {0};
COMMON_DATA u16 gTotalCameraPixelOffsetY = 0;
COMMON_DATA u16 gTotalCameraPixelOffsetX = 0;

// Where a tile of the overworld's backgrounds is in their tilemaps. A 64x32
// map is two 32x32 screenblocks side by side, so columns 32 to 63 are 0x400
// entries on, and rows stay 32 apart. (from pokeemerald-dualscreen)
static inline u32 BgTilemapIndex(u32 x, u32 y)
{
    return (x >= 32 ? 0x400 : 0) + y * 32 + (x & 31);
}

// The map column at the left of the backgrounds: MAP_FILL_EXTRA_X metatiles
// left of the GBA's view
static inline int GetMapViewOriginX(void)
{
    return gSaveBlock1Ptr->pos.x - (int)GetMapFillExtraX();
}

// text
static void move_tilemap_camera_to_upper_left_corner_(struct FieldCameraOffset *cameraOffset)
{
    cameraOffset->xTileOffset = 0;
    cameraOffset->yTileOffset = 0;
    cameraOffset->xPixelOffset = 0;
    cameraOffset->yPixelOffset = 0;
    cameraOffset->copyBGToVRAM = TRUE;
}

static void tilemap_move_something(struct FieldCameraOffset *cameraOffset, u32 b, u32 c)
{
    cameraOffset->xTileOffset += b;
    cameraOffset->xTileOffset %= GetBgMapTilesX();
    cameraOffset->yTileOffset += c;
    cameraOffset->yTileOffset %= 32;
}

static void coords8_add(struct FieldCameraOffset *cameraOffset, u32 b, u32 c)
{
    cameraOffset->xPixelOffset += b;
#ifdef WIDE_OVERWORLD
    cameraOffset->xPixelOffset %= GetBgMapTilesX() * 8;
#endif
    cameraOffset->yPixelOffset += c;
}

void move_tilemap_camera_to_upper_left_corner(void)
{
    move_tilemap_camera_to_upper_left_corner_(&sFieldCameraOffset);
}

void FieldUpdateBgTilemapScroll(void)
{
    u32 r4, r5;
    // Past the columns drawn left of the GBA's view, so that it stays where
    // the sprites and collision are
    r5 = sFieldCameraOffset.xPixelOffset + sHorizontalCameraPan + GetMapFillExtraX() * 16;
    r4 = sVerticalCameraPan + sFieldCameraOffset.yPixelOffset + 8;

    SetGpuReg(REG_OFFSET_BG1HOFS, r5);
    SetGpuReg(REG_OFFSET_BG1VOFS, r4);
    SetGpuReg(REG_OFFSET_BG2HOFS, r5);
    SetGpuReg(REG_OFFSET_BG2VOFS, r4);
    SetGpuReg(REG_OFFSET_BG3HOFS, r5);
    SetGpuReg(REG_OFFSET_BG3VOFS, r4);
}

void FieldCameraGetPixelOffsetAtGround(s16 *hofs_p, s16 *vofs_p)
{
    *hofs_p = sFieldCameraOffset.xPixelOffset + sHorizontalCameraPan + GetMapFillExtraX() * 16;
    *vofs_p = sFieldCameraOffset.yPixelOffset + sVerticalCameraPan + 8;
}

void DrawWholeMapView(void)
{
    DrawWholeMapViewInternal(gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y, gMapHeader.mapLayout);
   // sFieldCameraOffset.copyBGToVRAM = TRUE;
}

static void DrawWholeMapViewInternal(int x, int y, const struct MapLayout *mapLayout)
{
    u8 i;
    u8 j;
    u32 r6;
    u8 temp;
    u32 mapTilesX = GetBgMapTilesX();
    int originX = x - (int)GetMapFillExtraX();

    for (i = 0; i < 32; i += 2)
    {
        temp = sFieldCameraOffset.yTileOffset + i;
        if (temp >= 32)
            temp -= 32;
        r6 = temp;
        for (j = 0; j < mapTilesX; j += 2)
        {
            temp = sFieldCameraOffset.xTileOffset + j;
            if (temp >= mapTilesX)
                temp -= mapTilesX;
            DrawMetatileAt(mapLayout, BgTilemapIndex(temp, r6), originX + j / 2, y + i / 2);
        }
    }
}

static void RedrawMapSlicesForCameraUpdate(struct FieldCameraOffset *cameraOffset, int x, int y)
{
    const struct MapLayout *mapLayout = gMapHeader.mapLayout;

    if (x > 0)
        RedrawMapSliceWest(cameraOffset, mapLayout);
    if (x < 0)
        RedrawMapSliceEast(cameraOffset, mapLayout);
    if (y > 0)
        RedrawMapSliceNorth(cameraOffset, mapLayout);
    if (y < 0)
        RedrawMapSliceSouth(cameraOffset, mapLayout);
    cameraOffset->copyBGToVRAM = TRUE;
}

static void RedrawMapSliceNorth(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout)
{
    u8 i;
    u8 temp;
    u32 r7;

    u32 mapTilesX = GetBgMapTilesX();

    temp = cameraOffset->yTileOffset + 28;
    if (temp >= 32)
        temp -= 32;
    r7 = temp;
    for (i = 0; i < mapTilesX; i += 2)
    {
        temp = cameraOffset->xTileOffset + i;
        if (temp >= mapTilesX)
            temp -= mapTilesX;
        DrawMetatileAt(mapLayout, BgTilemapIndex(temp, r7), GetMapViewOriginX() + i / 2, gSaveBlock1Ptr->pos.y + 14);
    }
}

static void RedrawMapSliceSouth(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout)
{
    u8 i;
    u8 temp;
    u32 r7 = cameraOffset->yTileOffset;
    u32 mapTilesX = GetBgMapTilesX();

    for (i = 0; i < mapTilesX; i += 2)
    {
        temp = cameraOffset->xTileOffset + i;
        if (temp >= mapTilesX)
            temp -= mapTilesX;
        DrawMetatileAt(mapLayout, BgTilemapIndex(temp, r7), GetMapViewOriginX() + i / 2, gSaveBlock1Ptr->pos.y);
    }
}

static void RedrawMapSliceEast(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout)
{
    u8 i;
    u8 temp;
    u32 r6 = cameraOffset->xTileOffset;

    for (i = 0; i < 32; i += 2)
    {
        temp = cameraOffset->yTileOffset + i;
        if (temp >= 32)
            temp -= 32;
        DrawMetatileAt(mapLayout, BgTilemapIndex(r6, temp), GetMapViewOriginX(), gSaveBlock1Ptr->pos.y + i / 2);
    }
}

static void RedrawMapSliceWest(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout)
{
    u8 i;
    u8 temp;
    // The backgrounds' last metatile column, the one that just came into view
    u32 mapTilesX = GetBgMapTilesX();
    u32 lastColumn = mapTilesX / 2 - 1;
    u8 r5 = cameraOffset->xTileOffset + lastColumn * 2;

    if (r5 >= mapTilesX)
        r5 -= mapTilesX;
    for (i = 0; i < 32; i += 2)
    {
        temp = cameraOffset->yTileOffset + i;
        if (temp >= 32)
            temp -= 32;
        DrawMetatileAt(mapLayout, BgTilemapIndex(r5, temp), GetMapViewOriginX() + lastColumn, gSaveBlock1Ptr->pos.y + i / 2);
    }
}

void CurrentMapDrawMetatileAt(int x, int y)
{
    int offset = MapPosToBgTilemapOffset(&sFieldCameraOffset, x, y);

    if (offset >= 0)
    {
        DrawMetatileAt(gMapHeader.mapLayout, offset, x, y);
       // sFieldCameraOffset.copyBGToVRAM = TRUE;
    }
}

void DrawDoorMetatileAt(int x, int y, const u16 *tiles)
{
    int offset = MapPosToBgTilemapOffset(&sFieldCameraOffset, x, y);

    if (offset >= 0)
    {
        DrawMetatile(1, tiles, offset);
       // sFieldCameraOffset.copyBGToVRAM = TRUE;
    }
}

static void DrawMetatileAt(const struct MapLayout *mapLayout, u16 offset, int x, int y)
{
    u16 metatileId = MapGridGetMetatileIdForDraw(x, y);
    const u16 *metatiles;

    if (metatileId > NUM_METATILES_TOTAL)
        metatileId = 0;
    if (metatileId < NUM_METATILES_IN_PRIMARY)
        metatiles = mapLayout->primaryTileset->metatiles;
    else
    {
        metatiles = mapLayout->secondaryTileset->metatiles;
        metatileId -= NUM_METATILES_IN_PRIMARY;
    }
    DrawMetatile(MapGridGetMetatileLayerTypeForDraw(x, y), metatiles + metatileId * NUM_TILES_PER_METATILE, offset);
}

static void DrawMetatile(s32 metatileLayerType, const u16 *tiles, u16 offset)
{
    switch (metatileLayerType)
    {
    case METATILE_LAYER_TYPE_SPLIT:
        // Draw metatile's bottom layer to the bottom background layer.
        gBGTilemapBuffers3[offset] = tiles[0];
        gBGTilemapBuffers3[offset + 1] = tiles[1];
        gBGTilemapBuffers3[offset + 0x20] = tiles[2];
        gBGTilemapBuffers3[offset + 0x21] = tiles[3];

        // Draw transparent tiles to the middle background layer.
        gBGTilemapBuffers1[offset] = 0;
        gBGTilemapBuffers1[offset + 1] = 0;
        gBGTilemapBuffers1[offset + 0x20] = 0;
        gBGTilemapBuffers1[offset + 0x21] = 0;

        // Draw metatile's top layer to the top background layer.
        gBGTilemapBuffers2[offset] = tiles[4];
        gBGTilemapBuffers2[offset + 1] = tiles[5];
        gBGTilemapBuffers2[offset + 0x20] = tiles[6];
        gBGTilemapBuffers2[offset + 0x21] = tiles[7];
        break;
    case METATILE_LAYER_TYPE_COVERED:
        // Draw metatile's bottom layer to the bottom background layer.
        gBGTilemapBuffers3[offset] = tiles[0];
        gBGTilemapBuffers3[offset + 1] = tiles[1];
        gBGTilemapBuffers3[offset + 0x20] = tiles[2];
        gBGTilemapBuffers3[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the middle background layer.
        gBGTilemapBuffers1[offset] = tiles[4];
        gBGTilemapBuffers1[offset + 1] = tiles[5];
        gBGTilemapBuffers1[offset + 0x20] = tiles[6];
        gBGTilemapBuffers1[offset + 0x21] = tiles[7];

        // Draw transparent tiles to the top background layer.
        gBGTilemapBuffers2[offset] = 0;
        gBGTilemapBuffers2[offset + 1] = 0;
        gBGTilemapBuffers2[offset + 0x20] = 0;
        gBGTilemapBuffers2[offset + 0x21] = 0;
        break;
    case METATILE_LAYER_TYPE_NORMAL:
        // Draw garbage to the bottom background layer.
        gBGTilemapBuffers3[offset] = 0x3014;
        gBGTilemapBuffers3[offset + 1] = 0x3014;
        gBGTilemapBuffers3[offset + 0x20] = 0x3014;
        gBGTilemapBuffers3[offset + 0x21] = 0x3014;

        // Draw metatile's bottom layer to the middle background layer.
        gBGTilemapBuffers1[offset] = tiles[0];
        gBGTilemapBuffers1[offset + 1] = tiles[1];
        gBGTilemapBuffers1[offset + 0x20] = tiles[2];
        gBGTilemapBuffers1[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the top background layer, which covers object event sprites.
        gBGTilemapBuffers2[offset] = tiles[4];
        gBGTilemapBuffers2[offset + 1] = tiles[5];
        gBGTilemapBuffers2[offset + 0x20] = tiles[6];
        gBGTilemapBuffers2[offset + 0x21] = tiles[7];
        break;
    }
    ScheduleBgCopyTilemapToVram(1);
    ScheduleBgCopyTilemapToVram(2);
    ScheduleBgCopyTilemapToVram(3);
}

static s32 MapPosToBgTilemapOffset(struct FieldCameraOffset *cameraOffset, s32 x, s32 y)
{
    s32 mapTilesX = GetBgMapTilesX();

    x -= GetMapViewOriginX();
    x *= 2;
    if (x >= mapTilesX || x < 0)
        return -1;
    x = x + cameraOffset->xTileOffset;
    if (x >= mapTilesX)
        x -= mapTilesX;

    y = (y - gSaveBlock1Ptr->pos.y) * 2;
    if (y >= 32 || y < 0)
        return -1;
    y = y + cameraOffset->yTileOffset;
    if (y >= 32)
        y -= 32;

    return BgTilemapIndex(x, y);
}

static void CameraUpdateCallback(struct CameraObject *fieldCamera)
{
    if (fieldCamera->spriteId != 0)
    {
        fieldCamera->movementSpeedX = gSprites[fieldCamera->spriteId].data[2];
        fieldCamera->movementSpeedY = gSprites[fieldCamera->spriteId].data[3];
    }
}

void ResetCameraUpdateInfo(void)
{
    gFieldCamera.movementSpeedX = 0;
    gFieldCamera.movementSpeedY = 0;
    gFieldCamera.x = 0;
    gFieldCamera.y = 0;
    gFieldCamera.spriteId = 0;
    gFieldCamera.callback = NULL;
}

u32 InitCameraUpdateCallback(u8 trackedSpriteId)
{
    if (gFieldCamera.spriteId != 0)
        DestroySprite(&gSprites[gFieldCamera.spriteId]);
    gFieldCamera.spriteId = AddCameraObject(trackedSpriteId);
    gFieldCamera.callback = CameraUpdateCallback;
    return 0;
}

void CameraUpdate(void)
{
    int deltaX;
    int deltaY;
    int curMovementOffsetY;
    int curMovementOffsetX;
    int movementSpeedX;
    int movementSpeedY;

    if (gFieldCamera.callback != NULL)
        gFieldCamera.callback(&gFieldCamera);
    movementSpeedX = gFieldCamera.movementSpeedX;
    movementSpeedY = gFieldCamera.movementSpeedY;
    deltaX = 0;
    deltaY = 0;
    curMovementOffsetX = gFieldCamera.x;
    curMovementOffsetY = gFieldCamera.y;


    if (curMovementOffsetX == 0 && movementSpeedX != 0)
    {
        if (movementSpeedX > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }
    if (curMovementOffsetY == 0 && movementSpeedY != 0)
    {
        if (movementSpeedY > 0)
            deltaY = 1;
        else
            deltaY = -1;
    }
    if (curMovementOffsetX != 0 && curMovementOffsetX == -movementSpeedX)
    {
        if (movementSpeedX > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }
    if (curMovementOffsetY != 0 && curMovementOffsetY == -movementSpeedY)
    {
        if (movementSpeedY > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }

    gFieldCamera.x += movementSpeedX;
    gFieldCamera.x = gFieldCamera.x - 16 * (gFieldCamera.x / 16);
    gFieldCamera.y += movementSpeedY;
    gFieldCamera.y = gFieldCamera.y - 16 * (gFieldCamera.y / 16);

    if (deltaX != 0 || deltaY != 0)
    {
#ifdef WIDE_OVERWORLD
        u8 mapGroup = gSaveBlock1Ptr->location.mapGroup;
        u8 mapNum = gSaveBlock1Ptr->location.mapNum;
#endif
        CameraMove(deltaX, deltaY);
        UpdateObjectEventsForCameraUpdate(deltaX, deltaY);
        // RotatingGatePuzzleCameraUpdate(deltaX, deltaY);
        // ResetBerryTreeSparkleFlags();
        tilemap_move_something(&sFieldCameraOffset, deltaX * 2, deltaY * 2);
        RedrawMapSlicesForCameraUpdate(&sFieldCameraOffset, deltaX * 2, deltaY * 2);
#ifdef WIDE_OVERWORLD
        // Walking into a connected map builds the VMap from it, which holds
        // less of the old map than the wide view showed, so the columns far
        // from the view would keep the old map's border. Draw it all again.
        if (mapGroup != gSaveBlock1Ptr->location.mapGroup || mapNum != gSaveBlock1Ptr->location.mapNum)
            DrawWholeMapView();
#endif
    }

    coords8_add(&sFieldCameraOffset, movementSpeedX, movementSpeedY);
    gTotalCameraPixelOffsetX -= movementSpeedX;
    gTotalCameraPixelOffsetY -= movementSpeedY;
}

void MoveCameraAndRedrawMap(int deltaX, int deltaY) // unused
{
    CameraMove(deltaX, deltaY);
    UpdateObjectEventsForCameraUpdate(deltaX, deltaY);
    DrawWholeMapView();
    gTotalCameraPixelOffsetX -= deltaX * 16;
    gTotalCameraPixelOffsetY -= deltaY * 16;
}

void CameraUpdateNoObjectRefresh(void)
{
    int deltaX;
    int deltaY;
    int curMovementOffsetY;
    int curMovementOffsetX;
    int movementSpeedX;
    int movementSpeedY;

    if (gFieldCamera.callback != NULL)
        gFieldCamera.callback(&gFieldCamera);
    movementSpeedX = gFieldCamera.movementSpeedX;
    movementSpeedY = gFieldCamera.movementSpeedY;
    deltaX = 0;
    deltaY = 0;
    curMovementOffsetX = gFieldCamera.x;
    curMovementOffsetY = gFieldCamera.y;


    if (curMovementOffsetX == 0 && movementSpeedX != 0)
    {
        if (movementSpeedX > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }
    if (curMovementOffsetY == 0 && movementSpeedY != 0)
    {
        if (movementSpeedY > 0)
            deltaY = 1;
        else
            deltaY = -1;
    }
    if (curMovementOffsetX != 0 && curMovementOffsetX == -movementSpeedX)
    {
        if (movementSpeedX > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }
    if (curMovementOffsetY != 0 && curMovementOffsetY == -movementSpeedY)
    {
        if (movementSpeedY > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }

    gFieldCamera.x += movementSpeedX;
    gFieldCamera.x = gFieldCamera.x - 16 * (gFieldCamera.x / 16);
    gFieldCamera.y += movementSpeedY;
    gFieldCamera.y = gFieldCamera.y - 16 * (gFieldCamera.y / 16);

    if (deltaX != 0 || deltaY != 0)
    {
        CameraMove(deltaX, deltaY);
        // UpdateObjectEventsForCameraUpdate(deltaX, deltaY);
        // RotatingGatePuzzleCameraUpdate(deltaX, deltaY);
        // ResetBerryTreeSparkleFlags();
        tilemap_move_something(&sFieldCameraOffset, deltaX * 2, deltaY * 2);
        RedrawMapSlicesForCameraUpdate(&sFieldCameraOffset, deltaX * 2, deltaY * 2);
    }

    coords8_add(&sFieldCameraOffset, movementSpeedX, movementSpeedY);
    // gTotalCameraPixelOffsetX -= movementSpeedX;
    // gTotalCameraPixelOffsetY -= movementSpeedY;
}

void SetCameraPanningCallback(void (*a)(void))
{
    sFieldCameraPanningCallback = a;
}

void SetCameraPanning(s16 a, s16 b)
{
    sHorizontalCameraPan = a;
    sVerticalCameraPan = b + 32;
}

void InstallCameraPanAheadCallback(void)
{
    sFieldCameraPanningCallback = CameraPanningCB_PanAhead;
    sBikeCameraPanFlag = FALSE;
    sHorizontalCameraPan = 0;
    sVerticalCameraPan = 32;
}

void UpdateCameraPanning(void)
{
    if (sFieldCameraPanningCallback != NULL)
        sFieldCameraPanningCallback();
    // Update sprite offset of overworld objects
    gSpriteCoordOffsetX = gTotalCameraPixelOffsetX - sHorizontalCameraPan;
    gSpriteCoordOffsetY = gTotalCameraPixelOffsetY - sVerticalCameraPan - 8;
}

static void CameraPanningCB_PanAhead(void)
{
    u8 var;

    if (gBikeCameraAheadPanback == FALSE)
    {
        InstallCameraPanAheadCallback();
    }
    else
    {
        // this code is never reached.
        if (gPlayerAvatar.tileTransitionState == 1)
        {
            sBikeCameraPanFlag ^= 1;
            if (sBikeCameraPanFlag == FALSE)
                return;
        }
        else
        {
            sBikeCameraPanFlag = FALSE;
        }

        var = GetPlayerMovementDirection();
        if (var == 2)
        {
            if (sVerticalCameraPan > -8)
                sVerticalCameraPan -= 2;
        }
        else if (var == 1)
        {
            if (sVerticalCameraPan < 72)
                sVerticalCameraPan += 2;
        }
        else if (sVerticalCameraPan < 32)
        {
            sVerticalCameraPan += 2;
        }
        else if (sVerticalCameraPan > 32)
        {
            sVerticalCameraPan -= 2;
        }
    }
}
