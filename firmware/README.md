# Display firmware (Waveshare ESP32-S3-Touch-LCD-1.46)

ESP-IDF 5.3.2, LVGL 9.4.0 (downloaded by the build), NimBLE for Bluetooth.

## Build and flash
One time: install ESP-IDF 5.3.2 (it needs Homebrew's Python 3.11 first on the PATH; on an Intel Mac use /usr/local instead of /opt/homebrew):

    brew install python@3.11 cmake ninja dfu-util
    export PATH="/opt/homebrew/opt/python@3.11/libexec/bin:$PATH"
    mkdir -p ~/esp
    cd ~/esp
    git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git
    cd esp-idf
    ./install.sh esp32s3

Then, in every new Terminal window, with the display plugged in:

    export PATH="/opt/homebrew/opt/python@3.11/libexec/bin:$PATH"
    source ~/esp/esp-idf/export.sh
    cd path/to/go-marker/firmware
    idf.py set-target esp32s3
    idf.py build
    ls /dev/cu.usbmodem*
    idf.py -p /dev/cu.usbmodemXXXX flash

`set-target` is needed once. The first build downloads LVGL, so it needs internet. Use the port name that `ls` printed in place of
`/dev/cu.usbmodemXXXX`. Do not pipe the `source` command into anything.

If the port does not appear, flip the USB-C plug: one orientation reaches the ESP32-S3 and the other reaches a second chip. To recover a
stuck board, hold BOOT while plugging it in. Progress and errors are printed over the same USB port (`idf.py -p <port> monitor`).

## The 1.85B board (Waveshare ESP32-S3-Touch-LCD-1.85B, 360 x 360)
Same project, a different board: it needs its own build folder and settings. After the one-time ESP-IDF install above, open a new Terminal window (the first two lines below only work once per window) and run:

    export PATH="/opt/homebrew/opt/python@3.11/libexec/bin:$PATH"
    source ~/esp/esp-idf/export.sh
    cd path/to/go-marker/firmware
    idf.py -B build185 -DSDKCONFIG=sdkconfig185 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.185" -DNAV_BOARD=1_85B build
    ls /dev/cu.usbmodem*
    idf.py -B build185 -DSDKCONFIG=sdkconfig185 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.185" -DNAV_BOARD=1_85B -p /dev/cu.usbmodemXXXX flash

The first build downloads the screen, touch, and audio drivers. The 1.46 and the 1.85B use separate build folders (`build` and `build185`), so you can switch between them.
Board code for the 1.85B is in `main/board185/`. The red light on this board means power, and the green light means the battery is charging.

## Layout
    main/app_main.c      start-up and the main loop: link, settings, splash, sound
    main/ble_server.c    the Bluetooth service and its characteristics (see ../proto/nav_protocol.h)
    main/lvgl_port.c     LVGL <-> panel glue: display, touch, tick, frame timing log ("perf:" lines)
    main/board/          board drivers (see ../NOTICE for their source)
    main/nav_nvs_store.c what is saved across power-offs: trip stats, brightness, voice, units, clock format, theme, page order
    main/board_time.c    the clock chip (PCF85063)
    main/board_sound.c   the speaker (optional); main/voices/make_voices.py makes the spoken phrases
    ../ui  ../proto      the screens and the packet definitions, shared with the simulator
    ../sim/feed.c        the demo ride used when no phone is connected (NAV_DEMO_RIDE)

## Hard-won facts about this board (all verified on the real hardware)
- The panel needs byte-swapped RGB565. Do it in `flush_cb` with `lv_draw_sw_rgb565_swap()`.
- No `esp_lcd_panel_mirror()` call: the vendor's mirror setting produced a mirrored image.
- Partial-redraw areas must be aligned to 4 px in X (the rounder in `lvgl_port.c`).
- The LCD reset line is on an I2C IO expander (TCA9554, EXIO2): `I2C_Init` and `EXIO_Init` must run first.
- Pins: I2C SDA 11 / SCL 10; QSPI CLK 40, D0-D3 = 46/45/42/41, CS 21; backlight GPIO 5 (PWM); touch interrupt GPIO 4 (SPD2010 at 0x53);
  IMU QMI8658 at 0x6A/0x6B; clock PCF85063 at 0x51; speaker I2S DIN 47, LRCK 38, BCK 48.
- Big buffers (map data, images) must live in PSRAM. Static arrays in internal RAM starved the Bluetooth controller of memory once ("BLE_INIT: Malloc failed").
- LVGL's built-in memory pool is 64 KB on the board: avoid anything that makes LVGL allocate a full-screen layer (opacity, clipped corners, fades).
- Drawing is done by the CPU and is most of the frame time. About 11 frames a second on the map page, which looks smooth in use.
  What helped most: `-O2`, draw buffers in internal RAM, larger CPU caches, skipping map segments that are off screen, drawing slanted
  lines in short pieces, and filling parks and water with horizontal spans instead of triangles.
