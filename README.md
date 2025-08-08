# GainBP — GStreamer Gain + Band-Pass Audio Filter

Custom GStreamer element that applies **gain** and a **band-pass filter** (high-pass + low-pass) to audio streams.

- **Element factory name:** `gainbp`
- **Caps:** `audio/x-raw, format=F32LE, channels=1..8, rate=1..192000`
- **Properties:**
  - `gain` (double, default `1.0`) — linear gain factor (e.g., 2.0 ≈ +6 dB)
  - `lowcut` (double, default `100.0`) — HPF cutoff in Hz
  - `highcut` (double, default `8000.0`) — LPF cutoff in Hz

---

## Requirements (Ubuntu 22.04)

```bash
sudo apt update
sudo apt install -y build-essential pkg-config \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
