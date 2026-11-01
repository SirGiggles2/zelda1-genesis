"""numpy-only resample_poly vs scipy on the 7 real DMC samples (T-419 Task 11).

If the extractor's final bytes (HINT u8 and XGM s8, after its round/clip) are
identical for every sample, extract_dmc_samples.py can drop scipy, the largest
download in the browser builder.
"""
from __future__ import annotations

import sys
from fractions import Fraction
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))
ROM = ROOT / "roms" / "Legend of Zelda, The (USA).nes"


def _samples():
    import extract_dmc_samples as x  # noqa: PLC0415
    prg = x.read_ines(ROM)
    for a12, a13, a10 in zip(x.SAMPLE_ADDR_BYTES, x.SAMPLE_LENGTH_BYTES, x.SAMPLE_RATE_BYTES):
        off = x.nes_addr_to_prg_offset(0xC000 + a12 * 64, prg)
        decoded = x.decode_dmc(bytes(prg[off:off + a13 * 16 + 1]))
        rate = x.dmc_rate_hz(a10 & 0x0F)
        yield np.asarray(decoded, dtype=np.float64) - 64.0, rate, x.GENESIS_HINT_HZ, x.XGM_PCM_HZ


def _final(resample, centered, rate, hint_hz, xgm_hz):
    f = Fraction(hint_hz, int(round(rate))).limit_denominator(1000)
    hint = np.clip(np.round(resample(centered, f.numerator, f.denominator) + 64.0), 0, 127).astype(np.uint8)
    g = Fraction(xgm_hz, int(round(rate))).limit_denominator(1000)
    xgm = np.clip(np.round(resample(centered, g.numerator, g.denominator) * 2.0), -128, 127).astype(np.int8)
    return hint.tobytes(), xgm.tobytes()


@pytest.mark.skipif(not ROM.is_file(), reason="needs the local NES ROM")
def test_numpy_resample_matches_scipy_on_real_samples():
    from scipy.signal import resample_poly as scipy_rp  # noqa: PLC0415
    from tools.nesdata.resample import resample_poly  # noqa: PLC0415
    count = 0
    for centered, rate, hint_hz, xgm_hz in _samples():
        assert _final(resample_poly, centered, rate, hint_hz, xgm_hz) == _final(scipy_rp, centered, rate, hint_hz, xgm_hz)
        count += 1
    assert count == 7
