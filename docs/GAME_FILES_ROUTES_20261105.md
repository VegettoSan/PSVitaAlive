# PSVitaAlive — Inventario completo de rutas Game Files

**Corte:** 5 de noviembre de 2026. **Fuente:** archivos canónicos de `apps/`, no el catálogo generado.

> Regla: para Game Files externos, `installed_paths` se resuelve desde la raíz `ux0:data/`, aunque `extract_path` apunte a una subcarpeta más profunda. Los Game Files ubicados en `ux0:app/` pertenecen a la aplicación y no requieren `installed_paths`.

## Resumen

- **158 aplicaciones** con Game Files: **150** en `ux0:data/` y **8** en `ux0:app/`.
- **172 enlaces Game Files**: 163 hacia data y 9 hacia app.
- Las rutas documentan la carpeta principal identificada; no otorgan permiso automático para borrar partidas, ajustes, cachés compartidas u otros archivos.

## Tabla por aplicación (configuración efectiva)

| # | Aplicación | Title ID | JSON | Destino `extract_path` | `installed_paths` | Ruta que se identifica | Verificación |
|---:|---|---|---|---|---|---|---|
| 1 | [2ship2harkinian Vita](../apps/2ship2harkinian-vita.json) | `2SHIP2HAR` | `2ship2harkinian-vita.json` | `ux0:data/` | `2s2h` | `ux0:data/2s2h/` | ZIP |
| 2 | [9mm HD](../apps/9mm-hd.json) | `NMHD00001` | `9mm-hd.json` | `ux0:data/` | `9mm` | `ux0:data/9mm/` | ZIP |
| 3 | [Amnesia: The Dark Descent](../apps/amnesia-the-dark-descent.json) | `AMNE00001` | `amnesia-the-dark-descent.json` | `ux0:data/` | `Amnesia` | `ux0:data/Amnesia/` | ZIP |
| 4 | [Animal Crossing Vita](../apps/animal-crossing-vita.json) | `ACGC00001` | `animal-crossing-vita.json` | `ux0:data/AnimalCrossing/rom/` | `AnimalCrossing` | `ux0:data/AnimalCrossing/` | Ruta de extracción |
| 5 | [Arcaea](../apps/arcaea.json) | `ARCA69420` | `arcaea.json` | `ux0:data/` | `arcaea` | `ux0:data/arcaea/` | ZIP |
| 6 | [Asphalt 5](../apps/asphalt-5.json) | `ASPHALT05` | `asphalt-5.json` | `ux0:data/` | `asphalt5` | `ux0:data/asphalt5/` | ZIP |
| 7 | [Asphalt 6](../apps/asphalt-6.json) | `ASPHALT06` | `asphalt-6.json` | `ux0:data/` | `asphalt6` | `ux0:data/asphalt6/` | ZIP |
| 8 | [Asphalt 8 Vita](../apps/asphalt-8.json) | `ASPHALT20` | `asphalt-8.json` | `ux0:data/` | `asphalt8` | `ux0:data/asphalt8/` | ZIP |
| 9 | [Barony Vita](../apps/barony-vita.json) | `BARO00001` | `barony-vita.json` | `ux0:data/` | `barony` | `ux0:data/barony/` | ZIP |
| 10 | [Batman - The Dark Knight Rises](../apps/batman-the-dark-knight-rises.json) | `KRAS00000` | `batman-the-dark-knight-rises.json` | `ux0:data/` | `kras` | `ux0:data/kras/` | Documentación / revisión |
| 11 | [Batman AC Lockdown](../apps/batman-ac-lockdown.json) | `ACLD00001` | `batman-ac-lockdown.json` | `ux0:data/` | `aclockdown` | `ux0:data/aclockdown/` | ZIP |
| 12 | [Batman Arkham Origins](../apps/batman-arkham-origins.json) | `BAOR00001` | `batman-arkham-origins.json` | `ux0:data/` | `batmanarkhamorigins` | `ux0:data/batmanarkhamorigins/` | ZIP |
| 13 | [Battleship Vita](../apps/battleship-vita.json) | `SSB64VITA` | `battleship-vita.json` | `ux0:data/` | `battleship` | `ux0:data/battleship/` | ZIP |
| 14 | [Beach Buggy Racing Vita](../apps/beach-buggy-racing-vita.json) | `BBRV00001` | `beach-buggy-racing-vita.json` | `ux0:data/` | `beachbuggyracing` | `ux0:data/beachbuggyracing/` | ZIP |
| 15 | [BIOHAZARD4 Vita](../apps/biohazard4-vita.json) | `BIO4AF000` | `biohazard4-vita.json` | `ux0:data/` | `Bio4` | `ux0:data/Bio4/` | ZIP |
| 16 | [Blasphemous Vita](../apps/blasphemous-vita.json) | `BLPM12345` | `blasphemous-vita.json` | `ux0:app/` | — | `ux0:app/BLPM12345/` | Destino app |
| 17 | [BombSquad Vita (Alpha Version)](../apps/bombsquad-vita-alpha-version.json) | `BOMBS1762` | `bombsquad-vita-alpha-version.json` | `ux0:data/` | `BombSquad` | `ux0:data/BombSquad/` | ZIP |
| 18 | [Brothers in Arms 2](../apps/brothers-in-arms-2.json) | `PSVBIA002` | `brothers-in-arms-2.json` | `ux0:data/` | `brothersinarms2` | `ux0:data/brothersinarms2/` | ZIP |
| 19 | [Buckshot Roulette](../apps/buckshot-roulette.json) | `BSHT00016` | `buckshot-roulette.json` | `ux0:data/` | `bsht` | `ux0:data/bsht/` | ZIP |
| 20 | [Bully Vita](../apps/bully-vita.json) | `BULLY0000` | `bully-vita.json` | `ux0:data/` | `Bully` | `ux0:data/Bully/` | ZIP |
| 21 | [Call Of Duty 4: Modern Warfare Multiplayer](../apps/call-of-duty-4-modern-warfare-multiplayer.json) | `CODVIT001` | `call-of-duty-4-modern-warfare-multiplayer.json` | `ux0:data/` | `CODVIT001` | `ux0:data/CODVIT001/` | ZIP |
| 22 | [Carnivores Dinosaur Hunter](../apps/carnivores-dinosaur-hunter.json) | `PSVCDH001` | `carnivores-dinosaur-hunter.json` | `ux0:data/` | `carnivoresdinosaurhunter` | `ux0:data/carnivoresdinosaurhunter/` | ZIP |
| 23 | [Carnivores Ice Age](../apps/carnivores-ice-age.json) | `PSVCSIA01` | `carnivores-ice-age.json` | `ux0:data/` | `carnivoresiceage` | `ux0:data/carnivoresiceage/` | ZIP |
| 24 | [Castle Crashers](../apps/castle-crashers.json) | `CCRV00001` | `castle-crashers.json` | `ux0:data/` | `CastleCrashers` | `ux0:data/CastleCrashers/` | ZIP |
| 25 | [Castle of Illusion](../apps/castle-of-illusion.json) | `COIV00001` | `castle-of-illusion.json` | `ux0:data/` | `castleofillusion` | `ux0:data/castleofillusion/` | ZIP |
| 26 | [COD WAW](../apps/cod-waw.json) | `WAW000001` | `cod-waw.json` | `ux0:data/` | `waw` | `ux0:data/waw/` | ZIP |
| 27 | [COD: BOZ Vita](../apps/cod-boz-vita.json) | `BOZ000001` | `cod-boz-vita.json` | `ux0:data/` | `boz` | `ux0:data/boz/` | ZIP |
| 28 | [Conker's Bad Fur Day](../apps/conker-s-bad-fur-day.json) | `CBFD00064` | `conker-s-bad-fur-day.json` | `ux0:data/` | `conker` | `ux0:data/conker/` | ZIP |
| 29 | [Crazy Taxi Vita](../apps/crazy-taxi-vita.json) | `CRAZYTAXI` | `crazy-taxi-vita.json` | `ux0:data/` | `crazytaxi` | `ux0:data/crazytaxi/` | ZIP |
| 30 | [CTR: High Octane](../apps/ctr-high-octane.json) | `CTR000004` | `ctr-high-octane.json` | `ux0:data/` | `ctr` | `ux0:data/ctr/` | ZIP |
| 31 | [Cuphead Vita](../apps/cuphead-vita.json) | `CPHD23233` | `cuphead-vita.json` | `ux0:app/` | — | `ux0:app/CPHD23233/` | Destino app |
| 32 | [Cut the Rope Classic](../apps/cut-the-rope-classic.json) | `CTROPE001` | `cut-the-rope-classic.json` | `ux0:data/` | `ctr` | `ux0:data/ctr/` | ZIP |
| 33 | [Cut the Rope: Magic](../apps/cut-the-rope-magic.json) | `CTRMAG001` | `cut-the-rope-magic.json` | `ux0:data/` | `ctrm` | `ux0:data/ctrm/` | ZIP |
| 34 | [D2Vita](../apps/d2vita.json) | `DTWO00001` | `d2vita.json` | `ux0:data/` | `d2vita` | `ux0:data/d2vita/` | ZIP |
| 35 | [Dead Space Vita](../apps/dead-space-vita.json) | `DEADSPACE` | `dead-space-vita.json` | `ux0:data/` | `deadspace` | `ux0:data/deadspace/` | ZIP |
| 36 | [Devil May Cry 4 Refrain Vita](../apps/devil-may-cry-4-refrain-vita.json) | `ACRODEA00` | `devil-may-cry-4-refrain-vita.json` | `ux0:data/` | `Acrodea` | `ux0:data/Acrodea/` | ZIP |
| 37 | [Digimon World](../apps/digimon-world.json) | `DGWD00001` | `digimon-world.json` | `ux0:data/` | `dw1` | `ux0:data/dw1/` | ZIP |
| 38 | [DJMAX Ray](../apps/djmax-ray.json) | `DJMR00001` | `djmax-ray.json` | `ux0:data/` | `djmaxray` | `ux0:data/djmaxray/` | ZIP |
| 39 | [Dragon Ball Tap Battle](../apps/dragon-ball-tap-battle.json) | `DBTB01178` | `dragon-ball-tap-battle.json` | `ux0:data/` | `DBTapBattle` | `ux0:data/DBTapBattle/` | ZIP |
| 40 | [Dragon Ball Z: BT3](../apps/dragon-ball-z-bt3.json) | `BTTD00010` | `dragon-ball-z-bt3.json` | `ux0:data/` | `bt3` | `ux0:data/bt3/` | ZIP |
| 41 | [DuckTales Remastered](../apps/ducktales-remastered.json) | `DUK000000` | `ducktales-remastered.json` | `ux0:data/` | `duck` | `ux0:data/duck/` | ZIP |
| 42 | [Dungeon Hunter 2](../apps/dungeon-hunter-2.json) | `PSVDH0002` | `dungeon-hunter-2.json` | `ux0:data/` | `dungeon-hunter-2` | `ux0:data/dungeon-hunter-2/` | ZIP |
| 43 | [DuskLigth TLOZ:TLP](../apps/duskligth.json) | `DUSKGXM01` | `duskligth.json` | `ux0:data/` | `dusklight` | `ux0:data/dusklight/` | ZIP |
| 44 | [Fallout 2 CE](../apps/fallout-2-ce.json) | `FOUT00002` | `fallout-2-ce.json` | `ux0:data/` | `fallout2` | `ux0:data/fallout2/` | ZIP |
| 45 | [Fallout CE](../apps/fallout-ce.json) | `FOUT00001` | `fallout-ce.json` | `ux0:data/` | `fallout` | `ux0:data/fallout/` | ZIP |
| 46 | [FF3-Vita](../apps/ff3-vita.json) | `FF3000000` | `ff3-vita.json` | `ux0:data/` | `ff3` | `ux0:data/ff3/` | ZIP |
| 47 | [FF4AY-Vita](../apps/ff4ay-vita.json) | `FF4AY0000` | `ff4ay-vita.json` | `ux0:data/` | `ff4a` | `ux0:data/ff4a/` | ZIP |
| 48 | [FF5-Vita](../apps/ff5-vita.json) | `FF5000000` | `ff5-vita.json` | `ux0:data/` | `ff5` | `ux0:data/ff5/` | ZIP |
| 49 | [FFD-Vita](../apps/ffd-vita.json) | `FFDIMEN01` | `ffd-vita.json` | `ux0:data/` | `ffd` | `ux0:data/ffd/` | ZIP |
| 50 | [Final Fantasy VI](../apps/final-fantasy-vi.json) | `FF6000000` | `final-fantasy-vi.json` | `ux0:data/` | `ff6` | `ux0:data/ff6/` | ZIP |
| 51 | [FINAL FANTASY VII](../apps/final-fantasy-vii.json) | `FFSQ00007` | `final-fantasy-vii.json` | `ux0:data/` | `ffvii-vita` | `ux0:data/ffvii-vita/` | ZIP |
| 52 | [Gangstar Miami Vindication](../apps/gangstar-miami-vindication.json) | `PSVGMV002` | `gangstar-miami-vindication.json` | `ux0:data/` | `gangstarmiamivindication` | `ux0:data/gangstarmiamivindication/` | ZIP |
| 53 | [GE007Vita](../apps/ge007vita.json) | `GEVT00001` | `ge007vita.json` | `ux0:data/` | `GoldenEye007` | `ux0:data/GoldenEye007/` | ZIP |
| 54 | [Geometry Dash Vita](../apps/geometry-dash-vita.json) | `GEOMETRYD` | `geometry-dash-vita.json` | `ux0:data/` | `gdash` | `ux0:data/gdash/` | ZIP |
| 55 | [Ghostship Vita](../apps/ghostship-vita.json) | `GHSTSHP01` | `ghostship-vita.json` | `ux0:data/` | `ghostship` | `ux0:data/ghostship/` | ZIP |
| 56 | [Golden Balloon](../apps/golden-balloon.json) | `GBLN00001` | `golden-balloon.json` | `ux0:data/` | `goldenballoon` | `ux0:data/goldenballoon/` | ZIP |
| 57 | [Gravity Guy](../apps/gravity-guy.json) | `GRVG00001` | `gravity-guy.json` | `ux0:data/` | `gravityguy` | `ux0:data/gravityguy/` | ZIP |
| 58 | [GTA: SA Vita](../apps/gta-sa-vita.json) | `GTASA0000` | `gta-sa-vita.json` | `ux0:data/` | `gtasa` | `ux0:data/gtasa/` | ZIP |
| 59 | [Gun Bros](../apps/gun-bros.json) | `BROS00001` | `gun-bros.json` | `ux0:data/` | `gunbros` | `ux0:data/gunbros/` | ZIP |
| 60 | [Gunforce 2](../apps/gunforce-2.json) | `GNFC00002` | `gunforce-2.json` | `ux0:data/` | `gunforce2` | `ux0:data/gunforce2/` | ZIP |
| 61 | [Half-Life 2](../apps/half-life-2.json) | `SRCV00001` | `half-life-2.json` | `ux0:data/` | `halflife2` | `ux0:data/halflife2/` | ZIP |
| 62 | [Halo CE](../apps/halo-ce.json) | `HCEV00001` | `halo-ce.json` | `ux0:data/` | `haloce-vita` | `ux0:data/haloce-vita/` | ZIP |
| 63 | [Hero of Sparta](../apps/hero-of-sparta.json) | `PSVHOS001` | `hero-of-sparta.json` | `ux0:data/` | `heroofsparta` | `ux0:data/heroofsparta/` | ZIP |
| 64 | [Hollow Knight Vita](../apps/hollow-knight-vita.json) | `HKVT12222` | `hollow-knight-vita.json` | `ux0:app/` | — | `ux0:app/HKVT12222/` | Destino app |
| 65 | [Ignition Vita](../apps/ignition-vita.json) | `IGNVITA12` | `ignition-vita.json` | `ux0:data/` | `ignition` | `ux0:data/ignition/` | ZIP |
| 66 | [Illusia](../apps/illusia.json) | `ILLU00001` | `illusia.json` | `ux0:data/` | `illusia` | `ux0:data/illusia/` | ZIP |
| 67 | [Into the Breach](../apps/into-the-breach.json) | `THEBREACH` | `into-the-breach.json` | `ux0:data/` | `breach` | `ux0:data/breach/` | ZIP |
| 68 | [JAVITA](../apps/javita.json) | `JAVITA001` | `javita.json` | `ux0:data/` | `JAVITA` | `ux0:data/JAVITA/` | ZIP |
| 69 | [Jazz Jackrabbit 2 Resurrection](../apps/jazz-jackrabbit-2-resurrection.json) | `JAZZ20000` | `jazz-jackrabbit-2-resurrection.json` | `ux0:data/` | `jazz2` | `ux0:data/jazz2/` | ZIP |
| 70 | [JK2VITA](../apps/jk2vita.json) | `JK2VITA01` | `jk2vita.json` | `ux0:data/` | `JK2Vita` | `ux0:data/JK2Vita/` | ZIP |
| 71 | [Kare wa Kanojo Vita](../apps/kare-wa-kanojo-vita.json) | `RPYKWKFAN` | `kare-wa-kanojo-vita.json` | `ux0:app/` | — | `ux0:app/RPYKWKFAN/` | Destino app |
| 72 | [Kingdom Hearts 358/2 Days](../apps/kingdom-hearts-358-2-days.json) | `KHDD00358` | `kingdom-hearts-358-2-days.json` | `ux0:data/` | `khdays` | `ux0:data/khdays/` | ZIP |
| 73 | [KOTOR](../apps/kotor.json) | `KOTR00001` | `kotor.json` | `ux0:data/` | `kotor` | `ux0:data/kotor/` | ZIP |
| 74 | [KOTOR II](../apps/kotor-ii.json) | `KOTR00002` | `kotor-ii.json` | `ux0:data/` | `kotor2` | `ux0:data/kotor2/` | ZIP |
| 75 | [LDhewm3 ReARMed](../apps/potatoom-3.json) | `DHEWM3ES2` | `potatoom-3.json` | `ux0:data/` | `dhewm3` | `ux0:data/dhewm3/` | ZIP |
| 76 | [Lighthouse Vita](../apps/lighthouse-vita.json) | `BANJO0064` | `lighthouse-vita.json` | `ux0:data/` | `lighthouse` | `ux0:data/lighthouse/` | Documentación / revisión |
| 77 | [LSWTCS Vita](../apps/lswtcs-vita.json) | `LSWCS0000` | `lswtcs-vita.json` | `ux0:data/` | `lswtcs` | `ux0:data/lswtcs/` | ZIP |
| 78 | [Manhunt](../apps/manhunt.json) | `MHRE00001` | `manhunt.json` | `ux0:data/` | `manhunt` | `ux0:data/manhunt/` | ZIP |
| 79 | [Mario Kart Wii](../apps/mario-kart-wii.json) | `MKWV00001` | `mario-kart-wii.json` | `ux0:data/` | `mkw` | `ux0:data/mkw/` | ZIP |
| 80 | [Mario Strikers Charged](../apps/mario-strikers-charged.json) | `MSCN00003` | `mario-strikers-charged.json` | `ux0:data/` | `mscharged` | `ux0:data/mscharged/` | ZIP |
| 81 | [Marvel vs Capcom 2](../apps/marvel-vs-capcom-2.json) | `MVSC00001` | `marvel-vs-capcom-2.json` | `ux0:data/` | `mvc2` | `ux0:data/mvc2/` | ZIP |
| 82 | [Mass Effect Infiltrator Vita](../apps/mass-effect-infiltrator-vita.json) | `MASSEFFEC` | `mass-effect-infiltrator-vita.json` | `ux0:data/` | `masseffect` | `ux0:data/masseffect/` | ZIP |
| 83 | [Max Payne](../apps/max-payne.json) | `MAXP00000` | `max-payne.json` | `ux0:data/` | `maxpayne` | `ux0:data/maxpayne/` | ZIP |
| 84 | [Modern Combat 2: Black Pegasus](../apps/modern-combat-2-black-pegasus.json) | `MC2V00001` | `modern-combat-2-black-pegasus.json` | `ux0:data/` | `moderncombat2` | `ux0:data/moderncombat2/` | ZIP |
| 85 | [Modern Combat 3: Fallen Nation](../apps/modern-combat-3-fallen-nation.json) | `MOCO30000` | `modern-combat-3-fallen-nation.json` | `ux0:data/` | `mc3` | `ux0:data/mc3/` | ZIP |
| 86 | [Modern Combat: Sandstorm](../apps/modern-combat-sandstorm.json) | `MCSS00001` | `modern-combat-sandstorm.json` | `ux0:data/` | `mc1` | `ux0:data/mc1/` | ZIP |
| 87 | [Monster Hunter Tri](../apps/monster-hunter-tri.json) | `MH3WII001` | `monster-hunter-tri.json` | `ux0:data/` | `mh3-wii` | `ux0:data/mh3-wii/` | ZIP |
| 88 | [Monster Shooter](../apps/monster-shooter.json) | `MNST00001` | `monster-shooter.json` | `ux0:data/` | `monstershooter` | `ux0:data/monstershooter/` | ZIP |
| 89 | [N.O.V.A. 3 Vita](../apps/n-o-v-a-3-vita.json) | `NOVA30001` | `n-o-v-a-3-vita.json` | `ux0:data/` | `nova3` | `ux0:data/nova3/` | ZIP |
| 90 | [NAOH: Arcade Collection](../apps/naoh-arcade-collection.json) | `NAOH00001` | `naoh-arcade-collection.json` | `ux0:data/` | `NaohAC` | `ux0:data/NaohAC/` | ZIP |
| 91 | [NBA 2K16](../apps/nba-2k16.json) | `NBA2K1600` | `nba-2k16.json` | `ux0:data/` | `nba2k16` | `ux0:data/nba2k16/` | Documentación / revisión |
| 92 | [NFS II SE Recompiled](../apps/nfs-ii-se-recompiled.json) | `NFSR00001` | `nfs-ii-se-recompiled.json` | `ux0:data/` | `nfs2-recomp` | `ux0:data/nfs2-recomp/` | ZIP |
| 93 | [NFS Most Wanted](../apps/nfs-most-wanted.json) | `NFSMWVITA` | `nfs-most-wanted.json` | `ux0:data/` | `nfsmw` | `ux0:data/nfsmw/` | ZIP |
| 94 | [NFSHP Vita](../apps/nfshp-vita.json) | `NFSHP0000` | `nfshp-vita.json` | `ux0:data/` | `nfshp` | `ux0:data/nfshp/` | ZIP |
| 95 | [Nitronic Rush](../apps/nitronic-rush.json) | `NRSH00001` | `nitronic-rush.json` | `ux0:data/` | `nitronic` | `ux0:data/nitronic/` | ZIP |
| 96 | [NOVA 2 HD](../apps/nova-2-hd.json) | `PSVNOVA02` | `nova-2-hd.json` | `ux0:data/` | `nova2` | `ux0:data/nova2/` | ZIP |
| 97 | [OpenMW Vita](../apps/openmw-vita.json) | `OMWV00001` | `openmw-vita.json` | `ux0:data/` | `openmw` | `ux0:data/openmw/` | ZIP |
| 98 | [Paper Mario Vita](../apps/paper-mario-vita.json) | `PPMR00001` | `paper-mario-vita.json` | `ux0:data/` | `papership` | `ux0:data/papership/` | ZIP |
| 99 | [PaperBoat](../apps/paperboat.json) | `PAPERBOAT` | `paperboat.json` | `ux0:data/` | `paperboat` | `ux0:data/paperboat/` | ZIP |
| 100 | [Pikmin](../apps/pikmin.json) | `NECTAR001` | `pikmin.json` | `ux0:data/` | `Pikmin` | `ux0:data/Pikmin/` | ZIP |
| 101 | [Plants vs Zombies 2](../apps/plants-vs-zombies-2.json) | `PVZ2VITA1` | `plants-vs-zombies-2.json` | `ux0:data/` | `pvz2` | `ux0:data/pvz2/` | ZIP |
| 102 | [Pocket Mortys Vita](../apps/pocket-mortys-vita.json) | `POMO00137` | `pocket-mortys-vita.json` | `ux0:app/POMO00137/` | — | `ux0:app/POMO00137/` | Destino app |
| 103 | [Powerpuff Girls Mojo Madness](../apps/powerpuff-girls-mojo-madness.json) | `PPGMV2000` | `powerpuff-girls-mojo-madness.json` | `ux0:data/` | `powerpuff_mojo` | `ux0:data/powerpuff_mojo/` | ZIP |
| 104 | [PREDATORS](../apps/predators.json) | `PRED42069` | `predators.json` | `ux0:app/` | — | `ux0:app/PRED42069/` | Destino app |
| 105 | [Prince of Persia Classic](../apps/prince-of-persia-classic.json) | `POPC00001` | `prince-of-persia-classic.json` | `ux0:data/` | `popclassic` | `ux0:data/popclassic/` | ZIP |
| 106 | [Pureya Vita](../apps/pureya-vita.json) | `PRYA22322` | `pureya-vita.json` | `ux0:app/` | — | `ux0:app/PRYA22322/` | Destino app |
| 107 | [Raging Thunder](../apps/raging-thunder.json) | `PSVRT0001` | `raging-thunder.json` | `ux0:data/` | `ragingthunder` | `ux0:data/ragingthunder/` | ZIP |
| 108 | [Real Racing 2](../apps/real-racing-2.json) | `RR2VITA01` | `real-racing-2.json` | `ux0:data/` | `rr2` | `ux0:data/rr2/` | ZIP |
| 109 | [Real Racing 3](../apps/real-racing-3.json) | `RR3VITA01` | `real-racing-3.json` | `ux0:data/` | `rr3` | `ux0:data/rr3/` | ZIP |
| 110 | [Red Ball 4 PS Vita](../apps/red-ball-4.json) | `RDBL40001` | `red-ball-4.json` | `ux0:data/` | `rdball4` | `ux0:data/rdball4/` | ZIP |
| 111 | [Resident Evil 4 GC](../apps/resident-evil-4-gc.json) | `BIOHAZ004` | `resident-evil-4-gc.json` | `ux0:data/` | `BIOHAZ004` | `ux0:data/BIOHAZ004/` | ZIP |
| 112 | [Resident Evil Code: Veronica X](../apps/resident-evil-code-veronica-x.json) | `RECVX0001` | `resident-evil-code-veronica-x.json` | `ux0:data/` | `recvx` | `ux0:data/recvx/` | ZIP |
| 113 | [Resident Evil Degeneration](../apps/resident-evil-degeneration.json) | `REDG00001` | `resident-evil-degeneration.json` | `ux0:data/` | `redegeneration` | `ux0:data/redegeneration/` | ZIP |
| 114 | [Resident Evil Mercenaries VS](../apps/resident-evil-mercenaries-vs.json) | `REMV00001` | `resident-evil-mercenaries-vs.json` | `ux0:data/` | `remercenariesvs` | `ux0:data/remercenariesvs/` | ZIP |
| 115 | [Sacred Odyssey](../apps/sacred-odyssey.json) | `PSVSOTROA` | `sacred-odyssey.json` | `ux0:data/` | `sacredodyssey` | `ux0:data/sacredodyssey/` | ZIP |
| 116 | [Scribblenauts Remix](../apps/scribblenauts-remix.json) | `SCRB00001` | `scribblenauts-remix.json` | `ux0:data/` | `scribblenauts` | `ux0:data/scribblenauts/` | ZIP |
| 117 | [Shadow Fight 2](../apps/shadow-fight-2.json) | `SHDF00002` | `shadow-fight-2.json` | `ux0:data/` | `shadowfight2` | `ux0:data/shadowfight2/` | ZIP |
| 118 | [Shadow Guardian](../apps/shadow-guardian.json) | `PSVSGHD01` | `shadow-guardian.json` | `ux0:data/` | `shadowguardian` | `ux0:data/shadowguardian/` | ZIP |
| 119 | [Ship of Harkinian Vita](../apps/ship-of-harkinian-vita.json) | `SHIPOFHAR` | `ship-of-harkinian-vita.json` | `ux0:data/` | `soh` | `ux0:data/soh/` | ZIP |
| 120 | [ShotgunK2Vita](../apps/shotgunk2vita.json) | `SGKV00002` | `shotgunk2vita.json` | `ux0:data/` | `shotgunk2vita_steam` | `ux0:data/shotgunk2vita_steam/` | ZIP |
| 121 | [Silent Hill 4: The Room](../apps/silent-hill-4-the-room.json) | `SILH00004` | `silent-hill-4-the-room.json` | `ux0:data/` | `sh4` | `ux0:data/sh4/` | ZIP |
| 122 | [Simpsons Hit & Run](../apps/simpsons-hit-run.json) | `ROADRAGE2` | `simpsons-hit-run.json` | `ux0:data/` | `simpsons` | `ux0:data/simpsons/` | ZIP |
| 123 | [Skate 3 Vita](../apps/skate-3-vita.json) | `SKTV00001` | `skate-3-vita.json` | `ux0:data/` | `SKATEVITA` | `ux0:data/SKATEVITA/` | ZIP |
| 124 | [Smash Melee Vita](../apps/smash-melee-vita.json) | `MLVITA002` | `smash-melee-vita.json` | `ux0:data/` | `melee` | `ux0:data/melee/` | ZIP |
| 125 | [Sonic Mania Vita](../apps/sonic-mania-vita.json) | `MANIA0001` | `sonic-mania-vita.json` | `ux0:data/` | `Mania` | `ux0:data/Mania/` | ZIP |
| 126 | [Sonic R](../apps/sonic-r.json) | `SONICR001` | `sonic-r.json` | `ux0:data/` | `sonicr` | `ux0:data/sonicr/` | ZIP |
| 127 | [Sonic Robo Blast 2 Kart](../apps/sonic-robo-blast-2-kart.json) | `SRB2KART0` | `sonic-robo-blast-2-kart.json` | `ux0:data/` | `srb2kart` | `ux0:data/srb2kart/` | ZIP |
| 128 | [Spaghetti Kart Vita](../apps/spaghetti-kart-vita.json) | `SPAGHETTI` | `spaghetti-kart-vita.json` | `ux0:data/` | `spaghetti` | `ux0:data/spaghetti/` | ZIP |
| 129 | [SSX (2012)](../apps/ssx-2012.json) | `SSXV00001` | `ssx-2012.json` | `ux0:data/` | `ssx` | `ux0:data/ssx/` | ZIP |
| 130 | [Star Fox Adventures](../apps/star-fox-adventures.json) | `STFX00001` | `star-fox-adventures.json` | `ux0:data/` | `stairfax` | `ux0:data/stairfax/` | ZIP |
| 131 | [Stardew Valley Enhanced](../apps/stardew-valley-enhanced.json) | `STDV00001` | `stardew-valley-enhanced.json` | `ux0:data/` | `StardewValley` | `ux0:data/StardewValley/` | ZIP |
| 132 | [Starship Vita](../apps/starship-vita.json) | `STARSHIP1` | `starship-vita.json` | `ux0:data/` | `starship` | `ux0:data/starship/` | ZIP |
| 133 | [Street Fighter IV CE](../apps/street-fighter-iv-ce.json) | `SF4CE0000` | `street-fighter-iv-ce.json` | `ux0:data/` | `sf4ce` | `ux0:data/sf4ce/` | ZIP |
| 134 | [Super Hexagon Vita](../apps/super-hexagon-vita.json) | `SHXV00001` | `super-hexagon-vita.json` | `ux0:data/` | `superhexagon` | `ux0:data/superhexagon/` | ZIP |
| 135 | [Super Mario All-Stars Vita](../apps/super-mario-all-stars-vita-allstars1.json) | `ALLSTARS1` | `super-mario-all-stars-vita-allstars1.json` | `ux0:data/` | `smworld` | `ux0:data/smworld/` | ZIP |
| 136 | [Super Mario All-Stars Vita](../apps/super-mario-all-stars-vita.json) | `ALLSTARSW` | `super-mario-all-stars-vita.json` | `ux0:data/` | `smworld` | `ux0:data/smworld/` | ZIP |
| 137 | [Super Mario Galaxy](../apps/super-mario-galaxy.json) | `SMGV00001` | `super-mario-galaxy.json` | `ux0:data/` | `smg` | `ux0:data/smg/` | ZIP |
| 138 | [Super Mario Strikers Vita](../apps/super-mario-strikers-vita.json) | `SMSVITA01` | `super-mario-strikers-vita.json` | `ux0:data/` | `strikersVita` | `ux0:data/strikersVita/` | ZIP |
| 139 | [Super Mario World Vita](../apps/super-mario-world-vita.json) | `SPRMRWRLD` | `super-mario-world-vita.json` | `ux0:data/` | `smworld` | `ux0:data/smworld/` | ZIP |
| 140 | [Tap Sonic](../apps/tap-sonic.json) | `TPSN00001` | `tap-sonic.json` | `ux0:data/` | `tapsonic` | `ux0:data/tapsonic/` | ZIP |
| 141 | [The Drifter](../apps/the-drifter.json) | `DRFT00001` | `the-drifter.json` | `ux0:app/` | — | `ux0:app/DRFT00001/` | Destino app |
| 142 | [The Impossible Game](../apps/the-impossible-game.json) | `PSVTIG001` | `the-impossible-game.json` | `ux0:data/` | `theimpossiblegame` | `ux0:data/theimpossiblegame/` | ZIP |
| 143 | [THUG Vita](../apps/thug-vita.json) | `THUG00001` | `thug-vita.json` | `ux0:data/` | `thug` | `ux0:data/thug/` | ZIP |
| 144 | [Tomb Raider 1 & 2 Classic Collection](../apps/tomb-raider-1-2-classic-collection.json) | `TMBRAIDER` | `tomb-raider-1-2-classic-collection.json` | `ux0:data/` | `tombraider` | `ux0:data/tombraider/` | ZIP |
| 145 | [Touhou12.3Vita Hisoutensoku](../apps/touhou12-3vita-hisoutensoku.json) | `THSV12302` | `touhou12-3vita-hisoutensoku.json` | `ux0:data/` | `touhou123vita` | `ux0:data/touhou123vita/` | ZIP |
| 146 | [Turbo Fast League](../apps/turbo-fast-league.json) | `TRBO00001` | `turbo-fast-league.json` | `ux0:data/` | `turbo` | `ux0:data/turbo/` | ZIP |
| 147 | [TwinsanityEvolved](../apps/twinsanityevolved.json) | `TWEV00001` | `twinsanityevolved.json` | `ux0:data/` | `twinsanity` | `ux0:data/twinsanity/` | ZIP |
| 148 | [TWoM Vita](../apps/twom-vita.json) | `TWOM00000` | `twom-vita.json` | `ux0:data/` | `twom` | `ux0:data/twom/` | ZIP |
| 149 | [Ultimate Spider-Man](../apps/ultimate-spider-man.json) | `USMR00001` | `ultimate-spider-man.json` | `ux0:data/` | `usm` | `ux0:data/usm/` | ZIP |
| 150 | [Ultimate Spiderman: Total Mayhem](../apps/ultimate-spiderman-total-mayhem.json) | `SPIDERMAN` | `ultimate-spiderman-total-mayhem.json` | `ux0:data/` | `spiderman` | `ux0:data/spiderman/` | ZIP |
| 151 | [UT99 Vita](../apps/ut99-vita.json) | `UNREALT99` | `ut99-vita.json` | `ux0:data/` | `ut99` | `ux0:data/ut99/` | ZIP |
| 152 | [Wonder Boy: The Dragon's Trap](../apps/wonder-boy-the-dragon-s-trap.json) | `WBDT00001` | `wonder-boy-the-dragon-s-trap.json` | `ux0:data/` | `wonderboy` | `ux0:data/wonderboy/` | Documentación / revisión |
| 153 | [Xash3D-FWGS](../apps/xash3d-fwgs.json) | `XASH10000` | `xash3d-fwgs.json` | `ux0:data/` | `xash3d` | `ux0:data/xash3d/` | ZIP |
| 154 | [Zenonia 1 Remastered Vita](../apps/zenonia-1-remastered-vita.json) | `ZNNA00002` | `zenonia-1-remastered-vita.json` | `ux0:data/` | `zenonia1` | `ux0:data/zenonia1/` | ZIP |
| 155 | [Zenonia 2 Remaster Vita](../apps/zenonia-2-remaster-vita.json) | `ZNNA00001` | `zenonia-2-remaster-vita.json` | `ux0:data/` | `zenonia2` | `ux0:data/zenonia2/` | ZIP |
| 156 | [Zenonia 3](../apps/zenonia-3.json) | `PSVZ00003` | `zenonia-3.json` | `ux0:data/` | `zenonia3` | `ux0:data/zenonia3/` | ZIP |
| 157 | [Zombie Infection](../apps/zombie-infection.json) | `PSVZI0001` | `zombie-infection.json` | `ux0:data/` | `zombieinfection` | `ux0:data/zombieinfection/` | ZIP |
| 158 | [Zombie Tsunami Vita](../apps/zombie-tsunami-vita.json) | `ZTSU00001` | `zombie-tsunami-vita.json` | `ux0:data/` | `zombietsunami` | `ux0:data/zombietsunami/` | ZIP |

## Variantes y enlaces Game Files múltiples

Si una aplicación ofrece variantes de idioma o varios enlaces, se relacionan aquí. La auditoría de un índice ZIP no prueba por sí sola que todos sus enlaces alternativos contengan la misma estructura.

| Aplicación | Enlace | `extract_path` | `installed_paths` | Ruta principal |
|---|---|---|---|---|
| Asphalt 8 Vita | Game Data Files ZIP - Mediafire | `ux0:data/` | `asphalt8` | `ux0:data/asphalt8/` |
| Asphalt 8 Vita | Game Data Files ZIP | `ux0:data/` | `asphalt8` | `ux0:data/asphalt8/` |
| Beach Buggy Racing Vita | Game Data Files ZIP - MediaFire | `ux0:data/` | `beachbuggyracing` | `ux0:data/beachbuggyracing/` |
| Beach Buggy Racing Vita | Game Data Files ZIP | `ux0:data/` | `beachbuggyracing` | `ux0:data/beachbuggyracing/` |
| Cut the Rope Classic | Game Data Files ZIP - Mediafire | `ux0:data/` | `ctr` | `ux0:data/ctr/` |
| Cut the Rope Classic | Game Data Files ZIP | `ux0:data/` | `ctr` | `ux0:data/ctr/` |
| Cut the Rope: Magic | Game Data Files ZIP - MediaFire | `ux0:data/` | `ctrm` | `ux0:data/ctrm/` |
| Cut the Rope: Magic | Game Data Files ZIP | `ux0:data/` | `ctrm` | `ux0:data/ctrm/` |
| D2Vita | Game Files English | `ux0:data/` | `d2vita` | `ux0:data/d2vita/` |
| D2Vita | Game Files Spanish | `ux0:data/` | `d2vita` | `ux0:data/d2vita/` |
| Dragon Ball Tap Battle | Game Files Japanese | `ux0:data/` | `DBTapBattle` | `ux0:data/DBTapBattle/` |
| Dragon Ball Tap Battle | Game Files Spanish | `ux0:data/` | `DBTapBattle` | `ux0:data/DBTapBattle/` |
| Dragon Ball Z: BT3 | Game Files English | `ux0:data/` | `bt3` | `ux0:data/bt3/` |
| Dragon Ball Z: BT3 | Game Files Latino MOD | `ux0:data/` | `bt3` | `ux0:data/bt3/` |
| DuskLigth TLOZ:TLP | Game Files USA | `ux0:data/` | `dusklight` | `ux0:data/dusklight/` |
| DuskLigth TLOZ:TLP | Game Files Spanish | `ux0:data/` | `dusklight` | `ux0:data/dusklight/` |
| Pocket Mortys Vita | Game Data Files ZIP Mediafire | `ux0:app/POMO00137/` | — | `ux0:app/POMO00137/` |
| Pocket Mortys Vita | Game Data Files ZIP | `ux0:app/POMO00137/` | — | `ux0:app/POMO00137/` |
| Prince of Persia Classic | Game Data Files ZIP - MediaFire | `ux0:data/` | `popclassic` | `ux0:data/popclassic/` |
| Prince of Persia Classic | Game Data Files ZIP | `ux0:data/` | `popclassic` | `ux0:data/popclassic/` |
| Red Ball 4 PS Vita | Game Data Files ZIP - MediaFire | `ux0:data/` | `rdball4` | `ux0:data/rdball4/` |
| Red Ball 4 PS Vita | Game Data Files ZIP | `ux0:data/` | `rdball4` | `ux0:data/rdball4/` |
| Simpsons Hit & Run | Game Files English | `ux0:data/` | `simpsons` | `ux0:data/simpsons/` |
| Simpsons Hit & Run | Game Files Spanish | `ux0:data/` | `simpsons` | `ux0:data/simpsons/` |
| Zenonia 3 | Game Data Files ZIP - MediaFire | `ux0:data/` | `zenonia3` | `ux0:data/zenonia3/` |
| Zenonia 3 | Game Data Files ZIP | `ux0:data/` | `zenonia3` | `ux0:data/zenonia3/` |
| Zombie Tsunami Vita | Game Data Files ZIP - MediaFire | `ux0:data/` | `zombietsunami` | `ux0:data/zombietsunami/` |
| Zombie Tsunami Vita | Game Data Files ZIP | `ux0:data/` | `zombietsunami` | `ux0:data/zombietsunami/` |

## Carpetas externas compartidas

Una misma carpeta puede pertenecer funcionalmente a varios ports. **No eliminarla como propiedad exclusiva de una aplicación**.

| Carpeta | Apps que la referencian (ID interno) |
|---|---|
| `ux0:data/ctr/` | `ctr-high-octane`, `cut-the-rope-classic` |
| `ux0:data/smworld/` | `super-mario-all-stars-vita`, `super-mario-all-stars-vita-allstars1`, `super-mario-world-vita` |

## Excepciones y notas

- **Animal Crossing Vita:** su ZIP extrae en `ux0:data/AnimalCrossing/rom/`, pero se identifica la carpeta principal `ux0:data/AnimalCrossing/`.
- **The Drifter:** el ZIP ya incluye `DRFT00001/`. Se extrae en `ux0:app/`, y no declara `installed_paths`.
- **Pocket Mortys Vita:** el ZIP contiene archivos sueltos para la aplicación; se extraen directamente en `ux0:app/POMO00137/`.
- **Batman: The Dark Knight Rises y NBA 2K16:** sus ZIP incluyen una carpeta `shader_cache/` independiente. La tabla registra solo las carpetas principales `kras/` y `nba2k16/`; su propiedad requiere revisión antes de borrar.
- **Wonder Boy: The Dragon's Trap:** `wonderboy/` proviene de instrucciones del port; el índice ZIP no fue accesible por error HTTP.
- **Rutas que coinciden con Title ID:** pueden estar en `ux0:data/`; únicamente el `extract_path` del enlace indica el destino real.
- **Data Files y otras clases de enlaces:** no forman parte de esta migración.

## Procedencia

- [Archivos fuente `apps/`](../apps/).
- [Auditoría de índices ZIP](audits/game_files_zip_roots_20261105.json).
- [Plan técnico de Game/Data Files](INSTALLED_GAME_DATA_MANAGEMENT_PLAN_20261105.md).
- [Script que genera este documento](../scripts/report_game_files_paths.py).

**Estado funcional:** los metadatos están listos para el catálogo; la lectura de `installed_paths` en el cliente PS Vita y su edición en el generador web siguen siendo fases de implementación separadas.
