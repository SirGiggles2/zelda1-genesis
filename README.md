# Zelda for Sega Genesis / Mega Drive

This is a port of the original NES **The Legend of Zelda** to the Sega Genesis.
It includes Original and Redux modes, both quests, and local multiplayer for up
to four players. It's still a work in progress: there are bugs to find, and busy
multiplayer rooms can slow down.

You build the game from your own NES ROM. The builder runs on your computer;
your ROM never gets sent to a website.

## Build your ROM

1. Open [Downloads](https://github.com/SirGiggles2/zelda1-genesis/releases) and get
   the builder for **Windows**, **macOS** (Apple Silicon or Intel), or **Linux**.
2. Extract the whole download and open **Zelda Genesis Builder**.
3. Drop in your **The Legend of Zelda (USA), PRG0** `.nes` file.
4. Click **Build Genesis ROM**, then **Download ROM** when it finishes.

That's it. The app includes its own Python runtime and native build tools.
You don't need to install Python, Git, SGDK, Docker, or Wine.

The first build needs an internet connection to get the supported Zelda Redux
v3.3.3 patch. After that, the patch is cached. If you already have it, you can
choose it under **Advanced**. Your input ROM and patch are never changed.

Open the finished `.md` file in a Genesis emulator or on compatible hardware.
Development testing uses BizHawk, Genesis Plus GX, and BlastEm. Full hardware
and full-game testing are still ongoing.

## If it won't build

- **Wrong ROM:** the supported input is the USA PRG0 release, with an iNES
  header (131,088 bytes). Other revisions and already patched ROMs won't work
  as the original input.
- **Can't get the Redux patch:** reconnect and try again, or choose the
  [v3.3.3 IPS patch](https://raw.githubusercontent.com/ShadowOne333/The-Legend-of-Zelda-Redux/v3.3.3/patches/Zelda1_Redux.ips)
  under Advanced.
- **Something else:** open **Build details** and save the log. Include it in a
  [bug report](https://github.com/SirGiggles2/zelda1-genesis/issues), along with
  your operating system and what happened. Please don't attach your ROM.

## Building from source

The app and command-line converter use the same game source and build pipeline.
If you're working on the code, see [the source build guide](docs/CONVERTER.md).
The converter regenerates graphics, rooms and ROM-derived music from the files
you provide. Native tools are used on every platform.

## Credits


Thanks to everyone whose work helped make this possible.

**Music**

- **Inglebard** made the Genesis Overworld and Underworld arrangements in DefleMask.
- **Cyberdeous** made the alternate Underworld arrangement. Both artists' music
  was made without AI, and their arrangements remain available in the game.
- The other tracks are converted from your NES ROM and Redux patch. Their
  instruments and mixes were edited by SirGiggles in Music Studio. The builder
  now uses the revised mixes, including Title, Overworld, Level 9 and Ending.

**The original game**

- **Nintendo** — *The Legend of Zelda* (1986), directed by Shigeru Miyamoto and
  Takashi Tezuka, programmed by Toshihiko Nakago, with music by Koji Kondo.
  The songs Inglebard covered are Kondo's.

**Zelda Redux** — [The Legend of Zelda Redux](https://github.com/ShadowOne333/The-Legend-of-Zelda-Redux)

- **ShadowOne333** — creator and lead of Zelda Redux.
- And everyone ShadowOne333 credits for Redux: **Trax** (Zelda 1
  disassembly, feedback and help), **BogaaBogaa** (arrows, 999 rupees, HUD,
  credits, MMC5 work, cave-warp dialogue), **Fiskbit** (MMC1 animation),
  **DarkSamus993** (ASM help, Select-button item switch), **Stratoform** (Pols
  Voice flute, PRG1 Game Over port, cracked-wall collision, hearts, arrow
  limits), **snarfblam** (Automap), **gzip** (Select-button fix, waterfall
  animation, Zelda Hack Pack), **minucce** (HUD, scrolling, cave and door
  fixes, beam alignment, diagonal sword swing, Copy/Erase saves),
  **tacoschip** (dungeon sword/beam graphics, Dungeon Automap, dungeon music),
  **kalita-kan** (optional patches), **lexluthermeister** (new-bosses patch),
  **BlazeHeatnix** (script revisions), **BleakBluets** (Game Over shield
  sound), **vailkyte** (Dungeon Automap fix for levels 7–9), and the
  RomHacking.net community that gave feedback and ideas.

**Disassembly**

- **aldonunez** — the [Zelda 1 disassembly](https://github.com/aldonunez/zelda1-disassembly).
  Its labels, comments and RAM map are the reference this whole port was
  built and checked against.

**Tools**

- **Stephane Dallongeville** — [SGDK](https://github.com/Stephane-D/SGDK) and
  its XGM sound driver, which build and run this port.
- **Delek** — DefleMask Tracker, used to make the music.
- **The BizHawk team** and **Genesis Plus GX** (Eke-Eke)
  — the emulators the port was tested and compared in.

**This port**

- **SirGiggles** — Project direction, music edits, and a lot of playtesting.
- **Anthropic's Claude** and **OpenAI's Codex** — most of the code (see below).

## About the code

Most of the programming was done with Anthropic's Claude and OpenAI's Codex,
with direction and playtesting by SirGiggles. Automated comparisons against the
NES game help catch mistakes, but they don't replace playing it. Bug reports
and feedback are welcome.

## Legal

The Legend of Zelda belongs to Nintendo. This is an unofficial fan project and
isn't affiliated with Nintendo. No game ROM is included; you need your own
legally obtained copy. Zelda Redux is ShadowOne333's work and is downloaded
separately by the builder.

The project's own code uses the [MIT license](LICENSE). Third-party credits and
license terms are in [NOTICE](NOTICE).
