"""Independent frame-state oracle for private NES-converted VGM fixtures.

python tools/converter/test_nes_music.py --rom YOUR.nes --fixtures PRIVATE_FOLDER
Optional --redux YOUR_PATCHED.nes also identifies which input matches.
No fixtures, register traces, or generated music belong in the public repo.
"""
from __future__ import annotations
import argparse
import gzip
import json
import struct
from pathlib import Path

from nes_music import ROOT, SONGS, generate


def model(data: bytes):
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    if data[:4] != b"Vgm " or len(data) < 64:
        raise ValueError("not a VGM")
    u32 = lambda p: struct.unpack_from("<I", data, p)[0]
    if u32(4)+4 != len(data):
        raise ValueError("VGM EOF length mismatch")
    clocks = (u32(12), u32(44))
    start = 0x34+u32(0x34) if u32(8) >= 0x150 and u32(0x34) else 0x40
    loop_offset = 0x1C+u32(0x1C) if u32(0x1C) else None
    loop_frame = None
    regs, keys, latch = [[0]*256 for _ in range(2)], [0]*6, 0
    psg = [0]*8
    frames, edges = [], []
    at, samples, ended = start, 0, False
    while at < len(data):
        if at == loop_offset:
            loop_frame = len(frames)
        cmd = data[at]
        at += 1
        if cmd in (0x52,0x53):
            reg, val = data[at:at+2]
            at += 2
            port = cmd-0x52
            regs[port][reg] = val
            if port == 0 and reg == 0x28:
                code = val & 7
                if code in (0,1,2,4,5,6):
                    ch = code if code < 3 else code-1
                    keys[ch] = val >> 4
                    edges.append((ch, keys[ch]))
        elif cmd == 0x50:
            val = data[at]
            at += 1
            if val & 128:
                latch = (val >> 4) & 7
                psg[latch] = (psg[latch] & 0x3F0) | (val & 15)
            elif latch % 2 == 0 and latch < 6:
                psg[latch] = (psg[latch] & 15) | ((val & 63) << 4)
            else:
                psg[latch] = val & 15
        elif cmd == 0x62:
            row = []
            for ch in range(6):
                r, c = regs[ch//3], ch%3
                # Compare pitch only while keyed, but all operator/envelope
                # and stereo registers: matching notes alone can hide wrong timbre.
                pitch = (((r[0xA4+c]&7)<<8)|r[0xA0+c], (r[0xA4+c]>>3)&7) if keys[ch] else (0,0)
                patch = tuple(r[base+s+c] for base in (0x30,0x40,0x50,0x60,0x70,0x80,0x90)
                              for s in (0,4,8,12)) + (r[0xB0+c],r[0xB4+c])
                row.append((keys[ch], *pitch, patch))
            frames.append((row, tuple(psg), tuple(edges)))
            edges = []
            samples += 735
        elif cmd == 0x66:
            ended = True
            break
        else:
            raise ValueError(f"unsupported command ${cmd:02X} at ${at-1:X}; expected 60Hz register stream")
    if not ended or samples != u32(24):
        raise ValueError("VGM end/sample count mismatch")
    if loop_offset is not None and (loop_frame is None or u32(32) != samples-loop_frame*735):
        raise ValueError("invalid VGM loop/sample metadata")
    return clocks, loop_frame, frames


def compare(actual: bytes, expected: bytes, name: str):
    a_clocks, a_loop, a = model(actual)
    b_clocks, b_loop, b = model(expected)
    if (a_clocks,a_loop,len(a)) != (b_clocks,b_loop,len(b)):
        raise ValueError(f"{name}: clocks/loop/frame count differ: {(a_clocks,a_loop,len(a))} vs {(b_clocks,b_loop,len(b))}")
    for f, (af, bf) in enumerate(zip(a,b)):
        for ch, (ac,bc) in enumerate(zip(af[0],bf[0])):
            if ac != bc:
                raise ValueError(f"{name}: frame {f}, FM{ch+1}: key/note/operator volume or patch mismatch")
        if af[1] != bf[1]:
            raise ValueError(f"{name}: frame {f}, PSG: period/noise/attenuation mismatch")
        if af[2] != bf[2]:
            raise ValueError(f"{name}: frame {f}, FM key transitions differ")
    return {"frames":len(a), "loop_frame":a_loop, "byte_identical":actual == expected}


def self_check():
    """Prove the verifier rejects altered pitch, key, TL, noise and loop."""
    from nes_music import Vgm
    w = Vgm()
    for reg,val in ((0xA4,0x22),(0xA0,0x40),(0x48,4),(0x28,0xF0)):
        w.fm(0,reg,val)
    w.psg(0xE4)
    w.psg(0xF2)
    w.loop = (0,0)
    w.frame()
    base = w.bytes()
    for offset in (69,72,75,77,79,28):
        mutant = bytearray(base)
        mutant[offset] ^= 1
        try:
            compare(bytes(mutant),base,"negative control")
        except (ValueError,IndexError):
            continue
        raise ValueError(f"verifier accepted mutation at {offset}")
    print("Verifier negative controls: PASS (pitch, TL, key, noise, attenuation, loop)")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rom",type=Path,required=True)
    p.add_argument("--redux",type=Path)
    p.add_argument("--fixtures",type=Path,required=True)
    p.add_argument("--out",type=Path,default=ROOT/"build/nes_music_test")
    args = p.parse_args()
    self_check()
    matched, reports = [], {}
    for kind,rom in (("original",args.rom),("redux",args.redux)):
        if rom is None:
            continue
        out = args.out/kind
        capture_report = generate(rom,out,compile_xgm=False)
        results = {}
        for name,_ in SONGS:
            ref = args.fixtures/f"{name}.vgm"
            if not ref.is_file():
                raise ValueError(f"missing private reference: {ref}")
            try:
                result = compare((out/ref.name).read_bytes(),ref.read_bytes(),name)
                results[name] = {"status":"PASS",**result}
                print(f"{kind} {name}: PASS {result['frames']} frames, all 6 FM + 4 PSG channels, "
                      f"loop {result['loop_frame']}; byte-identical={result['byte_identical']}")
            except ValueError as exc:
                results[name] = {"status":"FAIL","error":str(exc)}
                print(f"{kind}: FAIL {exc}")
        reports[kind] = {"rom_sha256":capture_report["rom_sha256"],"songs":results}
        if all(r["status"] == "PASS" for r in results.values()):
            matched.append(kind)
    args.out.mkdir(parents=True,exist_ok=True)
    (args.out/"verdict.json").write_text(json.dumps({"matches":matched,"reports":reports},indent=2)+"\n")
    print(f"VERDICT: {'PASS' if matched else 'FAIL'}; matching ROMs: {', '.join(matched) or 'none'}")
    return 0 if matched else 1


if __name__ == "__main__":
    raise SystemExit(main())
