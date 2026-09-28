# DISaveRedirect

ASI (`scripts\DISaveRedirect.asi` + `.ini`, loaded by UAL as winmm.dll). Build: `build.bat` (cl, x86).

Dead Island keeps saves + profile only in Steam Cloud through `ISteamRemoteStorage004`
(`Steam\userdata\<id>\91310\remote\out\{save,profiles}`). Only `engine_x86_rwdi.dll` imports
`SteamRemoteStorage`; the ASI swaps that IAT entry for a stand-in interface that does the file work in `SaveDir`.

Slots the engine calls (engine RVAs 0x1C96C0, 0x1C99B8, 0x1D48EA-0x1D4EF0): FileWrite 0x00, FileRead 0x04,
FileDelete 0x0C, SetSyncPlatforms 0x14 (name, 1), FileExists 0x18, GetFileSize 0x20, GetFileCount 0x2C,
GetFileNameAndSize 0x30. The stand-in fills slots 0-16 itself; the UGC slots 17-21 forward to the real interface.

`ImportSteamSaves = 1`: the first call that finds `SaveDir` empty copies every Steam Cloud file in through the
real interface (read only; Steam's copies are never written). Writes go to `<file>.tmp`, then MoveFileEx.

Tested 2026-09-27: boot imported all 6 files (default.pro/.prr, save_0.sar, save_0/1/2.sav), menu read the saves
from `saves\`. The user still needs to test save/load in game.

`BlockAchievements = 1`: also IAT-hooks `SteamUserStats` (ISteamUserStats010) with a forwarding stand-in that drops
SetStat (slots 3, 4) and SetAchievement (slot 7); the engine calls SetAchievement 0x1C, StoreStats 0x28, 0x58. Boot test 09-27: both hooks in, ACH_49 set at boot was blocked.
