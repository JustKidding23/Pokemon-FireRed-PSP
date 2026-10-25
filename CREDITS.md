# Credits

Pokémon FireRed PSP is built on the work of many people. Everyone whose commits are in this
repository's history is also in GitHub's contributor list. This page says who did what,
from the contributor lists of each project on 24 October 2026, most commits first.

## The game

Pokémon FireRed is © 2004 Nintendo, Creatures Inc. and GAME FREAK inc. This is an unofficial
fan project, not made or endorsed by them or by Sony.

## The decompilation: [pret/pokefirered](https://github.com/pret/pokefirered)

The game's source code, rebuilt by the pret team so that it compiles to the original game.
Everything this port does starts from it. Its 65 contributors:

[PikalaxALT](https://github.com/PikalaxALT), [GriffinRichards](https://github.com/GriffinRichards), [jiangzhengwenjz](https://github.com/jiangzhengwenjz), [ghoulslash](https://github.com/ghoulslash), [garakmon](https://github.com/garakmon), [Kurausukun](https://github.com/Kurausukun), [ultima-soul](https://github.com/ultima-soul), [scnorton](https://github.com/scnorton), [cbt6](https://github.com/cbt6), [AsparagusEduardo](https://github.com/AsparagusEduardo), [Missingmew](https://github.com/Missingmew), [RevoSucks](https://github.com/RevoSucks), [huderlem](https://github.com/huderlem), [luckytyphlosion](https://github.com/luckytyphlosion), [SatoMew](https://github.com/SatoMew), sceptillion, [Deokishisu](https://github.com/Deokishisu), [gwimm](https://github.com/gwimm), [AZero13](https://github.com/AZero13), [hjk321](https://github.com/hjk321), [LOuroboros](https://github.com/LOuroboros), [cawtds](https://github.com/cawtds), [colonelsalt](https://github.com/colonelsalt), [fieldy101](https://github.com/fieldy101), [sphericalice](https://github.com/sphericalice), [Sierraffinity](https://github.com/Sierraffinity), [melthelesbian](https://github.com/melthelesbian), [Delta456](https://github.com/Delta456), [Squeetz](https://github.com/Squeetz), [V-FEXrt](https://github.com/V-FEXrt), [Jademalo](https://github.com/Jademalo), [Jaizu](https://github.com/Jaizu), [Soul-8691](https://github.com/Soul-8691), SombrAbsol, “Delta456”, [mrgriffin](https://github.com/mrgriffin), [fdeblasio](https://github.com/fdeblasio), [Tianmaru](https://github.com/Tianmaru), lioniac, [SethBarberee](https://github.com/SethBarberee), [SnorlaxMonster](https://github.com/SnorlaxMonster), [abcboy101](https://github.com/abcboy101), [Sewef](https://github.com/Sewef), PokeCodec, [kovankerckhoven](https://github.com/kovankerckhoven), [Gamr13](https://github.com/Gamr13), [ednolan](https://github.com/ednolan), [BigBahss](https://github.com/BigBahss), [AnonymousRandomPerson](https://github.com/AnonymousRandomPerson), [citrusbolt](https://github.com/citrusbolt), [WhenGryphonsFly](https://github.com/WhenGryphonsFly), [sophxm](https://github.com/sophxm), [Wack0](https://github.com/Wack0), [Birdulon](https://github.com/Birdulon), [Zeturic](https://github.com/Zeturic), [Kermalis](https://github.com/Kermalis), [waterwheels](https://github.com/waterwheels), [jarechiga11](https://github.com/jarechiga11), [iimarckus](https://github.com/iimarckus), Hedara, [estellarc](https://github.com/estellarc), [rawr51919](https://github.com/rawr51919), [camthesaxman](https://github.com/camthesaxman), [aarant](https://github.com/aarant), [Acimut](https://github.com/Acimut)

## The ports it comes from

The code that runs the game outside a GBA came to FireRed from the owner's
[Pokémon Emerald PSP](https://github.com/JustKidding23/Pokemon-Emerald-PSP), which was built on:

- [NTx86/pokeemerald-sdl2pc](https://github.com/NTx86/pokeemerald-sdl2pc), the first PC port,
  by [NTx86](https://github.com/NTx86) with [JaceCear](https://github.com/JaceCear),
  [BiffaloXIII](https://github.com/BiffaloXIII) and elyro1
- pokeemerald-native, where [fuddlesworth](https://github.com/fuddlesworth) carried the PC
  port on (its repository is no longer public). The fixes for dividing by zero and for battle
  scripts without an animation argument came from there too.
- [dnllnoe/pokeemerald-ps2](https://github.com/dnllnoe/pokeemerald-ps2), the PlayStation 2 port by
  [dnllnoe](https://github.com/dnllnoe), which first built the game for a 32-bit MIPS console

The idea of carrying windows on past the GBA's edges in the wider view came from
[Goldoire/pokeemerald-dualscreen](https://github.com/Goldoire/pokeemerald-dualscreen).

## The PSP port

[JustKidding23](https://github.com/JustKidding23), with Claude (Anthropic's AI assistant) in
Claude Code, which wrote the code. See [How this was made](README.md#how-this-was-made).

## Tools

- [pspdev](https://github.com/pspdev): the PSP toolchain and pspsdk the game is built with
- [PSPLink](https://github.com/pspdev/psplinkusb), by TyRaNiD, Julian T, Rasmus B, John_K and
  pspdev: starting and debugging the game on a PSP over USB
- [PPSSPP](https://github.com/hrydgard/ppsspp): testing it on a PC
- [mGBA](https://github.com/mgba-emu/mgba): showing how the original game draws a frame, to
  compare with
- [atracdenc](https://github.com/dcherednik/atracdenc), by Daniil Cherednik: the PSP menu's
  music
