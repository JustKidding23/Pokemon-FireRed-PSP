#include "global.h"
#include "save.h"
#include "decompress.h"
#include "overworld.h"
#include "load_save.h"
#include "task.h"
#include "link.h"
#include "save_failed_screen.h"
#include "fieldmap.h"
#include "pokemon_storage_system.h"
#include "gba/flash_internal.h"
#include "sloopsvc.h"
#ifdef PORTABLE
#include "platform.h"
#endif

static u8 HandleWriteSector(u16 sectorId, const struct SaveSectorLocation *locations);
static u8 TryWriteSector(u8 sectorNum, u8 *data);
static u8 HandleReplaceSector(u16 sectorId, const struct SaveSectorLocation *locations);
static u8 CopySaveSlotData(u16 sectorId, const struct SaveSectorLocation *locations);
static u8 GetSaveValidStatus(const struct SaveSectorLocation *locations);
static u8 ReadFlashSector(u8 sectorId, struct SaveSector *sector);
static u16 CalculateChecksum(void *data, u16 size);

/*
 * Sector Layout:
 *
 * Sectors 0 - 13:      Save Slot 1
 * Sectors 14 - 27:     Save Slot 2
 * Sectors 28 - 29:     Hall of Fame
 * Sectors 30 - 31:     Trainer Tower
 *
 * There are two save slots for saving the player's game data. We alternate between
 * them each time the game is saved, so that if the current save slot is corrupt,
 * we can load the previous one. We also rotate the sectors in each save slot
 * so that the same data is not always being written to the same sector. This
 * might be done to reduce wear on the flash memory, but I'm not sure, since all
 * 14 sectors get written anyway.
 *
 * See SECTOR_ID_* constants in save.h
 */

// (u8 *)structure was removed from the first statement of the macro in Emerald
// and Fire Red/Leaf Green. This is because malloc is used to allocate addresses
// so storing the raw addresses should not be done in the offsets information.
#define SAVEBLOCK_CHUNK(structure, chunkNum)                                   \
{                                                                              \
    chunkNum * SECTOR_DATA_SIZE,                                               \
    sizeof(structure) >= chunkNum * SECTOR_DATA_SIZE ?                         \
    min(sizeof(structure) - chunkNum * SECTOR_DATA_SIZE, SECTOR_DATA_SIZE) : 0 \
}

struct
{
    u16 offset;
    u16 size;
} static const sSaveSlotLayout[NUM_SECTORS_PER_SLOT] =
{
    SAVEBLOCK_CHUNK(struct SaveBlock2, 0), // SECTOR_ID_SAVEBLOCK2

    SAVEBLOCK_CHUNK(struct SaveBlock1, 0), // SECTOR_ID_SAVEBLOCK1_START
    SAVEBLOCK_CHUNK(struct SaveBlock1, 1),
    SAVEBLOCK_CHUNK(struct SaveBlock1, 2),
    SAVEBLOCK_CHUNK(struct SaveBlock1, 3), // SECTOR_ID_SAVEBLOCK1_END

    SAVEBLOCK_CHUNK(struct PokemonStorage, 0), // SECTOR_ID_PKMN_STORAGE_START
    SAVEBLOCK_CHUNK(struct PokemonStorage, 1),
    SAVEBLOCK_CHUNK(struct PokemonStorage, 2),
    SAVEBLOCK_CHUNK(struct PokemonStorage, 3),
    SAVEBLOCK_CHUNK(struct PokemonStorage, 4),
    SAVEBLOCK_CHUNK(struct PokemonStorage, 5),
    SAVEBLOCK_CHUNK(struct PokemonStorage, 6),
    SAVEBLOCK_CHUNK(struct PokemonStorage, 7),
    SAVEBLOCK_CHUNK(struct PokemonStorage, 8), // SECTOR_ID_PKMN_STORAGE_END
};

// These will produce an error if a save struct is larger than the space
// alloted for it in the flash.
STATIC_ASSERT(sizeof(struct SaveBlock2) <= SECTOR_DATA_SIZE, SaveBlock2FreeSpace);
STATIC_ASSERT(sizeof(struct SaveBlock1) <= SECTOR_DATA_SIZE * (SECTOR_ID_SAVEBLOCK1_END - SECTOR_ID_SAVEBLOCK1_START + 1), SaveBlock1FreeSpace);
STATIC_ASSERT(sizeof(struct PokemonStorage) <= SECTOR_DATA_SIZE * (SECTOR_ID_PKMN_STORAGE_END - SECTOR_ID_PKMN_STORAGE_START + 1), PokemonStorageFreeSpace);

#ifdef PORTABLE
// agbcc rounds every struct up to 4 bytes, and the save holds the structs as
// they are in RAM. Keep the GBA's layout so saves move between the two.
STATIC_ASSERT(sizeof(struct Time) == 8, TimeSizeMatchesGba);
STATIC_ASSERT(sizeof(struct Mail) == 0x24, MailSizeMatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, lastBerryTreeUpdate) == 0xA0, SaveBlock2LayoutMatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, encryptionKey) == 0xF20, SaveBlock2KeyMatchesGba);
// Every field of the two save blocks where pret notes its offset on the GBA
// (include/global.h), so a struct laid out differently from agbcc stops the build
STATIC_ASSERT(offsetof(struct SaveBlock2, playerName) == 0x0, SaveBlock2_playerName_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, playerGender) == 0x8, SaveBlock2_playerGender_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, specialSaveWarpFlags) == 0x9, SaveBlock2_specialSaveWarpFlags_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, playerTrainerId) == 0xA, SaveBlock2_playerTrainerId_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, playTimeHours) == 0xE, SaveBlock2_playTimeHours_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, playTimeMinutes) == 0x10, SaveBlock2_playTimeMinutes_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, playTimeSeconds) == 0x11, SaveBlock2_playTimeSeconds_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, playTimeVBlanks) == 0x12, SaveBlock2_playTimeVBlanks_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, optionsButtonMode) == 0x13, SaveBlock2_optionsButtonMode_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, pokedex) == 0x18, SaveBlock2_pokedex_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, filler_90) == 0x90, SaveBlock2_filler_90_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, localTimeOffset) == 0x98, SaveBlock2_localTimeOffset_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, lastBerryTreeUpdate) == 0xA0, SaveBlock2_lastBerryTreeUpdate_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, gcnLinkFlags) == 0xA8, SaveBlock2_gcnLinkFlags_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, unkFlag1) == 0xAC, SaveBlock2_unkFlag1_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, unkFlag2) == 0xAD, SaveBlock2_unkFlag2_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, battleTower) == 0xB0, SaveBlock2_battleTower_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, mapView) == 0x898, SaveBlock2_mapView_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, linkBattleRecords) == 0xA98, SaveBlock2_linkBattleRecords_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, berryCrush) == 0xAF0, SaveBlock2_berryCrush_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, pokeJump) == 0xB00, SaveBlock2_pokeJump_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, berryPick) == 0xB10, SaveBlock2_berryPick_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, filler_B20) == 0xB20, SaveBlock2_filler_B20_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock2, encryptionKey) == 0xF20, SaveBlock2_encryptionKey_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, pos) == 0x0, SaveBlock1_pos_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, location) == 0x4, SaveBlock1_location_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, continueGameWarp) == 0xC, SaveBlock1_continueGameWarp_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, dynamicWarp) == 0x14, SaveBlock1_dynamicWarp_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, lastHealLocation) == 0x1C, SaveBlock1_lastHealLocation_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, escapeWarp) == 0x24, SaveBlock1_escapeWarp_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, savedMusic) == 0x2C, SaveBlock1_savedMusic_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, weather) == 0x2E, SaveBlock1_weather_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, weatherCycleStage) == 0x2F, SaveBlock1_weatherCycleStage_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, flashLevel) == 0x30, SaveBlock1_flashLevel_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, mapLayoutId) == 0x32, SaveBlock1_mapLayoutId_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, playerPartyCount) == 0x34, SaveBlock1_playerPartyCount_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, playerParty) == 0x38, SaveBlock1_playerParty_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, money) == 0x290, SaveBlock1_money_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, coins) == 0x294, SaveBlock1_coins_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, registeredItem) == 0x296, SaveBlock1_registeredItem_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, pcItems) == 0x298, SaveBlock1_pcItems_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, bagPocket_Items) == 0x310, SaveBlock1_bagPocket_Items_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, bagPocket_KeyItems) == 0x3B8, SaveBlock1_bagPocket_KeyItems_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, bagPocket_PokeBalls) == 0x430, SaveBlock1_bagPocket_PokeBalls_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, bagPocket_TMHM) == 0x464, SaveBlock1_bagPocket_TMHM_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, bagPocket_Berries) == 0x54C, SaveBlock1_bagPocket_Berries_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, seen1) == 0x5F8, SaveBlock1_seen1_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, berryBlenderRecords) == 0x62C, SaveBlock1_berryBlenderRecords_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, unused_632) == 0x632, SaveBlock1_unused_632_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, trainerRematchStepCounter) == 0x638, SaveBlock1_trainerRematchStepCounter_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, trainerRematches) == 0x63A, SaveBlock1_trainerRematches_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, objectEvents) == 0x6A0, SaveBlock1_objectEvents_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, objectEventTemplates) == 0x8E0, SaveBlock1_objectEventTemplates_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, flags) == 0xEE0, SaveBlock1_flags_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, vars) == 0x1000, SaveBlock1_vars_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, gameStats) == 0x1200, SaveBlock1_gameStats_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, questLog) == 0x1300, SaveBlock1_questLog_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, easyChatProfile) == 0x2CA0, SaveBlock1_easyChatProfile_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, easyChatBattleStart) == 0x2CAC, SaveBlock1_easyChatBattleStart_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, easyChatBattleWon) == 0x2CB8, SaveBlock1_easyChatBattleWon_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, easyChatBattleLost) == 0x2CC4, SaveBlock1_easyChatBattleLost_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, mail) == 0x2CD0, SaveBlock1_mail_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, additionalPhrases) == 0x2F10, SaveBlock1_additionalPhrases_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, oldMan) == 0x2F18, SaveBlock1_oldMan_MatchesGba);
// (pret's note puts dewfordTrends at 0x2F54, but OldMan's filler is 0x40 bytes on the GBA
// too, so it's at 0x2F58, and the fields after it match)
STATIC_ASSERT(offsetof(struct SaveBlock1, daycare) == 0x2F80, SaveBlock1_daycare_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, giftRibbons) == 0x309C, SaveBlock1_giftRibbons_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, externalEventData) == 0x30A7, SaveBlock1_externalEventData_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, externalEventFlags) == 0x30BB, SaveBlock1_externalEventFlags_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, roamer) == 0x30D0, SaveBlock1_roamer_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, enigmaBerry) == 0x30EC, SaveBlock1_enigmaBerry_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, mysteryGift) == 0x3120, SaveBlock1_mysteryGift_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, unused_348C) == 0x348C, SaveBlock1_unused_348C_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, ramScript) == 0x361C, SaveBlock1_ramScript_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, recordMixingGift) == 0x3A08, SaveBlock1_recordMixingGift_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, seen2) == 0x3A18, SaveBlock1_seen2_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, rivalName) == 0x3A4C, SaveBlock1_rivalName_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, fameChecker) == 0x3A54, SaveBlock1_fameChecker_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, unused_3A94) == 0x3A94, SaveBlock1_unused_3A94_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, registeredTexts) == 0x3AD4, SaveBlock1_registeredTexts_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, trainerNameRecords) == 0x3BA8, SaveBlock1_trainerNameRecords_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, route5DayCareMon) == 0x3C98, SaveBlock1_route5DayCareMon_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, unused_3D24) == 0x3D24, SaveBlock1_unused_3D24_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, towerChallengeId) == 0x3D34, SaveBlock1_towerChallengeId_MatchesGba);
STATIC_ASSERT(offsetof(struct SaveBlock1, trainerTower) == 0x3D38, SaveBlock1_trainerTower_MatchesGba);
#endif

// Sector num to begin writing save data. Sectors are rotated each time the game is saved. (possibly to avoid wear on flash memory?)
COMMON_DATA u16 gLastWrittenSector = 0;
COMMON_DATA u32 gLastSaveCounter = 0;
COMMON_DATA u16 gLastKnownGoodSector = 0;
COMMON_DATA u32 gDamagedSaveSectors = 0;
COMMON_DATA u32 gSaveCounter = 0;
COMMON_DATA struct SaveSector *gSaveDataBufferPtr = NULL; // the pointer is in fast IWRAM but points to the slower EWRAM.
COMMON_DATA u16 gIncrementalSectorId = 0;
COMMON_DATA u16 gSaveUnusedVar = 0;
COMMON_DATA u16 gSaveFileStatus = 0;
COMMON_DATA void (*gGameContinueCallback)(void) = NULL;
COMMON_DATA struct SaveSectorLocation gRamSaveSectorLocations[NUM_SECTORS_PER_SLOT] = {0};
COMMON_DATA u16 gSaveAttemptStatus = 0;

EWRAM_DATA struct SaveSector gSaveDataBuffer = {0};
EWRAM_DATA u32 gSaveUnusedVar2 = 0;

void ClearSaveData(void)
{
    u16 i;

    for (i = 0; i < SECTORS_COUNT; i++)
        EraseFlashSector(i);
}

void Save_ResetSaveCounters(void)
{
    gSaveCounter = 0;
    gLastWrittenSector = 0;
    gDamagedSaveSectors = 0;
}

static bool32 SetDamagedSectorBits(u8 op, u8 sectorNum)
{
    bool32 retVal = FALSE;

    switch (op)
    {
    case ENABLE:
        gDamagedSaveSectors |= (1 << sectorNum);
        break;
    case DISABLE:
        gDamagedSaveSectors &= ~(1 << sectorNum);
        break;
    case CHECK: // unused
        if (gDamagedSaveSectors & (1 << sectorNum))
            retVal = TRUE;
        break;
    }

    return retVal;
}

static u8 WriteSaveSectorOrSlot(u16 sectorId, const struct SaveSectorLocation *locations)
{
    u32 status;
    u16 i;

    gSaveDataBufferPtr = &gSaveDataBuffer;

    if (sectorId != FULL_SAVE_SLOT)  // write single sector
        status = HandleWriteSector(sectorId, locations);

    else  // write all sectors
    {
        gLastKnownGoodSector = gLastWrittenSector; // backup the current written sector before attempting to write.
        gLastSaveCounter = gSaveCounter;
        gLastWrittenSector++;
        gLastWrittenSector %= NUM_SECTORS_PER_SLOT; // array count save sector locations
        gSaveCounter++;
        status = SAVE_STATUS_OK;

        for (i = 0; i < NUM_SECTORS_PER_SLOT; i++)
            HandleWriteSector(i, locations);

        // Check for any bad sectors
        if (gDamagedSaveSectors != 0) // skip the damaged sector.
        {
            status = SAVE_STATUS_ERROR;
            gLastWrittenSector = gLastKnownGoodSector;
            gSaveCounter = gLastSaveCounter;
        }
    }

    return status;
}

static u8 HandleWriteSector(u16 sectorId, const struct SaveSectorLocation *locations)
{
    u16 i;
    u16 sectorNum;
    u8 *data;
    u16 size;

    sectorNum = gLastWrittenSector + sectorId;
    sectorNum %= NUM_SECTORS_PER_SLOT;
    sectorNum += NUM_SECTORS_PER_SLOT * (gSaveCounter % NUM_SAVE_SLOTS);

    data = locations[sectorId].data;
    size = locations[sectorId].size;

    // clear buffer
    for (i = 0; i < SECTOR_SIZE; i++)
        ((char *)gSaveDataBufferPtr)[i] = 0;

    // fill buffer with save data
    gSaveDataBufferPtr->id = sectorId;
    gSaveDataBufferPtr->signature = SECTOR_SIGNATURE;
    gSaveDataBufferPtr->counter = gSaveCounter;

    for (i = 0; i < size; i++)
        gSaveDataBufferPtr->data[i] = data[i];

    gSaveDataBufferPtr->checksum = CalculateChecksum(data, size);
    return TryWriteSector(sectorNum, gSaveDataBufferPtr->data);
}

static u8 HandleWriteSectorNBytes(u8 sectorId, u8 *data, u16 size)
{
    u16 i;
    struct SaveSector *sector = &gSaveDataBuffer;

    for (i = 0; i < SECTOR_SIZE; i++)
        ((char *)sector)[i] = 0;

    sector->signature = SECTOR_SIGNATURE;

    for (i = 0; i < size; i++)
        sector->data[i] = data[i];

    sector->id = CalculateChecksum(data, size); // though this appears to be incorrect, it might be some sector checksum instead of a whole save checksum and only appears to be relevent to HOF data, if used.
    return TryWriteSector(sectorId, sector->data);
}

static u8 TryWriteSector(u8 sectorNum, u8 *data)
{
#if REVISION >= 0xA
    svc_WriteSector(sectorNum, data);
    SetDamagedSectorBits(DISABLE, sectorNum);
    return SAVE_STATUS_OK;
#else
    if (ProgramFlashSectorAndVerify(sectorNum, data)) // is damaged?
    {
        SetDamagedSectorBits(ENABLE, sectorNum); // set damaged sector bits.
        return SAVE_STATUS_ERROR;
    }
    else
    {
        SetDamagedSectorBits(DISABLE, sectorNum); // unset damaged sector bits. it's safe now.
        return SAVE_STATUS_OK;
    }
#endif
}

static u32 RestoreSaveBackupVarsAndIncrement(const struct SaveSectorLocation *locations)
{
    gSaveDataBufferPtr = &gSaveDataBuffer;
    gLastKnownGoodSector = gLastWrittenSector;
    gLastSaveCounter = gSaveCounter;
    gLastWrittenSector++;
    gLastWrittenSector %= NUM_SECTORS_PER_SLOT;
    gSaveCounter++;
    gIncrementalSectorId = 0;
    gDamagedSaveSectors = 0;
    return 0;
}

static u32 RestoreSaveBackupVars(const struct SaveSectorLocation *locations)
{
    gSaveDataBufferPtr = &gSaveDataBuffer;
    gLastKnownGoodSector = gLastWrittenSector;
    gLastSaveCounter = gSaveCounter;
    gIncrementalSectorId = 0;
    gDamagedSaveSectors = 0;
    return 0;
}

static u8 HandleWriteIncrementalSector(u16 numSectors, const struct SaveSectorLocation *locations)
{
    u8 status;

    if (gIncrementalSectorId < numSectors - 1)
    {
        status = SAVE_STATUS_OK;
        HandleWriteSector(gIncrementalSectorId, locations);
        gIncrementalSectorId++;
        if (gDamagedSaveSectors)
        {
            status = SAVE_STATUS_ERROR;
            gLastWrittenSector = gLastKnownGoodSector;
            gSaveCounter = gLastSaveCounter;
        }
    }
    else
        status = SAVE_STATUS_ERROR;

    return status;
}

static u8 HandleReplaceSectorAndVerify(u16 sectorId, const struct SaveSectorLocation *locations)
{
    u8 status = SAVE_STATUS_OK;

    HandleReplaceSector(sectorId - 1, locations);

    if (gDamagedSaveSectors)
    {
        status = SAVE_STATUS_ERROR;
        gLastWrittenSector = gLastKnownGoodSector;
        gSaveCounter = gLastSaveCounter;
    }
    return status;
}

static u8 HandleReplaceSector(u16 sectorId, const struct SaveSectorLocation *locations)
{
    u16 i;
    u16 sectorNum;
    u8 *data;
    u16 size;
    u8 status;

    sectorNum = gLastWrittenSector + sectorId;
    sectorNum %= NUM_SECTORS_PER_SLOT;
    sectorNum += NUM_SECTORS_PER_SLOT * (gSaveCounter % NUM_SAVE_SLOTS);

    data = locations[sectorId].data;
    size = locations[sectorId].size;

    // clear buffer
    for (i = 0; i < SECTOR_SIZE; i++)
        ((char *)gSaveDataBufferPtr)[i] = 0;

    // fill buffer with save data
    gSaveDataBufferPtr->id = sectorId;
    gSaveDataBufferPtr->signature = SECTOR_SIGNATURE;
    gSaveDataBufferPtr->counter = gSaveCounter;
    for (i = 0; i < size; i++)
        gSaveDataBufferPtr->data[i] = data[i];

    gSaveDataBufferPtr->checksum = CalculateChecksum(data, size);

#if REVISION >= 0xA
    svc_ReplaceSector(sectorNum, (u8*)gSaveDataBufferPtr);
    SetDamagedSectorBits(DISABLE, sectorNum);
    return SAVE_STATUS_OK;
#else
    // erase old save data
    EraseFlashSector(sectorNum);

    status = SAVE_STATUS_OK;

    // write new save data, excluding the signature and counter fields
    for (i = 0; i < SECTOR_SIGNATURE_OFFSET; i++)
    {
        if (ProgramFlashByte(sectorNum, i, gSaveDataBufferPtr->data[i]))
        {
            status = SAVE_STATUS_ERROR;
            break;
        }
    }

    if (status == SAVE_STATUS_ERROR)
    {
        SetDamagedSectorBits(ENABLE, sectorNum);
        return SAVE_STATUS_ERROR;
    }
    else
    {
        status = SAVE_STATUS_OK;

        // write signature (skipping the first byte) and counter fields
        // the first signature byte skipped is instead written in WriteSectorSignatureByte
        for (i = 0; i < SECTOR_SIZE - (SECTOR_SIGNATURE_OFFSET + 1); i++)
        {
            if (ProgramFlashByte(sectorNum, SECTOR_SIGNATURE_OFFSET + 1 + i, ((u8 *)gSaveDataBufferPtr)[SECTOR_SIGNATURE_OFFSET + 1 + i]))
            {
                status = SAVE_STATUS_ERROR;
                break;
            }
        }

        if (status == SAVE_STATUS_ERROR)
        {
            SetDamagedSectorBits(ENABLE, sectorNum);
            return SAVE_STATUS_ERROR;
        }
        else
        {
            SetDamagedSectorBits(DISABLE, sectorNum);
            return SAVE_STATUS_OK;
        }
    }
#endif
}

static u8 CopySectorSignatureByte(u16 sectorId, const struct SaveSectorLocation *locations)
{
    u16 sector;

    sector = gLastWrittenSector + sectorId - 1;
    sector %= NUM_SECTORS_PER_SLOT;
    sector += NUM_SECTORS_PER_SLOT * (gSaveCounter % NUM_SAVE_SLOTS);

    if (ProgramFlashByte(sector, SECTOR_SIGNATURE_OFFSET, ((u8 *)gSaveDataBufferPtr)[SECTOR_SIGNATURE_OFFSET]))
    {
        // sector is damaged, so enable the bit in gDamagedSaveSectors and restore the last written sector and save counter.
        SetDamagedSectorBits(ENABLE, sector);
        gLastWrittenSector = gLastKnownGoodSector;
        gSaveCounter = gLastSaveCounter;
        return SAVE_STATUS_ERROR;
    }
    else
    {
        SetDamagedSectorBits(DISABLE, sector);
        return SAVE_STATUS_OK;
    }
}

static u8 WriteSectorSignatureByte(u16 sectorId, const struct SaveSectorLocation *locations)
{
    u16 sector;

    sector = gLastWrittenSector + sectorId - 1;
    sector %= NUM_SECTORS_PER_SLOT;
    sector += NUM_SECTORS_PER_SLOT * (gSaveCounter % NUM_SAVE_SLOTS);

    // write only the first byte of the signature, which was skipped in HandleReplaceSector
    if (ProgramFlashByte(sector, SECTOR_SIGNATURE_OFFSET, SECTOR_SIGNATURE & 0xFF))
    {
        // sector is damaged, so enable the bit in gDamagedSaveSectors and restore the last written sector and save counter.
        SetDamagedSectorBits(ENABLE, sector);
        gLastWrittenSector = gLastKnownGoodSector;
        gSaveCounter = gLastSaveCounter;
        return SAVE_STATUS_ERROR;
    }
    else
    {
        SetDamagedSectorBits(DISABLE, sector);
        return SAVE_STATUS_OK;
    }
}

static u8 TryLoadSaveSlot(u16 sectorId, const struct SaveSectorLocation *locations)
{
    u8 status;
    gSaveDataBufferPtr = &gSaveDataBuffer;
    if (sectorId != FULL_SAVE_SLOT)
        status = SAVE_STATUS_ERROR;

    else
    {
        status = GetSaveValidStatus(locations);
        CopySaveSlotData(FULL_SAVE_SLOT, locations);
    }

    return status;
}

// sectorId is unused. All sectors in the save slot are read and copied.
static u8 CopySaveSlotData(u16 sectorId, const struct SaveSectorLocation *locations)
{
    u16 i;
    u16 checksum;
    u16 sector = NUM_SECTORS_PER_SLOT * (gSaveCounter % NUM_SAVE_SLOTS);
    u16 id;

    for (i = 0; i < NUM_SECTORS_PER_SLOT; i++)
    {
        ReadFlashSector(i + sector, gSaveDataBufferPtr);
        id = gSaveDataBufferPtr->id;
        if (id == 0)
            gLastWrittenSector = i;

#ifdef UBFIX
        // An erased sector reads as 0xFF, so its id is past the end of
        // locations. On the GBA its checksum reads garbage and the sector is
        // skipped because its signature is wrong.
        if (id >= NUM_SECTORS_PER_SLOT)
            continue;
#endif
        checksum = CalculateChecksum(gSaveDataBufferPtr->data, locations[id].size);
        if (gSaveDataBufferPtr->signature == SECTOR_SIGNATURE && gSaveDataBufferPtr->checksum == checksum)
        {
            u16 j;
            for (j = 0; j < locations[id].size; j++)
                locations[id].data[j] = gSaveDataBufferPtr->data[j];
        }
    }

    return SAVE_STATUS_OK;
}

static u8 GetSaveValidStatus(const struct SaveSectorLocation *locations)
{
    u16 sector;
    bool8 signatureValid;
    u16 checksum;
    u32 slot1saveCounter = 0;
    u32 slot2saveCounter = 0;
    u8 slot1Status;
    u8 slot2Status;
    u32 validSectors;
    const u32 ALL_SECTORS = (1 << NUM_SECTORS_PER_SLOT) - 1;  // bitmask of all saveblock sectors

    // check save slot 1.
    validSectors = 0;
    signatureValid = FALSE;
    for (sector = 0; sector < NUM_SECTORS_PER_SLOT; sector++)
    {
        ReadFlashSector(sector, gSaveDataBufferPtr);
        if (gSaveDataBufferPtr->signature == SECTOR_SIGNATURE)
        {
            signatureValid = TRUE;
            checksum = CalculateChecksum(gSaveDataBufferPtr->data, locations[gSaveDataBufferPtr->id].size);
            if (gSaveDataBufferPtr->checksum == checksum)
            {
                slot1saveCounter = gSaveDataBufferPtr->counter;
                validSectors |= 1 << gSaveDataBufferPtr->id;
            }
        }
    }

    if (signatureValid)
    {
        if (validSectors == ALL_SECTORS)
            slot1Status = SAVE_STATUS_OK;
        else
            slot1Status = SAVE_STATUS_ERROR;
    }
    else
        slot1Status = SAVE_STATUS_EMPTY;

    // check save slot 2.
    validSectors = 0;
    signatureValid = FALSE;
    for (sector = 0; sector < NUM_SECTORS_PER_SLOT; sector++)
    {
        ReadFlashSector(NUM_SECTORS_PER_SLOT + sector, gSaveDataBufferPtr);
        if (gSaveDataBufferPtr->signature == SECTOR_SIGNATURE)
        {
            signatureValid = TRUE;
            checksum = CalculateChecksum(gSaveDataBufferPtr->data, locations[gSaveDataBufferPtr->id].size);
            if (gSaveDataBufferPtr->checksum == checksum)
            {
                slot2saveCounter = gSaveDataBufferPtr->counter;
                validSectors |= 1 << gSaveDataBufferPtr->id;
            }
        }
    }

    if (signatureValid)
    {
        if (validSectors == ALL_SECTORS)
            slot2Status = SAVE_STATUS_OK;
        else
            slot2Status = SAVE_STATUS_ERROR;
    }
    else
        slot2Status = SAVE_STATUS_EMPTY;

    if (slot1Status == SAVE_STATUS_OK && slot2Status == SAVE_STATUS_OK)
    {
        // Choose counter of the most recent save file
        if ((slot1saveCounter == -1 && slot2saveCounter == 0) || (slot1saveCounter == 0 && slot2saveCounter == -1))
        {
            if ((unsigned)(slot1saveCounter + 1) < (unsigned)(slot2saveCounter + 1))
                gSaveCounter = slot2saveCounter;
            else
                gSaveCounter = slot1saveCounter;
        }
        else
        {
            if (slot1saveCounter < slot2saveCounter)
                gSaveCounter = slot2saveCounter;
            else
                gSaveCounter = slot1saveCounter;
        }
        return SAVE_STATUS_OK;
    }

    if (slot1Status == SAVE_STATUS_OK)
    {
        gSaveCounter = slot1saveCounter;
        if (slot2Status == SAVE_STATUS_ERROR)
            return SAVE_STATUS_ERROR;
        else
            return SAVE_STATUS_OK;
    }

    if (slot2Status == SAVE_STATUS_OK)
    {
        gSaveCounter = slot2saveCounter;
        if (slot1Status == SAVE_STATUS_ERROR)
            return SAVE_STATUS_ERROR;
        else
            return SAVE_STATUS_OK;
    }

    if (slot1Status == SAVE_STATUS_EMPTY && slot2Status == SAVE_STATUS_EMPTY)
    {
        gSaveCounter = 0;
        gLastWrittenSector = 0;
        return SAVE_STATUS_EMPTY;
    }

    gSaveCounter = 0;
    gLastWrittenSector = 0;
    return SAVE_STATUS_INVALID;
}

static u8 TryLoadSaveSector(u8 sectorId, u8 *data, u16 size)
{
    u16 i;
    struct SaveSector *sector = &gSaveDataBuffer;

    ReadFlashSector(sectorId, sector);
    if (sector->signature == SECTOR_SIGNATURE)
    {
        u16 checksum = CalculateChecksum(sector->data, size);
        if (sector->id == checksum)
        {
            for (i = 0; i < size; i++)
                data[i] = sector->data[i];

            return SAVE_STATUS_OK;
        }
        else
            return SAVE_STATUS_INVALID;

    }
    else
        return SAVE_STATUS_EMPTY;
}

static u8 ReadFlashSector(u8 sectorId, struct SaveSector *sector)
{
    ReadFlash(sectorId, 0, sector->data, SECTOR_SIZE);
    return 1;
}

static u16 CalculateChecksum(void *data, u16 size)
{
    u16 i;
    u32 checksum = 0;

    for (i = 0; i < (size / 4); i++)
    {
        // checksum += *(u32 *)data++;
        // For compatibility with modern gcc, these statements were separated.
        checksum += *(u32 *)data;
        data += 4;
    }

    return ((checksum >> 16) + checksum);
}

static void UpdateSaveAddresses(void)
{
    int i = 0;

    gRamSaveSectorLocations[i].data = (void *)(gSaveBlock2Ptr) + sSaveSlotLayout[i].offset;
    gRamSaveSectorLocations[i].size = sSaveSlotLayout[i].size;

    for (i = SECTOR_ID_SAVEBLOCK1_START; i <= SECTOR_ID_SAVEBLOCK1_END; i++)
    {
        gRamSaveSectorLocations[i].data = (void *)(gSaveBlock1Ptr) + sSaveSlotLayout[i].offset;
        gRamSaveSectorLocations[i].size = sSaveSlotLayout[i].size;
    }

    for (/*i = SECTOR_ID_PKMN_STORAGE_START*/; i <= SECTOR_ID_PKMN_STORAGE_END; i++) // do not initialize here to ensure matching
    {
        gRamSaveSectorLocations[i].data = (void *)(gPokemonStoragePtr) + sSaveSlotLayout[i].offset;
        gRamSaveSectorLocations[i].size = sSaveSlotLayout[i].size;
    }
}

u8 HandleSavingData(u8 saveType)
{
    u8 i;
    u32 *backupPtr = gMain.vblankCounter1;
    u8 *tempAddr;

    gMain.vblankCounter1 = NULL;
    UpdateSaveAddresses();
    switch (saveType)
    {
    case SAVE_HALL_OF_FAME_ERASE_BEFORE: // Unused
        for (i = SECTOR_ID_HOF_1; i < SECTORS_COUNT; i++)
            EraseFlashSector(i);
        // fallthrough
    case SAVE_HALL_OF_FAME:
        if (GetGameStat(GAME_STAT_ENTERED_HOF) < 999)
            IncrementGameStat(GAME_STAT_ENTERED_HOF);
        tempAddr = gDecompressionBuffer;
        HandleWriteSectorNBytes(SECTOR_ID_HOF_1, tempAddr, SECTOR_DATA_SIZE);
        HandleWriteSectorNBytes(SECTOR_ID_HOF_2, tempAddr + SECTOR_DATA_SIZE, SECTOR_DATA_SIZE);
        // fallthrough
    case SAVE_NORMAL:
    default:
        SaveSerializedGame();
        WriteSaveSectorOrSlot(FULL_SAVE_SLOT, gRamSaveSectorLocations);
        break;
    case SAVE_LINK:
        SaveSerializedGame();
        // only SaveBlock2 and SaveBlock1 (ignores storage in PC)
        for(i = SECTOR_ID_SAVEBLOCK2; i <= SECTOR_ID_SAVEBLOCK1_END; i++)
            WriteSaveSectorOrSlot(i, gRamSaveSectorLocations);
        break;
    case SAVE_EREADER: // unused
        SaveSerializedGame();
        // only SaveBlock2
        WriteSaveSectorOrSlot(SECTOR_ID_SAVEBLOCK2, gRamSaveSectorLocations);
        break;
    case SAVE_OVERWRITE_DIFFERENT_FILE:
        for (i = SECTOR_ID_HOF_1; i < SECTORS_COUNT; i++)
            EraseFlashSector(i);
        SaveSerializedGame();
        WriteSaveSectorOrSlot(FULL_SAVE_SLOT, gRamSaveSectorLocations);
        break;
    }
    gMain.vblankCounter1 = backupPtr;
#if REVISION >= 0xA
    svc_FinishSave();
#endif
#ifdef PORTABLE
    // Write the flash out to the save file once the whole save is done
    Platform_StoreSaveFile();
#endif
    return 0;
}

u8 TrySavingData(u8 saveType)
{
    if (gFlashMemoryPresent != TRUE)
    {
        gSaveAttemptStatus = SAVE_STATUS_ERROR;
        return SAVE_STATUS_ERROR;
    }

    HandleSavingData(saveType);
    if (!gDamagedSaveSectors)
    {
        gSaveAttemptStatus = SAVE_STATUS_OK;
        return SAVE_STATUS_OK;
    }
    else
    {
        DoSaveFailedScreen(saveType);
        gSaveAttemptStatus = SAVE_STATUS_ERROR;
        return SAVE_STATUS_ERROR;
    }
}

bool8 LinkFullSave_Init(void)
{
    if (gFlashMemoryPresent != TRUE)
        return TRUE;

    UpdateSaveAddresses();
    SaveSerializedGame();
    RestoreSaveBackupVarsAndIncrement(gRamSaveSectorLocations);
    return FALSE;
}

bool8 LinkFullSave_WriteSector(void) 
{
    u8 status = HandleWriteIncrementalSector(NUM_SECTORS_PER_SLOT, gRamSaveSectorLocations);
    if (gDamagedSaveSectors)
        DoSaveFailedScreen(SAVE_NORMAL);

    if (status == SAVE_STATUS_ERROR)
        return TRUE;
    else
        return FALSE;
}

bool8 LinkFullSave_ReplaceLastSector(void)
{
    HandleReplaceSectorAndVerify(NUM_SECTORS_PER_SLOT, gRamSaveSectorLocations);
    if (gDamagedSaveSectors)
        DoSaveFailedScreen(SAVE_NORMAL);

    return FALSE;
}

bool8 LinkFullSave_SetLastSectorSignature(void)
{
    CopySectorSignatureByte(NUM_SECTORS_PER_SLOT, gRamSaveSectorLocations);
    if (gDamagedSaveSectors)
        DoSaveFailedScreen(SAVE_NORMAL);
#ifdef PORTABLE
    Platform_StoreSaveFile();
#endif

    return FALSE;
}

bool8 WriteSaveBlock2(void)
{
    if (gFlashMemoryPresent != TRUE)
        return TRUE;

    UpdateSaveAddresses();
    SaveSerializedGame();
    RestoreSaveBackupVars(gRamSaveSectorLocations);

    // Because RestoreSaveBackupVars is called immediately prior,
    // gIncrementalSectorId will always be 0 (SECTOR_ID_SAVEBLOCK2) at this point,
    // so this function only saves the first sector (SECTOR_ID_SAVEBLOCK2)
    HandleReplaceSectorAndVerify(gIncrementalSectorId + 1, gRamSaveSectorLocations);
    return FALSE;
}

// Used in conjunction with WriteSaveBlock2 to write both for certain link saves.
// This is called repeatedly in a task, writing one sector of SaveBlock1 each time it is called.
// Returns TRUE when all sectors of SaveBlock1 have been written.
bool8 WriteSaveBlock1Sector(void)
{
    u8 finished = FALSE;
    u16 sectorId = ++gIncrementalSectorId;
    if (sectorId <= SECTOR_ID_SAVEBLOCK1_END)
    {
        HandleReplaceSectorAndVerify(gIncrementalSectorId + 1, gRamSaveSectorLocations);
        WriteSectorSignatureByte(sectorId, gRamSaveSectorLocations);
    }
    else
    {
        WriteSectorSignatureByte(sectorId, gRamSaveSectorLocations);
        finished = TRUE;
#ifdef PORTABLE
        Platform_StoreSaveFile();
#endif
    }
    if (gDamagedSaveSectors)
        DoSaveFailedScreen(SAVE_LINK);

    return finished;
}

u8 LoadGameSave(u8 saveType)
{
    u8 result;

    if (gFlashMemoryPresent != TRUE)
    {
        gSaveFileStatus = SAVE_STATUS_NO_FLASH;
        return SAVE_STATUS_ERROR;
    }

    UpdateSaveAddresses();
    switch (saveType)
    {
    case SAVE_NORMAL:
    default:
        result = TryLoadSaveSlot(FULL_SAVE_SLOT, gRamSaveSectorLocations);
        LoadSerializedGame();
        gSaveFileStatus = result;
        gGameContinueCallback = NULL;
        break;
    case SAVE_HALL_OF_FAME:
        result = TryLoadSaveSector(SECTOR_ID_HOF_1, gDecompressionBuffer, SECTOR_DATA_SIZE);
        if (result == SAVE_STATUS_OK)
            result = TryLoadSaveSector(SECTOR_ID_HOF_2, gDecompressionBuffer + SECTOR_DATA_SIZE, SECTOR_DATA_SIZE);
        break;
    }

    return result;
}

u32 TryReadSpecialSaveSector(u8 sectorId, u8 *dst)
{
    s32 i;
    s32 size;
    u8 *savData;

    if (sectorId != SECTOR_ID_TRAINER_TOWER_1 && sectorId != SECTOR_ID_TRAINER_TOWER_2)
        return SAVE_STATUS_ERROR;

    ReadFlash(sectorId, 0, (u8 *)&gSaveDataBuffer, SECTOR_SIZE);
    if (*(u32 *)(&gSaveDataBuffer.data[0]) != SPECIAL_SECTOR_SENTINEL)
        return SAVE_STATUS_ERROR;

    // copies whole save sector except the counter field
    i = 0;
    size = SECTOR_COUNTER_OFFSET - 1;
    savData = &gSaveDataBuffer.data[4]; // to skip past SPECIAL_SECTOR_SENTINEL
    for (; i <= size; i++)
        dst[i] = savData[i];

    return SAVE_STATUS_OK;
}

u32 TryWriteSpecialSaveSector(u8 sector, u8 *src)
{
    s32 i;
    s32 size;
    u8 *savData;
    void *savDataBuffer;

    if (sector != SECTOR_ID_TRAINER_TOWER_1 && sector != SECTOR_ID_TRAINER_TOWER_2)
        return SAVE_STATUS_ERROR;

    savDataBuffer = &gSaveDataBuffer;
    *(u32 *)(savDataBuffer) = SPECIAL_SECTOR_SENTINEL;

    // copies whole save sector except the counter field
    i = 0;
    size = SECTOR_COUNTER_OFFSET - 1;
    savData = &gSaveDataBuffer.data[4]; // to skip past SPECIAL_SECTOR_SENTINEL
    for (; i <= size; i++)
        savData[i] = src[i];

    if (ProgramFlashSectorAndVerify(sector, savDataBuffer) != 0)
        return SAVE_STATUS_ERROR;

    return SAVE_STATUS_OK;
}

void Task_LinkFullSave(u8 taskId)
{
    switch (gTasks[taskId].data[0])
    {
    case 0:
        gSoftResetDisabled = TRUE;
        gTasks[taskId].data[0] = 1;
        break;
    case 1:
#if REVISION >= 0xA
        if (!IsLinkTaskFinished()) break;
#endif
        SetLinkStandbyCallback();
        gTasks[taskId].data[0] = 2;
        break;
    case 2:
        if (IsLinkTaskFinished())
        {
            SaveMapView();
            gTasks[taskId].data[0] = 3;
        }
        break;
    case 3:
        SetContinueGameWarpStatusToDynamicWarp();
        LinkFullSave_Init();
        gTasks[taskId].data[0] = 4;
        break;
    case 4:
        if (++gTasks[taskId].data[1] == 5)
        {
            gTasks[taskId].data[1] = 0;
            gTasks[taskId].data[0] = 5;
        }
        break;
    case 5:
        if (LinkFullSave_WriteSector())
            gTasks[taskId].data[0] = 6;
        else
            gTasks[taskId].data[0] = 4;
        break;
    case 6:
        LinkFullSave_ReplaceLastSector();
        gTasks[taskId].data[0] = 7;
        break;
    case 7:
#if REVISION >= 0xA
        if (!IsLinkTaskFinished()) break;
#endif
        ClearContinueGameWarpStatus2();
        SetLinkStandbyCallback();
        gTasks[taskId].data[0] = 8;
        break;
    case 8:
        if (IsLinkTaskFinished())
        {
            LinkFullSave_SetLastSectorSignature();
#if REVISION >= 0xA
            svc_FinishSave();
#endif
            gTasks[taskId].data[0] = 9;
        }
        break;
    case 9:
#if REVISION >= 0xA
        if (!IsLinkTaskFinished()) break;
#endif
        SetLinkStandbyCallback();
        gTasks[taskId].data[0] = 10;
        break;
    case 10:
        if (IsLinkTaskFinished())
            gTasks[taskId].data[0]++;
        break;
    case 11:
        if (++gTasks[taskId].data[1] > 5)
        {
            gSoftResetDisabled = FALSE;
            DestroyTask(taskId);
        }
        break;
    }
}
