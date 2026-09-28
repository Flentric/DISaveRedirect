# DISaveRedirect

Keeps **Dead Island** (2011, Steam) saves and profile in a folder you choose instead of Steam Cloud only. One ASI.

Stock Dead Island stores its saves and profile only in Steam Cloud. With this ASI they go to a normal folder
(`saves\` in the game folder by default), so you can back them up, copy them or keep separate sets.

## Features

- **SaveDir:** save and profile folder, relative to the game folder or a full path. Empty = Steam Cloud as normal.
- **ImportSteamSaves:** on first run, your Steam Cloud saves are copied in. The Steam copies are never changed.
- Safe writes: each file is written to a `.tmp` file first, then swapped in.
- **BlockAchievements** (off by default): achievements and Steam stats never unlock, for testing.

Settings are in `DISaveRedirect.ini`.

## Install

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) in the game folder as
   `winmm.dll` (as `dinput8.dll` it can fail to launch under the Steam overlay).
2. Put `DISaveRedirect.asi` and `DISaveRedirect.ini` in `Dead Island\scripts\`.

Remove the ASI to go back to Steam Cloud. Saves made while it was installed stay in `SaveDir`.

## Build

Visual Studio (x86 C compiler). Edit the `vcvars32.bat` path in `build.bat`, then run it. Output:
`DISaveRedirect.asi`. It must be 32-bit.

`NOTES.md` explains how the Steam Remote Storage interface is replaced.

## License

MIT
