# Zelda Genesis Builder

For normal use, get the app for your operating system from
[Downloads](https://github.com/SirGiggles2/zelda1-genesis/releases), extract it,
and open **Zelda Genesis Builder**. Choose your original Zelda NES ROM, click
**Build Genesis ROM**, and download the finished game. Python and the native
compiler are included. No Wine is used.

Your ROM stays local. The only game file the app downloads is the supported
Zelda Redux patch, from its original repository. The patch is checked against
its expected SHA-256 and cached for later builds. Advanced lets you supply the
same IPS patch or its archive yourself.

## Supported input

The original input is **The Legend of Zelda (USA), PRG0**, in iNES format:
131,088 bytes, SHA-256
`8f72dc2e98572eb4ba7c3a902bca5f69c448fc4391837e5f8f0d4556280440ac`.

Redux uses `Zelda1_Redux.ips` from the **v3.3.3** tag:
[download the patch](https://raw.githubusercontent.com/ShadowOne333/The-Legend-of-Zelda-Redux/v3.3.3/patches/Zelda1_Redux.ips).
Its SHA-256 is `97703146e386c07c440493aaaa3259b97f9128adb78f0718a4fee727094ea171`.
Other revisions are rejected before conversion.

## Working on the source

Clone with submodules so the pinned SGDK source and libraries are present:

```sh
git clone --recurse-submodules https://github.com/SirGiggles2/zelda1-genesis
cd zelda1-genesis
```

Use Python 3.11 or newer. `Converter.bat` on Windows, `Converter.command` on
macOS, and `Converter.sh` on Linux open the same local browser interface.
The source launcher installs missing Python dependencies into an isolated
`build/builder-venv`; it does not change your system Python packages.

On Windows the pinned SGDK compiler is used. On Linux and macOS the native
compiler and xgmtool must be present in `toolchains/native/bin` or supplied
through `ZELDA_TOOLCHAIN_BIN`. The target is **m68k-elf**, not a host compiler
or the Linux m68k ABI. Node is also needed to reproduce the saved music mixes.

To prepare native release tools (requires a host C/C++ compiler and Make on
Linux/macOS, plus internet access):

```sh
python tools/converter/prepare_native_tools.py
python -m pip install -r tools/converter/requirements.txt
python tools/converter/build.py --check-toolchain
```

Or run the converter directly:

```sh
python tools/converter/build.py "/path/to/zelda.nes" --redux-patch "/path/to/Zelda1_Redux.ips" --output "/path/to/Zelda.md"
```

`--redux "/path/to/Zelda Redux.nes"` accepts the authenticated, already
patched Redux ROM instead. Input files are never changed.

## App releases

The Native builder apps workflow builds separate Windows x64, Linux x64,
Intel Mac, and Apple Silicon Mac packages. Each contains the sanitized source,
the pinned SGDK libraries, native tools, and Python dependencies. The workflow
checks native compilation, XGM conversion, the local API, and packaged Python
child scripts. No NES ROM or extracted music is uploaded for these checks.

Release tooling lives in `tools/converter/prepare_native_tools.py`,
`package_app.py`, `native_smoke.py`, and `archive_app.py`. Full game conversion
must also be checked locally with the supported ROM before publication.

## Music

The builder recreates the revised Zelda mixes from the supplied ROM and
`data/music/builder_mix.json`. That file stores instruments and mix settings,
not the game's notes or extracted VGM files. Inglebard's and Cyberdeous's
arrangements remain available through the existing game music options.
