# Zelda for Sega Genesis / Mega Drive

This is a port of the original NES **The Legend of Zelda** to the Sega Genesis.
It includes Original and Redux modes, both quests, and local multiplayer for up
to four players. It's still a work in progress: there are bugs to find, and busy
multiplayer rooms can slow down.

We can't share the game itself, so you make it yourself from your own copy of
the NES game. It only takes a minute, and nothing gets installed.

## Make your ROM

You need two files:

- **Your Zelda NES ROM.** It has to be the USA version (the first release,
  called "PRG0"). The file ends in `.nes`.
- **The Zelda Redux patch.** Download **Zelda1-Redux.zip** from the
  [Redux v3.3.3 releases page](https://github.com/ShadowOne333/The-Legend-of-Zelda-Redux/releases/tag/v3.3.3).
  You don't need to unzip it.

Then:

1. Go to [Releases](https://github.com/SirGiggles2/zelda1-genesis/releases) and
   download **Zelda-Genesis-Builder.html**. It's one file.
2. Double-click it. It opens in your web browser.
3. Drop your NES ROM in box **1** and the Redux zip in box **2** (or click a
   box to pick the file).
4. Click **Build**.
5. About a minute later, your browser saves **Zelda Genesis.md**. That's your game.

Play it in a Genesis emulator or on a Genesis flash cart.

A few things worth knowing:

- **The first time needs internet.** The page downloads a small helper (about
  20 MB) the first time. After that it's saved by your browser.
- **Your files stay on your computer.** Nothing gets uploaded anywhere. Your ROM
  and the patch aren't changed.
- **It works on Windows, Mac and Linux**, in any up-to-date browser. So far
  we've tested it in Microsoft Edge on Windows. If another browser gives you
  trouble, please tell us.
- **Every copy is the same.** The page checks that your game matches the
  official release exactly. If it doesn't, it won't save it.

## If it doesn't work

- **"This is not the USA PRG0 ROM."** Your ROM is a different version (like
  Europe, Japan or PRG1) or it's already been patched. You need the plain USA
  PRG0 one.
- **"This is an older Redux patch."** You have the old patch from the Redux
  source code. Download Zelda1-Redux.zip from the releases page linked above.
- **"This is not the Zelda Redux v3.3.3 patch."** Use the zip linked above.
  Other Redux versions won't work.
- **"Connect to the internet once…"** The page couldn't download its helper.
  Check your connection and click Build again.
- **Anything else:** open **Build details** at the bottom of the page, copy what
  it says, and post it in a
  [bug report](https://github.com/SirGiggles2/zelda1-genesis/issues). Say which
  computer and browser you used. Please don't attach your ROM.

## For developers

Making a new release of the builder is one command:

```
python tools/release/make_builder.py --rom "roms/Legend of Zelda, The (USA).nes" --redux "roms/Zelda Redux.nes"
```

It rebuilds the game from the ROMs, takes out everything that came from
Nintendo, packs the page, and checks that nothing from the ROMs is left in it.
Upload the `Zelda-Genesis-Builder.html` it makes in `builds/release/`.

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
legally obtained copy. Zelda Redux is ShadowOne333's work; you download the
patch from their page yourself.

The project's own code uses the [MIT license](LICENSE). Third-party credits and
license terms are in [NOTICE](NOTICE).
