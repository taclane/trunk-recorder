# smartnet-cc-probe

Tools for diagnosing SmartNet control channel reception independently of
trunk-recorder. Needs Python 3 with `numpy` and `scipy`, and `rtl_sdr`.

| Script | What it does |
|---|---|
| `analyze.py` | Channelizes one or more control channels out of an `rtl_sdr` u8 IQ capture (or `-` for stdin) and, once per second, reports CRC-valid OSWs/s (same framing, ECC and CRC as op25 `rx_smartnet`), SNR, carrier offset, FSK deviation, eye opening, and the share of samples outside trunk-recorder's ±1800 Hz PLL range. `--csv` writes the same per channel. |
| `pll_compare.py` | Runs trunk-recorder's `pll_freqdet_cf` settings and alternatives over a real capture, with added noise (`--snr-drop`) and carrier offset (`--offset`), to compare decode rates. |
| `capture.sh` | Pauses the wmata launchd job, captures both wmata dongles simultaneously, and restarts the job. |
| `monitor.sh` | Streams the spare dongles through `analyze.py` continuously, logging which control channel is live every second while trunk-recorder keeps running. |

Examples:

```bash
./analyze.py cap.u8 --center 496337000 --chan 496437500 --chan 496537500 --dump-cmds
./pll_compare.py cap.u8 --center 496337000 --chan 496437500 --offset 1500
rtl_sdr -d 91 -f 496337000 -s 1800000 -g 25.4 - | ./analyze.py - --center 496337000 --chan 496437500 --csv cc.csv
```
