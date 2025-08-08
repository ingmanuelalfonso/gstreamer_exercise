# GainBP — GStreamer Gain + Band-Pass Audio Filter

Custom GStreamer plugin that applies **gain** and a **band-pass filter** (high-pass + low-pass) to audio input from a microphone.

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
```
💡 Tip: Use headphones to avoid echo or feedback when monitoring your microphone input.

### Build
```bash
make clean && make
```
Export Plugin Path
Before running, make sure GStreamer can find the plugin:
```bash
export GST_PLUGIN_PATH="$PWD:$GST_PLUGIN_PATH"
```
### How to use with Microphone (PulseAudio default)
Here you can change gain, lowcut, highcut values for testing different configurations
```bash
gst-launch-1.0 -v \
  pulsesrc ! audioconvert ! audioresample \
  ! audio/x-raw,format=F32LE,rate=48000,channels=1 \
  ! gainbp gain=1.4 lowcut=120 highcut=3400 \
  ! autoaudiosink
```
### How to use Sine Test (no microphone required)
```
gst-launch-1.0 -v \
  audiotestsrc ! audioconvert ! audioresample \
  ! audio/x-raw,format=F32LE,rate=48000,channels=1 \
  ! gainbp gain=2.0 lowcut=200 highcut=3000 \
  ! autoaudiosink
```

