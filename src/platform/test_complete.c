// Emulator and PSPLink tests only: turns the save being played into a
// "complete" one, for testing screens that need a whole game's progress (the
// owner's idea, 2026-10-22). The test input's COMPLETE does it in the
// overworld; the script then saves with the start menu as usual.
#ifdef PSP_TEST
#include "global.h"
#include "event_data.h"
#include "item.h"
#include "money.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"

extern void EnableNationalPokedex(void);
extern void CreateBoxMonAt(u8 boxId, u8 boxPosition, u16 species, u8 level, u8 fixedIV, u8 hasFixedPersonality, u32 personality, u8 otIDType, u32 otID);

void TestMakeCompleteSave(void)
{
    u16 species;
    int slot = 0;

    // Every badge, the game cleared, the National Pokédex, the Sevii Islands
    // and every place on the map visited (for Fly)
    for (int flag = FLAG_BADGE01_GET; flag <= FLAG_BADGE08_GET; flag++)
        FlagSet(flag);
    FlagSet(FLAG_SYS_POKEMON_GET);
    FlagSet(FLAG_SYS_POKEDEX_GET);
    FlagSet(FLAG_SYS_GAME_CLEAR);
    FlagSet(FLAG_SYS_SEVII_MAP_123);
    FlagSet(FLAG_SYS_SEVII_MAP_4567);
    for (int flag = FLAG_WORLD_MAP_PALLET_TOWN; flag <= FLAG_WORLD_MAP_BIRTH_ISLAND_EXTERIOR; flag++)
        FlagSet(flag);
    EnableNationalPokedex();

    // Every Pokémon seen and caught, and one of each in the boxes
    for (species = 1; species < NUM_SPECIES; species++)
    {
        u16 dexNum;

        if (species >= SPECIES_OLD_UNOWN_B && species <= SPECIES_OLD_UNOWN_Z)
            continue;
        dexNum = SpeciesToNationalPokedexNum(species);
        GetSetPokedexFlag(dexNum, FLAG_SET_SEEN);
        GetSetPokedexFlag(dexNum, FLAG_SET_CAUGHT);
        if (slot < TOTAL_BOXES_COUNT * IN_BOX_COUNT)
        {
            CreateBoxMonAt(slot / IN_BOX_COUNT, slot % IN_BOX_COUNT, species, 50, 20, FALSE, 0, OT_ID_PLAYER_ID, 0);
            slot++;
        }
    }

    // Every item: one of each key item, TM and HM, 99 of the rest, and some
    // in the PC too
    for (u16 item = 1; item < ITEMS_COUNT; item++)
    {
        u8 pocket = ItemId_GetPocket(item);

        if (ItemId_GetName(item)[0] == CHAR_QUESTION_MARK)
            continue;
        // Key items only Ruby, Sapphire and Emerald have: no player has them,
        // and they'd fill the pocket before FireRed's own
        if (item == ITEM_MACH_BIKE || (item >= ITEM_CONTEST_PASS && item <= ITEM_DEVON_SCOPE))
            continue;
        AddBagItem(item, (pocket == POCKET_KEY_ITEMS || pocket == POCKET_TM_CASE) ? 1 : 99);
        if (item <= ITEM_TINY_MUSHROOM && pocket == POCKET_ITEMS)
            AddPCItem(item, 5);
    }
    SetMoney(&gSaveBlock1Ptr->money, 999999);

    // Two Mew that know every field move between them, after the party, for
    // testing the field effects: Fly, Surf, Strength, Flash; Cut, Rock Smash,
    // Waterfall, Dig
    {
        static const u16 sFieldMoves[2][MAX_MON_MOVES] = {
            {MOVE_FLY, MOVE_SURF, MOVE_STRENGTH, MOVE_FLASH},
            {MOVE_CUT, MOVE_ROCK_SMASH, MOVE_WATERFALL, MOVE_DIG},
        };
        u8 count = CalculatePlayerPartyCount();

        for (int i = 0; i < 2 && count < PARTY_SIZE; i++, count++)
        {
            CreateMon(&gPlayerParty[count], SPECIES_MEW, 70, 31, FALSE, 0, OT_ID_PLAYER_ID, 0);
            for (int m = 0; m < MAX_MON_MOVES; m++)
                SetMonMoveSlot(&gPlayerParty[count], sFieldMoves[i][m], m);
        }
        CalculatePlayerPartyCount();
    }
}
#endif
