# esp32-cam-websrv

Video streaming webserver for AI Thinker ESP32-CAM boards.

Derived from Espressif's own CameraWebServer example sketch:

https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/Camera/CameraWebServer

## Differences from the official CameraWebServer sketch:

* Removed dependence on [arduino-esp32](https://github.com/espressif/arduino-esp32). This application can be built using only the [esp-idf](https://github.com/espressif/arduino-esp32) development framework.
* Removed all face recognition features.
* Added control for the ESP32-CAM's on-board _flash_ LED.
* Added read-only FAT12 partition.
* WIFI SSID and password are now configuration parameters that are read from a configuration file in the FAT12 partition.
* The static webpage data (HTML, CSS and Javascript) are now stored as separate files in the FAT12 parition instead of being hard-coded byte array in a header file.

## Build dependency components

* [esp32-camera](https://github.com/espressif/esp32-camera)
* Standard esp-idf components:
    * esp\_event
    * esp\_http\_server
    * esp\_timer
    * esp\_wifi
    * fatfs
    * nvs\_flash
    * vfs

## Setup

Configure, build and flash using the _usual_ IDF tools:

1. Clean

    ```
    $ idf.py fullclean
    Executing action: fullclean
    Executing action: remove_managed_components
    Done
    ```

2. Set target

    ```
    $ idf.py set-target esp32
    Executing action: set-target
    ...
    -- Configuring done
    -- Generating done
    -- Build files have been written to: /tmp/esp32-cam-websrv/build
    ```

3. Build

    ```
    $ idf.py build
    Executing action: all (aliases: build)
    ...
    Project build complete. To flash, run this command:
    python ../../../../../github/espressif/esp-idf/components/esptool_py/esptool/esptool.py -p (PORT) -b 460800 --before default_reset --after hard_reset --chip esp32  write_flash --flash_mode dio --flash_size detect --flash_freq 80m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/camwebsrv.bin 0x110000 build/storage.bin or run 'idf.py -p (PORT) flash'

    ```

4. Upload to the ESP32-CAM board, assuming that it is connected to `/dev/ttyUSB0`
    ```
    $ idf.py -p /dev/ttyUSB0 flash
    Executing action: flash
    ...
    Leaving...
    Hard resetting via RTS pin...
    Done
    ```

## Author

[Vino Fernando Crescini](mailto:vfcrescini@gmail.com)

## License

This software is released under the GPLv3 license. For more information, see [COPYING](https://github.com/vfcrescini/esp32-cam-websrv/blob/main/COPYING).
