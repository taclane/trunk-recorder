#!/usr/bin/env python3
"""Compare FM detectors for SmartNet on a real capture, with added noise.

Extracts one control channel from an rtl_sdr u8 capture at 18 ksps, then
decodes it (same framing/ECC/CRC as op25 rx_smartnet) through different
frequency detectors, at the capture's own SNR and with extra AWGN:

  quad       open-loop quadrature discriminator (angle of x[n]*conj(x[n-1]))
  tr_pll     trunk-recorder's pll_freqdet_cf: loop bw 2/sps, limit +/-pi/sps
  pll_wide   same loop, limit +/-pi/2 (+/-4.5 kHz)
  <custom>   --pll BW:LIMIT_HZ adds more PLL variants

usage: pll_compare.py FILE --center HZ --chan HZ [--seconds N] [--snr-drop DB ...]
"""
import argparse
import math

import numpy as np
from scipy import fft

from analyze import CH_RATE, SPS, Channel, find_osws, symbol_stats


def pll_freqdet(y, loop_bw, max_freq):
    """Port of gr::analog::pll_freqdet_cf (GNU Radio 3.10)."""
    damp = math.sqrt(2) / 2
    denom = 1 + 2 * damp * loop_bw + loop_bw * loop_bw
    alpha = 4 * damp * loop_bw / denom
    beta = 4 * loop_bw * loop_bw / denom
    ph = np.angle(y).astype(np.float64)
    out = np.empty(len(y), dtype=np.float64)
    phase = freq = 0.0
    two_pi = 2 * math.pi
    for i in range(len(y)):
        out[i] = freq
        err = ph[i] - phase
        err = (err + math.pi) % two_pi - math.pi
        freq += beta * err
        phase += freq + alpha * err
        phase = (phase + math.pi) % two_pi - math.pi
        if freq > max_freq:
            freq = max_freq
        elif freq < -max_freq:
            freq = -max_freq
    return out * CH_RATE / (2 * math.pi)


def quad(y):
    d = np.angle(y[1:] * np.conj(y[:-1])) * CH_RATE / (2 * np.pi)
    return np.concatenate([[0.0], d])


def count_osws(freq_hz):
    syms = symbol_stats(freq_hz)
    off = float(np.median(syms))
    for _ in range(3):
        s = syms - off
        if (s > 0).any() and (s <= 0).any():
            off += (s[s > 0].mean() + s[s <= 0].mean()) / 2
    s = syms - off
    best = 0
    for bits in ((s > 0), (s <= 0)):
        _, loose = find_osws(bits.astype(np.uint8))
        best = max(best, len(loose))
    return best


def extract(path, center, rate, chan, seconds):
    n = int(seconds * rate) // 100 * 100
    raw = np.memmap(path, dtype=np.uint8, mode="r")[: 2 * n].astype(np.float32)
    raw = (raw - 127.4) / 128.0
    x = raw[0::2] + 1j * raw[1::2]
    c = Channel(chan, center, rate, n)
    X = fft.fft(x)
    snr = 10 * np.log10(np.mean(np.abs(X[c.chbins]) ** 2) / np.median(np.abs(X) ** 2) - 1)
    return fft.ifft(X[c.idx] * c.mask) * (c.nout / n), snr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--center", type=float, required=True)
    ap.add_argument("--rate", type=int, default=1800000)
    ap.add_argument("--chan", type=float, required=True)
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--offset", type=float, default=0, help="extra carrier offset to inject, Hz")
    ap.add_argument("--snr-drop", type=float, action="append", help="dB of AWGN-induced SNR loss to test")
    ap.add_argument("--pll", action="append", default=[], help="extra PLL variant BW:LIMIT_HZ")
    args = ap.parse_args()

    y, snr = extract(args.file, args.center, args.rate, args.chan, args.seconds)
    if args.offset:
        y = y * np.exp(2j * np.pi * args.offset * np.arange(len(y)) / CH_RATE)
    sig_pwr = np.mean(np.abs(y) ** 2) * (1 - 10 ** (-snr / 10))
    noise_pwr = np.mean(np.abs(y) ** 2) - sig_pwr

    lim = lambda hz: 2 * math.pi * hz / CH_RATE
    variants = {
        "quad": quad,
        "tr_pll": lambda v: pll_freqdet(v, 2.0 / SPS, math.pi / SPS),
        "pll_wide": lambda v: pll_freqdet(v, 2.0 / SPS, lim(4500)),
    }
    for spec in args.pll:
        bw, hz = spec.split(":")
        variants[f"pll_{bw}_{hz}"] = lambda v, bw=float(bw), hz=float(hz): pll_freqdet(v, bw, lim(hz))

    drops = [0.0] + (args.snr_drop or [3, 6, 9, 12, 15])
    rng = np.random.default_rng(1)
    print(f"channel {args.chan/1e6:.4f} MHz, capture SNR {snr:.1f} dB, {args.seconds:.0f} s, injected offset {args.offset:+.0f} Hz")
    print(f"{'SNR dB':>7} " + " ".join(f"{k:>10}" for k in variants) + "   (OSW/s)")
    for drop in drops:
        target = snr - drop
        extra = sig_pwr / 10 ** (target / 10) - noise_pwr if drop else 0.0
        v = y
        if extra > 0:
            v = y + np.sqrt(extra / 2) * (rng.standard_normal(len(y)) + 1j * rng.standard_normal(len(y)))
        rates = [count_osws(f(v)) / args.seconds for f in variants.values()]
        print(f"{target:>7.1f} " + " ".join(f"{r:>10.1f}" for r in rates), flush=True)


if __name__ == "__main__":
    main()
