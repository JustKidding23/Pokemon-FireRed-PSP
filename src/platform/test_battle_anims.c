// Emulator and PSPLink tests only: every move's battle animation, one after
// another, in a wild battle, with screenshots at set frames of each, to check
// them in the wider view. The test input's WILDBATTLE starts the battle from
// the overworld; ANIMS:first:last, once its menu is up, plays the moves.
#ifdef PSP_TEST
#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_gfx_sfx_util.h"
#include "battle_main.h"
#include "battle_setup.h"
#include "palette.h"
#include "pokemon.h"
#include "sprite.h"
#include "task.h"
#include "constants/moves.h"
#include "constants/species.h"

void TestLog(const char *format, ...);
extern u32 gTestDrawMax, gTestDrawSlow;
u32 gTestBehind(void);
static u32 sBehindAtStart;
void PspHw_DumpScreen(const char *path);
void CopyAllBattleSpritesInvisibilities(void);
void SetBattlerSpriteAffineMode(u8 affineMode);

// Frames into each animation that are screenshot as anim_MMM_N
static const u16 sShotFrames[] = {6, 18, 36, 60, 96};

#define tMove  data[0]
#define tLast  data[1]
#define tFrame data[2]
#define tState data[3]
#define tTurn  data[4]

static struct DisableStruct sDisable;
// Where the two Pokémon stand, put back after each move
static s16 sHomeX[2], sHomeY[2];
// The player's battle controller, paused during the test: the action menu
// would start the Pokémon bobbing again every frame
static void (*sSavedControllerFunc)(void);

static void ControllerPaused(void)
{
}

void TestStartWildBattle(void)
{
    ZeroEnemyPartyMons();
    CreateMon(&gEnemyParty[0], SPECIES_RATICATE, 30, 31, FALSE, 0, OT_ID_PLAYER_ID, 0);
    StartWildBattle();
}

static void Task_TestBattleAnims(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    char path[64];

    switch (tState)
    {
    case 0:
        if (gAnimScriptActive || gPaletteFade.active)
            break;
        // What the player's battle controller sets up for a move, after the
        // menu's bobbing stops (some animations move the Pokémon until it's
        // exactly back in place)
        EndBounceEffect(0, BOUNCE_HEALTHBOX);
        EndBounceEffect(0, BOUNCE_MON);
        memset(&sDisable, 0, sizeof(sDisable));
        gBattlerAttacker = 0;
        gBattlerTarget = 1;
        // Both turns, in order, as a battle plays two-turn moves like Dig
        gAnimMoveTurn = tTurn;
        gAnimMovePower = 60;
        gAnimMoveDmg = 30;
        gAnimFriendship = 255;
        gWeatherMoveAnim = 0;
        gAnimDisableStructPtr = &sDisable;
        SetBattlerSpriteAffineMode(ST_OAM_AFFINE_OFF);
        for (u32 battler = 0; battler < 2 && tTurn == 0; battler++)
        {
            sHomeX[battler] = gSprites[gBattlerSpriteIds[battler]].x;
            sHomeY[battler] = gSprites[gBattlerSpriteIds[battler]].y;
        }
        TestLog("anim %d turn %d start\n", tMove, tTurn);
        gTestDrawMax = gTestDrawSlow = 0;
        sBehindAtStart = gTestBehind();
        DoMoveAnim(tMove);
        tFrame = 0;
        tState = 1;
        break;
    case 1:
        gAnimScriptCallback();
        tFrame++;
        for (u32 i = 0; i < ARRAY_COUNT(sShotFrames); i++)
        {
#ifdef PSP_PROFILE
            // Timed on the PSP: screenshots over USB would stall it
            if (FALSE)
#else
            if (tFrame == sShotFrames[i] && gAnimScriptActive)
#endif
            {
                snprintf(path, sizeof(path), "host0:/anim_%03d_%d%u.rgba", tMove, tTurn, i);
                PspHw_DumpScreen(path);
                {
                    extern bool8 gWideViewMargins;
                    extern u8 gWideViewRepeatingBgs;

                    TestLog("  shot %u: dispcnt %04x bg %04x %04x %04x %04x bldcnt %04x win %04x/%04x margins %d repeat %02x\n", i,
                            REG_DISPCNT, REG_BG0CNT, REG_BG1CNT, REG_BG2CNT, REG_BG3CNT, REG_BLDCNT, REG_WININ, REG_WINOUT,
                            gWideViewMargins, gWideViewRepeatingBgs);
                    {
                        const u16 *map = (const u16 *)BG_SCREEN_ADDR((REG_BG3CNT >> 8) & 0x1F);

                        TestLog("    bg3 hofs %d row5: %04x %04x %04x | %04x %04x %04x %04x | %04x %04x %04x\n",
                                REG_BG3HOFS, map[5 * 32], map[5 * 32 + 1], map[5 * 32 + 2], map[5 * 32 + 29], map[5 * 32 + 30],
                                map[5 * 32 + 31], map[0x400 + 5 * 32], map[0x400 + 5 * 32 + 1], map[0x400 + 5 * 32 + 30], map[0x400 + 5 * 32 + 31]);
                    }
                }
            }
        }
        if (!gAnimScriptActive || tFrame > 900)
        {
            if (gAnimScriptActive)
            {
                TestLog("anim %d still running after %d frames: %d visual tasks, %d sound tasks\n",
                        tMove, tFrame, gAnimVisualTaskCount, gAnimSoundTaskCount);
                for (u32 battler = 0; battler < 2; battler++)
                {
                    struct Sprite *sprite = &gSprites[gBattlerSpriteIds[battler]];

                    TestLog("  battler %u sprite %u x2 %d y2 %d callback %08x\n", battler, gBattlerSpriteIds[battler],
                            sprite->x2, sprite->y2, (unsigned)(uintptr_t)sprite->callback);
                }
                for (u32 i = 0; i < NUM_TASKS; i++)
                {
                    if (gTasks[i].isActive && i != taskId)
                        TestLog("  task %u func %08x data %d %d %d %d %d %d %d %d %d %d\n", i, (unsigned)(uintptr_t)gTasks[i].func,
                                gTasks[i].data[7], gTasks[i].data[8], gTasks[i].data[9], gTasks[i].data[10], gTasks[i].data[11],
                                gTasks[i].data[12], gTasks[i].data[13], gTasks[i].data[14], gTasks[i].data[15], gTasks[i].data[0]);
                }
            }
            TestLog("anim %d end %d frames, slowest draw %u us, %u over 13 ms, %u skipped\n", tMove, tFrame,
                    gTestDrawMax, gTestDrawSlow, gTestBehind() - sBehindAtStart);
            // Where the two Pokémon were left: a move that leaves one moved
            // shows it in the wider view's margins
            for (u32 battler = 0; battler < 2; battler++)
            {
                struct Sprite *sprite = &gSprites[gBattlerSpriteIds[battler]];

                TestLog("  pos %u: x %d y %d x2 %d y2 %d invisible %d\n", battler, sprite->x, sprite->y,
                        sprite->x2, sprite->y2, sprite->invisible);
            }
            SetBattlerSpriteAffineMode(ST_OAM_AFFINE_NORMAL);
            // Substitute leaves the player's Pokémon as the doll, and
            // Transform as the other Pokémon: its own picture back
            if (tTurn == 1)
            {
                gBattleSpritesDataPtr->battlerData[0].transformSpecies = SPECIES_NONE;
                LoadBattleMonGfxAndAnimate(0, TRUE, gBattlerSpriteIds[0]);
            }
            // Moves like Fly, Dig and Teleport leave the Pokémon hidden or
            // moved (Teleport parks it past the right edge, x 272)
            for (u32 battler = 0; battler < 2 && tTurn == 1; battler++)
            {
                struct Sprite *sprite = &gSprites[gBattlerSpriteIds[battler]];

                sprite->invisible = FALSE;
                sprite->x = sHomeX[battler];
                sprite->y = sHomeY[battler];
                sprite->x2 = sprite->y2 = 0;
            }
            tFrame = 0;
            tState = 2;
        }
        break;
    case 2:
        if (++tFrame < 20)
            break;
        if (tTurn == 0)
        {
            // The second turn, without the Pokémon put back in between
            tTurn = 1;
            tState = 0;
            break;
        }
        tTurn = 0;
        if (++tMove > tLast)
        {
            gBattlerControllerFuncs[0] = sSavedControllerFunc;
            TestLog("anims done\n");
            DestroyTask(taskId);
            break;
        }
        tState = 0;
        break;
    }
}

void TestStartBattleAnims(int first, int last)
{
    u8 taskId = CreateTask(Task_TestBattleAnims, 0);

    sSavedControllerFunc = gBattlerControllerFuncs[0];
    gBattlerControllerFuncs[0] = ControllerPaused;

    gTasks[taskId].tMove = first;
    gTasks[taskId].tLast = last;
    gTasks[taskId].tState = 0;
    gTasks[taskId].tTurn = 0;
}
#endif
