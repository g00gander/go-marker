# Go Marker

Version: see the file `VERSION`. The build guide at https://nomarker.co/workbench/go-marker lists what changed in each version.

A small round navigation display for your bike. Your iPhone works out the route, the GPS, the map, and the weather and sends them over
Bluetooth. The display (a Waveshare ESP32-S3-Touch-LCD-1.46, or the 1.85B) only draws them.

**The Go Marker iPhone app is not part of this download.** It is being prepared for the App Store. Everything for the display is here: the
firmware, the screens, the simulator, and the Bluetooth packet definitions (`proto/nav_protocol.h`) that any phone app has to follow.

Besides the map, weather, trip, compass, and clock screens, the display has a Tires screen and a pulsing red ring on every screen when a tire is low.
A phone app feeds them with the tire pressure packet (TPMS) in `proto/nav_protocol.h`; the display only shows what it is sent.

**Build guide, parts list, and photos: https://nomarker.co/workbench/go-marker**

## Please read: this is not a safety device
Go Marker is a hobby project, provided as is, with no warranty (see LICENSE). Do not use it in a way that takes your attention off
the road. Set your route before you ride. Check the directions against the road and the signs yourself, and ride within your abilities
and the law.

## What is in here
    firmware/    ESP-IDF project for the display board
    ui/          the screens (LVGL 9), shared by the board and the simulator
    proto/       the Bluetooth packet definitions (nav_protocol.h), shared by everything
    sim/         a desktop simulator that draws the same screens on your computer
    tests/       small C tests

## Quick start
1. Optional, to see it on your computer: `brew install cmake sdl2`, clone LVGL 9.4.0 into `lvgl/` at the top of this folder
   (`git clone --depth 1 --branch v9.4.0 https://github.com/lvgl/lvgl.git lvgl`), then `cd sim && cmake -S . -B build && cmake --build build && ./build/nav_sim`.
2. Flash the display: install ESP-IDF 5.3.2 with Homebrew's Python 3.11 first on the PATH, then build and flash. The exact commands, for the 1.46 and for the 1.85B, are in `firmware/README.md`.
3. Optional spoken phrases: `python3 firmware/main/voices/make_voices.py` on a Mac, then rebuild the firmware.

The guide on the page above walks through each step, and has troubleshooting.

## License
Apache License 2.0, see LICENSE and NOTICE. The No Marker name and logo are not covered by it.
