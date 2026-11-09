# Flashing the display from Windows

These steps put the firmware on the Waveshare ESP32-S3-Touch-LCD-1.85B from a Windows 10 or 11 PC. I built and tested the firmware on a Mac. These Windows
steps follow Espressif's own Windows guide and I have not run them on a Windows machine, so if something differs, trust Espressif's
[Windows guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/get-started/windows-setup.html) over this page.

You need the display, a USB-C cable that carries data (some only charge), and about 6 GB of free disk space.

## 1. Install ESP-IDF 5.3.2 (once)

1. Download the **ESP-IDF Tools Installer** from [dl.espressif.com/dl/esp-idf](https://dl.espressif.com/dl/esp-idf/). The small online installer is fine. The offline one works without internet.
2. Run it. When it asks which version to install, pick **v5.3.2**. Do not pick a newer one (6.x): this firmware is built and tested only on 5.3.2. It installs Git, Python, the ESP32 compiler, CMake, and Ninja for you.
3. Keep the install folder it suggests, or use a short one with no spaces, such as `C:\esp`. Espressif says the path must be under 90 characters and must not contain spaces or parentheses.
4. On the last page, leave "Run ESP-IDF Command Prompt" checked, or open it later from the Start menu: type `ESP-IDF` and choose **ESP-IDF 5.3 CMD**.

Use that ESP-IDF Command Prompt for every step below. A normal Command Prompt or PowerShell window does not know where the tools are.

## 2. Get the code (once)

In the ESP-IDF Command Prompt:

    cd C:\
    git clone https://github.com/g00gander/go-marker.git

This makes `C:\go-marker`. Keep the path short and free of spaces. Windows has a path length limit, and the build makes deep folders.

## 3. Build it

    cd C:\go-marker\firmware
    idf.py -B build185 -DSDKCONFIG=sdkconfig185 -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.185" -DNAV_BOARD=1_85B build

This is one long line. Paste it as a single line and press Enter once. Do not split it: the line-continuation characters differ between Command Prompt and PowerShell, and a split command runs as separate commands.

The first build downloads the screen library and the screen, touch, and audio drivers, so it needs internet and takes a few minutes. It ends with
"Project build complete". Without `-DIDF_TARGET=esp32s3` a first build targets the original ESP32 and runs out of memory.

## 4. Find the display's COM port

Plug the display into the PC. Press **Windows + X**, open **Device Manager**, and expand **Ports (COM & LPT)**. The display shows up as
**USB Serial Device (COMx)**, for example `COM5`. Unplug it and plug it back in to see which entry comes and goes.

If no port appears:
- Try another cable (some only charge) or flip the USB-C plug.
- Hold the **BOOT** button while plugging it in. The board then shows up as a different USB device, which is the recovery mode for flashing.
- If Device Manager lists it under **Other devices** or with a yellow warning, Windows is missing a driver. Right-click it, choose **Update driver**, then **Search automatically**.

## 5. Flash it

Use your port name in place of `COM5`:

    idf.py -B build185 -DSDKCONFIG=sdkconfig185 -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.185" -DNAV_BOARD=1_85B -p COM5 flash

When it finishes you see "Hash of data verified" three times and then "Hard resetting". The display restarts and shows the splash screen, which stays up until
your phone connects.

To watch the display's log, run the same command with `-p COM5 monitor` in place of `flash`. Press **Ctrl + ]** to leave it. Close the monitor before you flash again, because only one program can use the port at a time.

## If something goes wrong

- **The build ends with "unknown target" or "command ^ is not known":** the command was split over several lines. Paste the build command as one line. If you already ran a split version, delete the `firmware\build185` folder and the `firmware\sdkconfig185` file, then run the one-line command again.
- **"idf.py is not recognized":** you are in a normal Command Prompt or PowerShell window. Open **ESP-IDF 5.3 CMD** from the Start menu.
- **"Failed to connect" or "Could not open COM5":** another program (a serial monitor, the Arduino IDE) may have the port open. Close it, or re-plug the display. If that does not help, hold **BOOT** while plugging in, then flash again.
- **A build error about a file name or path being too long:** move the code closer to the drive root (`C:\go-marker`) or turn on Windows long paths: run `git config --system core.longpaths true` in an Administrator Command Prompt, and enable "Long paths" in Group Policy or the registry (`LongPathsEnabled`).
- **The build stops partway after an earlier attempt failed:** delete the `firmware\build185` folder and run the build line again.
