// Emulator and PSPLink tests only: shortcuts to screens that take a lot of
// play to reach, for checking them in the wider view and hunting crashes. The
// test input's EVOLVE, HATCH and COINS, each done once the overworld is up.
#ifdef PSP_TEST
#include "global.h"
#include "coins.h"
#include "daycare.h"
#include "event_data.h"
#include "event_scripts.h"
#include "evolution_scene.h"
#include "item.h"
#include "overworld.h"
#include "pokemon.h"
#include "script.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/species.h"

extern const struct Evolution gEvolutionTable[NUM_SPECIES][EVOS_PER_MON];

// The first Pokémon in the party evolves into its first evolution, as after a
// level up, with B able to stop it
void TestEvolve(void)
{
    struct Pokemon *mon = &gPlayerParty[0];
    u16 species = GetMonData(mon, MON_DATA_SPECIES);
    u16 target = gEvolutionTable[species][0].targetSpecies;

    if (target == SPECIES_NONE)
        return;
    gCB2_AfterEvolution = CB2_ReturnToField;
    BeginEvolutionScene(mon, target, TRUE, 0);
}

// An egg in the party's last free slot hatches straight away
void TestHatchEgg(void)
{
    u8 slot = CalculatePlayerPartyCount();

    if (slot >= PARTY_SIZE)
        slot = PARTY_SIZE - 1;
    CreateEgg(&gPlayerParty[slot], SPECIES_PICHU, FALSE);
    CalculatePlayerPartyCount();
    gSpecialVar_0x8004 = slot;
    ScriptContext_SetupScript(EventScript_EggHatch);
}

// Coins for the Game Corner's slots, and the Coin Case: its flag (the slot
// machines check that, not the bag) and the item if the bag lacks it
void TestGiveCoins(void)
{
    FlagSet(FLAG_GOT_COIN_CASE);
    if (!CheckBagHasItem(ITEM_COIN_CASE, 1))
        AddBagItem(ITEM_COIN_CASE, 1);
    SetCoins(9999);
}
#endif
