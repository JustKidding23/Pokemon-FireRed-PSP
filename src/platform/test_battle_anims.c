// Emulator and PSPLink tests only: every move's battle animation, one after
// another, in a wild battle, with screenshots at set frames of each, to check
// them in the wider view. The test input's WILDBATTLE starts the battle from
// the overworld; ANIMS:first:last, once its menu is up, plays the moves.
#ifdef PSP_TEST
#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_gfx_sfx_util.h"
#include "battle_setup.h"
#include "palette.h"
#include "pokemon.h"
#include "sprite.h"
#include "task.h"
#include "constants/moves.h"
#include "constants/species.h"

void TestLog(const char *format, ...);
void PspHw_DumpScreen(const char *path);
void CopyAllBattleSpritesInvisibilities(void);
void SetBattlerSpriteAffineMode(u8 affineMode);

// Frames into each animation that are screenshot as anim_MMM_N
static const u16 sShotFrames[] = {6, 18, 36, 60, 96};

#define tMove  data[0]
#define tLast  data[1]
#define tFrame data[2]
#define tState data[3]

static struct DisableStruct sDisable;

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
        // What the player's battle controller sets up for a move
        memset(&sDisable, 0, sizeof(sDisable));
        gBattlerAttacker = 0;
        gBattlerTarget = 1;
        gAnimMoveTurn = 1;
        gAnimMovePower = 60;
        gAnimMoveDmg = 30;
        gAnimFriendship = 255;
        gWeatherMoveAnim = 0;
        gAnimDisableStructPtr = &sDisable;
        SetBattlerSpriteAffineMode(ST_OAM_AFFINE_OFF);
        TestLog("anim %d start\n", tMove);
        DoMoveAnim(tMove);
        tFrame = 0;
        tState = 1;
        break;
    case 1:
        gAnimScriptCallback();
        tFrame++;
        for (u32 i = 0; i < ARRAY_COUNT(sShotFrames); i++)
        {
            if (tFrame == sShotFrames[i] && gAnimScriptActive)
            {
                snprintf(path, sizeof(path), "host0:/anim_%03d_%u.rgba", tMove, i);
                PspHw_DumpScreen(path);
            }
        }
        if (!gAnimScriptActive || tFrame > 900)
        {
            if (gAnimScriptActive)
            {
                TestLog("anim %d still running after %d frames: %d visual tasks, %d sound tasks\n",
                        tMove, tFrame, gAnimVisualTaskCount, gAnimSoundTaskCount);
                for (u32 i = 0; i < NUM_TASKS; i++)
                {
                    if (gTasks[i].isActive && i != taskId)
                        TestLog("  task %u func %08x data %d %d %d %d %d %d %d %d %d %d\n", i, (unsigned)(uintptr_t)gTasks[i].func,
                                gTasks[i].data[7], gTasks[i].data[8], gTasks[i].data[9], gTasks[i].data[10], gTasks[i].data[11],
                                gTasks[i].data[12], gTasks[i].data[13], gTasks[i].data[14], gTasks[i].data[15], gTasks[i].data[0]);
                }
            }
            TestLog("anim %d end %d frames\n", tMove, tFrame);
            SetBattlerSpriteAffineMode(ST_OAM_AFFINE_NORMAL);
            // Moves like Fly and Dig leave the Pokémon hidden or moved
            for (u32 battler = 0; battler < 2; battler++)
            {
                struct Sprite *sprite = &gSprites[gBattlerSpriteIds[battler]];

                sprite->invisible = FALSE;
                sprite->x2 = sprite->y2 = 0;
            }
            tFrame = 0;
            tState = 2;
        }
        break;
    case 2:
        if (++tFrame < 20)
            break;
        if (++tMove > tLast)
        {
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

    gTasks[taskId].tMove = first;
    gTasks[taskId].tLast = last;
    gTasks[taskId].tState = 0;
}
#endif
