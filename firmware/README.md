# Display firmware (Waveshare ESP32-S3-Touch-LCD-1.85B)

ESP-IDF 5.3.2, LVGL 9.4.0 (downloaded by the build), NimBLE for Bluetooth. The board has a 360 x 360 round screen (ST77916 over QSPI), CST816S
touch, a speaker (ES8311 audio chip), a QMI8658 motion sensor, a BQ27220 battery gauge, and a connector for a lithium battery.

## Build and flash
On Windows, follow [WINDOWS.md](WINDOWS.md) instead. These steps are for macOS.

One time: install ESP-IDF 5.3.2 (it needs Homebrew's Python 3.11 first on the PATH; on an Intel Mac use /usr/local instead of /opt/homebrew):

    brew install python@3.11 cmake ninja dfu-util
    export PATH="/opt/homebrew/opt/python@3.11/libexec/bin:$PATH"
    mkdir -p ~/esp
    cd ~/esp
    git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git
    cd esp-idf
    ./install.sh esp32s3

Then, in every new Terminal window (the first two lines only work once per window), with the display plugged in:

    export PATH="/opt/homebrew/opt/python@3.11/libexec/bin:$PATH"
    source ~/esp/esp-idf/export.sh
    cd path/to/go-marker/firmware
    idf.py -B build185 -DSDKCONFIG=sdkconfig185 -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.185" -DNAV_BOARD=1_85B build
    ls /dev/cu.usbmodem*
    idf.py -B build185 -DSDKCONFIG=sdkconfig185 -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.185" -DNAV_BOARD=1_85B -p /dev/cu.usbmodemXXXX flash

The first build downloads LVGL and the screen, touch, and audio drivers, so it needs internet. Use the port name that `ls` printed in place of
`/dev/cu.usbmodemXXXX`. Do not pipe the `source` command into anything. Without `-DIDF_TARGET=esp32s3` a first build targets the original
ESP32 and runs out of RAM.

If the port does not appear, try another cable (some only charge) or flip the USB-C plug. To recover a stuck board, hold BOOT while plugging it in.
Progress and errors are printed over the same USB port (`idf.py -B build185 -p <port> monitor`).

The red light on the board means power, and the green light means the battery is charging.

## Layout
    main/app_main.c      start-up and the main loop: link, settings, splash, sound
    main/ble_server.c    the Bluetooth service and its characteristics (see ../proto/nav_protocol.h)
    main/lvgl_port.c     LVGL <-> panel glue: display, touch, tick, frame timing log ("perf:" lines)
    main/board185/       the board's drivers: screen, touch, I2C, battery gauge, motion sensor, speaker
    main/nav_nvs_store.c what is saved across power-offs: trip stats, brightness, voice, units, clock format, theme, page order, tire monitoring
    main/board_time.c    the clock chip (PCF85063)
    main/board_sound.c   the speaker (optional); main/voices/make_voices.py makes the spoken phrases
    ../ui  ../proto      the screens and the packet definitions, shared with the simulator
    ../sim/feed.c        the demo ride used when no phone is connected (NAV_DEMO_RIDE)

## Hard-won facts about this board
- Pins: I2C SDA 11 / SCL 10; QSPI CS 21, CLK 40, D0-D3 = 46/45/42/41, LCD reset 3; touch reset 1, touch interrupt 4; speaker I2S MCLK 2, BCLK 48,
  WS 38, DOUT 47, amplifier enable 9. The battery gauge answers at I2C address 0x55 and the motion sensor at 0x6B.
- The screen takes 18-bit color. LVGL renders 24-bit pixels, and a task on the second core converts them, adds a fine dither, and sends them to the panel while LVGL draws the next piece.
- The flash runs in QIO mode (`sdkconfig.defaults.185`): the code and fonts are read through the caches, so a faster flash shortens every frame.
- Big buffers (map data, images) must live in PSRAM. Static arrays in internal RAM starved the Bluetooth controller of memory once ("BLE_INIT: Malloc failed").
- LVGL's built-in memory pool is 64 KB on the board: avoid anything that makes LVGL allocate a full-screen layer (opacity, clipped corners, fades).
- Drawing is done by the CPU and is most of the frame time. The map page runs about 20 frames a second with no route and about 12 with a route showing,
  which looks smooth in use. What helped most: `-O2`, draw buffers in internal RAM, larger CPU caches, skipping map segments that are off screen, drawing slanted
  lines in short pieces, and filling parks and water with horizontal spans instead of triangles.
