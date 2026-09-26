#!/usr/bin/env python3
"""Offline SmartNet control channel analyzer for rtl_sdr u8 IQ captures.

For each requested channel, once per block (~1 s) it reports:
  osw/s      CRC-valid OSWs decoded (independent decoder, same framing,
             deinterleave, ECC and CRC as op25 rx_smartnet)
  snr_db     in-channel power vs. the capture's median noise floor
  off_hz     carrier offset from the nominal frequency (FM discriminator mean)
  dev_hz     measured FSK deviation (mean |symbol - carrier| at symbol centers)
  eye_db     eye opening: 20*log10(mean|sym| / std(sym - cluster mean))
  clip%      share of discriminator samples beyond +/-1800 Hz of the nominal
             frequency -- the lock range of trunk-recorder's pll_freqdet_cf

usage: analyze.py FILE --center HZ --rate SPS --chan HZ [--chan HZ ...] [--csv out.csv]
"""
import argparse
import csv
import sys
import time

import numpy as np
from scipy import fft

SYM_RATE = 3600
SPS = 5
CH_RATE = SYM_RATE * SPS  # 18 kHz, same as trunk-recorder's SmartNet chain
PLL_LIMIT_HZ = CH_RATE / (2 * SPS)  # pi/sps rad/sample -> 1800 Hz
SYNC = np.array([1, 0, 1, 0, 1, 1, 0, 0], dtype=np.uint8)  # 0xAC
FRAME = 84
PAYLOAD = 76


def crc_ok(ecc):
    accum, op = 0x0393, 0x036E
    for j in range(27):
        op = (op >> 1) ^ 0x0225 if op & 1 else op >> 1
        if ecc[j]:
            accum ^= op
    given = 0
    for j in range(10):
        given = (given << 1) | (~int(ecc[27 + j]) & 1)
    return given == accum


def decode_osw(frame76):
    raw = np.empty(PAYLOAD, dtype=np.uint8)
    for k in range(PAYLOAD // 4):
        for l in range(4):
            raw[k * 4 + l] = frame76[k + l * 19]
    exp = raw.copy()
    exp[1] = raw[0]
    exp[3::2] = raw[2::2] ^ raw[0:-2:2]
    syn = exp ^ raw
    ecc = raw[0::2][:37].copy()
    ecc ^= syn[1:74:2] & syn[3:76:2]
    if not crc_ok(ecc):
        return None
    bits = lambda a: int("".join(str(int(b)) for b in a), 2)
    addr = bits(ecc[0:16]) ^ 0xCC38
    grp = ~int(ecc[16]) & 1
    cmd = bits(ecc[17:27]) ^ 0x0D5
    return addr, grp, cmd


def find_osws(bits):
    """Return (strict, loose) OSW lists. strict also requires the sync that
    rx_smartnet expects right after the frame."""
    n = len(bits)
    if n < FRAME + 8:
        return [], []
    win = np.lib.stride_tricks.sliding_window_view(bits, 8)
    starts = np.nonzero((win == SYNC).all(axis=1))[0] + 8
    strict, loose = [], []
    for s in starts:
        if s + PAYLOAD > n:
            break
        osw = decode_osw(bits[s:s + PAYLOAD])
        if osw is None:
            continue
        loose.append(osw)
        if s + FRAME <= n and (bits[s + PAYLOAD:s + FRAME] == SYNC).all():
            strict.append(osw)
    return strict, loose


def symbol_stats(freq):
    """Pick symbol timing per sub-block, return symbol-center samples."""
    mf = np.convolve(freq, np.ones(SPS) / SPS, mode="same")
    out = []
    sub = SPS * 720
    for i in range(0, len(mf) - SPS, sub):
        seg = mf[i:i + sub]
        dc = np.median(seg)
        best = max(range(SPS), key=lambda p: np.mean(np.abs(seg[p::SPS] - dc)))
        out.append(seg[best::SPS])
    return np.concatenate(out) if out else np.array([])


def iq_blocks(path, nblk, hop):
    """Yield (sample_pos, uint8 IQ) blocks of nblk samples, advancing by hop.
    path "-" streams from stdin (e.g. rtl_sdr ... - | analyze.py -)."""
    if path != "-":
        raw = np.memmap(path, dtype=np.uint8, mode="r")
        for pos in range(0, len(raw) // 2 - nblk + 1, hop):
            yield pos, raw[2 * pos:2 * (pos + nblk)]
        return
    src = sys.stdin.buffer
    buf = bytearray()
    pos = 0
    while True:
        while len(buf) < 2 * nblk:
            chunk = src.read(2 * nblk - len(buf))
            if not chunk:
                return
            buf += chunk
        yield pos, np.frombuffer(bytes(buf), dtype=np.uint8)
        del buf[:2 * hop]
        pos += hop


class Channel:
    def __init__(self, freq, center, rate, nblk):
        self.freq = freq
        self.nout = nblk * CH_RATE // rate
        off = freq - center
        self.bin = int(round(off * nblk / rate))
        m = np.fft.fftfreq(self.nout, 1 / CH_RATE)
        # flat to 6.5 kHz, raised-cosine to zero at 8.5 kHz
        a = np.abs(m)
        self.mask = np.where(a < 6500, 1.0, np.where(a > 8500, 0.0, 0.5 * (1 + np.cos(np.pi * (a - 6500) / 2000))))
        self.idx = (self.bin + np.round(m * nblk / rate).astype(int)) % nblk
        self.chbins = self.idx[a < 6000]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--center", type=float, required=True)
    ap.add_argument("--rate", type=int, default=1800000)
    ap.add_argument("--chan", type=float, action="append", required=True)
    ap.add_argument("--block", type=float, default=1.0, help="seconds per report")
    ap.add_argument("--start-epoch", type=float, default=0, help="label rows with wall-clock time")
    ap.add_argument("--csv")
    ap.add_argument("--dump-cmds", action="store_true", help="print a histogram of OSW commands per channel")
    args = ap.parse_args()

    rate = args.rate
    hop = int(args.block * rate) // 100 * 100
    guard = 36000  # 20 ms each side, discarded after filtering
    nblk = hop + 2 * guard
    chans = [Channel(f, args.center, rate, nblk) for f in args.chan]
    ghop = guard * CH_RATE // rate

    if args.file == "-" and not args.start_epoch:
        args.start_epoch = time.time()
    writer = None
    if args.csv:
        fh = open(args.csv, "w", newline="")
        writer = csv.writer(fh)
        writer.writerow(["t", "freq", "osw_strict", "osw_loose", "polarity", "snr_db", "off_hz", "dev_hz", "eye_db", "clip_pct"])

    cmd_hist = {c.freq: {} for c in chans}
    print(f"{'t':>7} " + " | ".join(f"{c.freq/1e6:>10.4f} osw  snr  off   dev  eye clip" for c in chans))
    for pos, block in iq_blocks(args.file, nblk, hop):
        iq = block.astype(np.float32)
        iq = (iq - 127.4) / 128.0
        x = iq[0::2] + 1j * iq[1::2]
        X = fft.fft(x)
        pwr = np.abs(X) ** 2
        floor = np.median(pwr)
        t = pos / rate
        row = []
        for c in chans:
            y = fft.ifft(X[c.idx] * c.mask) * (c.nout / nblk)
            y = y[ghop:len(y) - ghop]
            snr = 10 * np.log10(max(np.mean(pwr[c.chbins]) / floor - 1, 1e-3))
            d = np.angle(y[1:] * np.conj(y[:-1])) * CH_RATE / (2 * np.pi)
            clip = 100.0 * np.mean(np.abs(d) > PLL_LIMIT_HZ)
            syms = symbol_stats(d)
            off = float(np.median(syms))
            for _ in range(3):  # carrier = midpoint of the two FSK clusters
                s = syms - off
                if (s > 0).any() and (s <= 0).any():
                    off += (s[s > 0].mean() + s[s <= 0].mean()) / 2
            s = syms - off
            pos_c, neg_c = s[s > 0], s[s <= 0]
            dev = (np.mean(pos_c) - np.mean(neg_c)) / 2 if len(pos_c) and len(neg_c) else 0.0
            spread = np.sqrt((np.sum((pos_c - pos_c.mean()) ** 2) + np.sum((neg_c - neg_c.mean()) ** 2)) / max(len(s), 1)) if len(pos_c) and len(neg_c) else 1.0
            eye = 20 * np.log10(max(dev, 1e-3) / max(spread, 1e-3))
            best = ([], [], "+")
            for pol in ("+", "-"):
                bits = ((s > 0) if pol == "+" else (s <= 0)).astype(np.uint8)
                strict, loose = find_osws(bits)
                if len(loose) > len(best[1]):
                    best = (strict, loose, pol)
            strict, loose, pol = best
            for _, _, cmd in loose:
                cmd_hist[c.freq][cmd] = cmd_hist[c.freq].get(cmd, 0) + 1
            nsec = len(y) / CH_RATE
            row.append(f"{len(loose)/nsec:>4.0f} {snr:>4.0f} {off:>+5.0f} {dev:>5.0f} {eye:>4.1f} {clip:>4.1f}")
            if writer:
                writer.writerow([f"{args.start_epoch + t:.1f}", c.freq, len(strict), len(loose), pol, f"{snr:.1f}", f"{off:.0f}", f"{dev:.0f}", f"{eye:.1f}", f"{clip:.2f}"])
        print(f"{t:>7.1f} " + " | ".join(f"{'':>10} {r}" for r in row), flush=True)
        if writer:
            fh.flush()

    if args.dump_cmds:
        for f, h in cmd_hist.items():
            top = sorted(h.items(), key=lambda kv: -kv[1])[:15]
            print(f"{f/1e6:.4f}: " + ", ".join(f"0x{k:03x}:{v}" for k, v in top))


if __name__ == "__main__":
    sys.exit(main())
