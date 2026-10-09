# zelda1-genesis

A port of **The Legend of Zelda** (NES, 1986) to the **Sega Genesis / Mega
Drive**. It plays like the NES original, and it has Zelda Redux built in as an
option you can switch on in the game.

This repository doesn't contain the game. It contains a converter: you give it
your own copy of the NES ROM, and it builds the Genesis ROM on your computer.

## What you need

- **Windows 10 or 11.** Linux works too, see [below](#linux).
- **Python 3.11 or newer** from https://www.python.org/downloads/. During setup,
  tick **"Add python.exe to PATH"**.
- **Git** from https://git-scm.com/downloads, to download this repository.
- **Your NES ROM:** *The Legend of Zelda (USA)*, revision PRG0, as a `.nes`
  file. It has to be this exact version. The converter checks its fingerprint
  (SHA-256 `8f72dc2e98572eb4ba7c3a902bca5f69c448fc4391837e5f8f0d4556280440ac`)
  and tells you if it's a different one.
- **The Zelda Redux v3.3.3 patch,** `Zelda1_Redux.ips`. Use this exact file
  (the download on the Redux release page is a different file and is
  rejected):
  https://raw.githubusercontent.com/ShadowOne333/The-Legend-of-Zelda-Redux/v3.3.3/patches/Zelda1_Redux.ips

## Step 1: download this repository

Open **PowerShell**, go to the folder you want to use, and run:

```powershell
git clone --recurse-submodules https://github.com/SirGiggles2/zelda1-genesis
```

This creates a `zelda1-genesis` folder. The `--recurse-submodules` part matters:
it also downloads SGDK, the Genesis toolkit the converter builds with. The green
"Download ZIP" button on GitHub leaves SGDK out, so use the command above.

## Step 2: convert

**The easy way:** open the `zelda1-genesis` folder and drag your NES ROM and the
Redux patch together onto **`Converter.bat`**. In the window that opens:

1. Press **Install requirements**. You only need to do this the first time; it
   needs an internet connection.
2. Press **Convert**. It takes a few minutes and shows each step.

Your Genesis ROM is saved next to your NES ROM as `Zelda.md` (you can pick
another name or folder before converting). Load it in a Genesis emulator; it
is tested in Genesis Plus GX and BizHawk. Your NES ROM and the patch are only
read, never changed.

**From the command line instead**, inside the `zelda1-genesis` folder:

```powershell
python -m pip install -r tools\builder\requirements.txt
python tools\builder\build.py "C:\path\to\zelda.nes" --redux-patch "C:\path\to\Zelda1_Redux.ips" --output "C:\path\to\Zelda.md"
```

## If something goes wrong

- **"unsupported ROM":** your NES ROM isn't the USA PRG0 version.
- **"does not turn this ROM into the supported Zelda Redux":** you have a
  different Redux version. Download v3.3.3.
- **"Zelda Redux is required":** you only gave it the NES ROM. Add the patch.
- **Missing SGDK or toolchain files:** the repository was downloaded without
  `--recurse-submodules`. Inside the folder, run
  `git submodule update --init`, then try again.
- **`python` isn't recognized:** reinstall Python and tick "Add python.exe to
  PATH".

## Linux

The same command line works. The SGDK toolchain is a Windows program, so it
runs under Wine; install Wine and make sure `wine` is on your PATH.

## How it works

The converter (`tools/builder/build.py`) checks both inputs, applies the Redux
patch to a copy of your ROM, and pulls everything the port needs out of the two
ROMs: graphics, rooms, music, text and the game's own tables. The title, item,
level 9, Ganon, Triforce, Zelda rescue and ending songs are converted from your
NES ROM to Genesis FM/PSG music on your computer. Then it compiles
the Genesis ROM with SGDK. In our tests the same inputs have always produced
exactly the same ROM, byte for byte, so you can compare your build with
anyone else's.

More detail is in [docs/CONVERTER.md](docs/CONVERTER.md). The release checks
(`tools/builder/from_scratch_gate.py`, `package_closure.py`, `make_package.py`,
`rom_bytes_scan.py`) confirm that conversion works from a clean copy, gives the
same ROM every time, and that no game data is in this repository.

## Credits

This port stands on a lot of other people's work. Thank you, all of you.

**Music**

- **Inglebard** — the Genesis/Mega Drive covers of the Overworld and
  Underworld themes that play in this port, made in DefleMask.

  All the other songs are extracted and converted from your NES ROM (but tbh
  they don't sound as good as what Inglebard did).

  **Cyberdeous** They (they're a duo) graciously made their own version of underworld, "Dungeon Jungle". Pick it in
  Options > UW MUSIC (Inglebard's stays the default).

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

- **SirGiggles** — Dude who slammed his fists at the keyboard till Claude did what he wanted, and playtested it to DEATH.
- **Anthropic's Claude** and **OpenAI's Codex** — most of the code (see below).

## AI disclosure

This project was built mostly by AI coding agents, Anthropic's Claude and
OpenAI's Codex, directed and play-tested by the maintainer. That includes most
of the game code, the converter, the tests and this README. The port was checked
against the original NES game with automated byte-for-byte comparisons rather
than taken on trust, but treat it as you would any hobby project. Bug reports
are welcome.

## Legal

*The Legend of Zelda* is Nintendo's. This project is not affiliated with or
endorsed by Nintendo, and it includes no Nintendo ROM, graphics, music or level
data. You need your own legally obtained copy of the game. Zelda Redux is
ShadowOne333's work and is not included either.

This project's own code is under the MIT license ([LICENSE](LICENSE)).
Third-party parts and their terms are listed in [NOTICE](NOTICE).
