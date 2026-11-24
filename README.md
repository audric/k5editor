# K5 Editor

A desktop editor for the memory channels and settings of a Quansheng UV-K5 handheld
running the **F4HWN v4.3** firmware. C++20 and Qt 6, Linux.

![K5 Editor showing the channel table](docs/screenshot.png)

## What it does

- Reads the radio over the usual programming cable (38400 baud) and shows the 200
  memory channels in an editable table: name, frequency, shift and offset, RX/TX tones,
  mode, width, power, step, the three scan lists and the other per-channel options.
- Edits the radio's settings, grouped as Radio, Display and sound, Keys and lock,
  Scanning, DTMF and Unlock.
- Flags channels whose stored data looks damaged or was left by another firmware.
- Opens and saves 8 KiB memory images.

## How it protects the radio

- Before every write it saves the radio's current memory to a dated backup file.
- It writes only the 8-byte blocks that changed, then reads the radio back and compares.
  The radio is restarted only if that check passes.
- It never writes the calibration area (`0x1D00`-`0x1FF1`).
- It refuses to write to any firmware other than F4HWN v4.3, because other firmwares
  store the same settings differently.
- If the radio changed the same memory since it was read (for example through its own
  menu), the write stops and asks.
- `UNLOCK ALL` for the TX frequency lock cannot be switched on from the program, as in
  the firmware author's own CHIRP driver. Do that in the radio's menu.

## Supported hardware and firmware

| | |
|---|---|
| Radio | Quansheng UV-K5 / UV-K5(8) / UV-K6 with the DP32G030 chip |
| Firmware | F4HWN v4.3 (any edition) |
| System | Linux |

Other firmwares can be read and saved to a file as a backup, but not edited or written.

## Build

On Debian or Ubuntu:

```sh
sudo apt install g++ cmake ninja-build qt6-base-dev
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build        # optional: runs the core tests
./build/k5editor
```

Your user needs access to the serial port, usually by being in the `dialout` group.

## Use

1. Connect the programming cable, switch the radio on, and press **Read from radio**.
2. Edit channels (double-click a cell) and settings. Changed values are shown in bold.
3. Press **Write to radio**. The program lists the changes and asks before sending them.

Backups go to `~/.local/share/k5editor/backups`; change that under
*File → Choose backup folder*.

Command line:

```sh
k5editor image.bin                     # open an image file
k5editor --dump /dev/ttyUSB0 out.bin   # read the radio to a file, without a window
```

## Not included

DTMF contacts, FM broadcast presets, calibration editing, the VFO state and firmware
flashing. The side-key action lists show every action the firmware family knows,
including ones a given edition was built without.

## Status

The core (packet format, memory layout, write planning and the write procedure) is
covered by automated tests that run against a simulated radio. Reading has been checked
against a real UV-K5 on F4HWN v4.3.

Use it at your own risk: it writes to your radio's memory. Keep the first backup it
makes; that file also contains your radio's calibration.

## Credits and licence

The memory layout was transcribed from the CHIRP driver for the Egzumer/F4HWN firmware
(`uvk5_egzumer_f4hwn_ver_4_3_0.py`, © Jacek Lipkowski SQ5BPF, Egzumer, JOC2 and others,
GPL v2 or later), which builds on SQ5BPF's reverse engineering of the UV-K5 protocol.
F4HWN firmware is by Armel F4HWN: <https://github.com/armel/uv-k5-firmware-custom>.

K5 Editor is free software under the GNU General Public License, version 3 or later.
See [LICENSE](LICENSE).
