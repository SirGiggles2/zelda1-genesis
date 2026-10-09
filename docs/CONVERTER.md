# Zelda Genesis Converter

Builds the Genesis ROM on your own PC from files you supply. The package
contains no Nintendo game data and no Zelda Redux data.

## You need

1. **The Legend of Zelda (USA) PRG0** NES ROM in iNES format (`.nes`).
   It must be exactly this dump: the converter checks its SHA-256
   (`8f72dc2e…0440ac`).
2. **Zelda Redux v3.3.3**, the patch file `Zelda1_Redux.ips` from the Redux
   repository's v3.3.3 tag (SHA-256 `97703146…ea171`):
   https://raw.githubusercontent.com/ShadowOne333/The-Legend-of-Zelda-Redux/v3.3.3/patches/Zelda1_Redux.ips
   - Other Redux versions are rejected, because they don't match the data
     this port was made from.
3. **Python 3.11 or newer** for Windows (https://www.python.org/downloads/), with
   "Add python.exe to PATH" ticked during setup.

## Convert

1. Drag the NES ROM and the Redux download together onto `Converter.bat`.
   Alternatively, double-click `Converter.bat` and use **Browse...**.
2. On first use, press **Install requirements**. This installs numpy,
   scipy and py65 once and needs internet access.
3. Press **Convert**. The window shows each step. The Genesis ROM is
   saved next to your NES ROM as `Zelda.md`, unless you choose
   another place.

Your input files are only read, never changed.

## Command line

```
python tools/builder/build.py "<zelda.nes>" --redux-patch "<Redux zip or .ips>" --output "<out.md>"
```

`--redux "<Zelda Redux.nes>"` accepts an already patched Redux ROM
instead of the patch.

Linux: the same command works. The bundled SGDK toolchain is Windows
software and runs under Wine (`wine` must be on PATH). Install the Python requirements with
`python3 -m pip install -r tools/builder/requirements.txt`.

## Problems

- **"unsupported NES ROM":** this is a different dump or revision. You
  need The Legend of Zelda (USA) PRG0.
- **"does not turn this ROM into the supported Zelda Redux":** this is
  the wrong Redux version. Use v3.3.3.
- **Missing toolchain or SGDK files:** the package is incomplete. Extract
  it again in full.
