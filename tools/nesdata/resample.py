"""numpy-only scipy.signal.resample_poly (default window ('kaiser', 5.0), padtype constant 0).

Same filter design and output alignment as scipy 1.17: firwin(2*half_len+1, 1/max_rate)
scaled to unit DC gain, multiplied by `up`, zero-padded so the output samples are
centred, then upsample-filter-downsample. Floating-point summation order differs
from scipy's C upfirdn; tools/nesdata/test_resample.py proves the extractor's
rounded bytes are identical on every real DMC sample. Lets the browser builder
skip scipy (its largest download). T-419 Task 11.
"""
from __future__ import annotations

from math import gcd

import numpy as np


def _kaiser(m: int, beta: float) -> np.ndarray:
    n = np.arange(0, m)
    alpha = (m - 1) / 2.0
    return np.i0(beta * np.sqrt(1 - ((n - alpha) / alpha) ** 2.0)) / np.i0(beta)


def _firwin_lowpass(numtaps: int, cutoff: float, beta: float = 5.0) -> np.ndarray:
    alpha = 0.5 * (numtaps - 1)
    m = np.arange(0, numtaps) - alpha
    h = cutoff * np.sinc(cutoff * m)
    h = h * _kaiser(numtaps, beta)
    return h / np.sum(h)


def _output_len(len_h: int, n_in: int, up: int, down: int) -> int:
    return ((n_in - 1) * up + len_h - 1) // down + 1


def _upfirdn_range(h: np.ndarray, x: np.ndarray, up: int, down: int, first: int, count: int) -> np.ndarray:
    """Outputs first..first+count of upfirdn(h, x, up, down), polyphase: output k is
    sum over input i of x[i] * h[k*down - i*up] (only taps that hit real samples)."""
    k = np.arange(first, first + count, dtype=np.int64)
    base = k * down
    taps = (len(h) + up - 1) // up + 1
    i = (base // up)[:, None] - np.arange(taps, dtype=np.int64)[None, :]
    j = base[:, None] - i * up
    ok = (i >= 0) & (i < len(x)) & (j >= 0) & (j < len(h))
    terms = np.where(ok, x[np.clip(i, 0, len(x) - 1)] * h[np.clip(j, 0, len(h) - 1)], 0.0)
    return terms.sum(axis=1)


def resample_poly(x, up: int, down: int) -> np.ndarray:
    x = np.asarray(x, dtype=np.float64)
    g = gcd(up, down)
    up, down = up // g, down // g
    if up == down == 1:
        return x.copy()
    n_in = x.shape[0]
    n_out = n_in * up
    n_out = n_out // down + bool(n_out % down)
    max_rate = max(up, down)
    half_len = 10 * max_rate
    h = _firwin_lowpass(2 * half_len + 1, 1.0 / max_rate) * up
    n_pre_pad = down - half_len % down
    n_post_pad = 0
    n_pre_remove = (half_len + n_pre_pad) // down
    while _output_len(len(h) + n_pre_pad + n_post_pad, n_in, up, down) < n_out + n_pre_remove:
        n_post_pad += 1
    h = np.concatenate((np.zeros(n_pre_pad), h, np.zeros(n_post_pad)))
    return _upfirdn_range(h, x, up, down, n_pre_remove, n_out)
