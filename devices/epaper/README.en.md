# ePaper Device

This device type targets the `Waveshare ESP32-S3-ePaper-1.54 V2` as a HomeKit
ePaper accessory and ports the full V2 board logic into this project.

Runtime behavior:

- Exposes one HomeKit temperature sensor service
- Exposes one HomeKit humidity sensor service
- Exposes one HomeKit battery service
- Reads the onboard `SHTC3` temperature and humidity sensor
- Reads time from the onboard `PCF85063` RTC
- Measures battery voltage and derives a battery percentage
- Initializes the audio codec path for recording, recorded playback, and demo playback
- Mounts `Micro SD` for status snapshots and optional CSV logging
- Uses the onboard `BOOT/PWR` buttons for record, playback, SD snapshot, reset, and sleep actions
- Supports `RTC` timed wake and deep sleep
- Renders temperature, humidity, time, battery, SD, audio state, and wake reason on the built-in `1.54-inch` `200x200` ePaper panel

Migration sources:

- `Example/ESP-IDF/V2/12_RTC_Sleep_Test`
  Provides the board power sequence and the 1.54-inch ePaper init and refresh flow
- `Example/ESP-IDF/V2/03_I2C_SHTC3`
  Provides `SHTC3` init, CRC validation, and measurement conversion logic
- `Example/ESP-IDF/V2/02_I2C_PCF85063`
  Provides `PCF85063` RTC readout logic

It also reuses the upstream `codec_board` component for the audio and `Micro SD`
board support needed by `ES8311`, the speaker, the microphone, and the SD slot.

## Support Scope

- `V2` hardware only
- `V1` and `V2` use different chips, memory layouts, and official examples
- The default target chip is `esp32s3`
- The default memory profile is `8MB Flash / 8MB PSRAM`
- The default partition table is `partitions_hap_epaper.csv` to leave enough OTA space for the larger image

## Default Button Actions

- `BOOT` single click: record audio
- `BOOT` double click: play the latest recording
- `BOOT` long release:
  longer than `3s` resets Wi-Fi, longer than `10s` resets to factory settings
- `PWR` single click: play the built-in demo clip
- `PWR` double click: write `/sdcard/homekit-epaper-status.txt`
- `PWR` long release: enter deep sleep and wake later via the configured `RTC` interval

## Board Defaults

- ePaper SPI
  `DC=10`, `CS=11`, `SCLK=12`, `MOSI=13`, `RST=9`, `BUSY=8`
- Power control
  `PWR=6`, `AUDIO_PWR=42`, `VBAT_PWR=17`
- Buttons and RTC interrupt
  `BOOT=0`, `PWR_KEY=21`, `RTC_INT=5`
- I2C
  `SDA=47`, `SCL=48`
- Device addresses
  `PCF85063=0x51`, `SHTC3=0x70`
- Temperature compensation
  Default `-4.0 C`, mapped to `CONFIG_HOMEKIT_EPAPER_TEMP_OFFSET_DECICELSIUS=-40`

You can change these defaults in
`menuconfig -> Example Configuration -> ePaper Device Configuration`.

Useful tunables:

- `HOMEKIT_EPAPER_REFRESH_INTERVAL_SEC`
  Dashboard refresh interval
- `HOMEKIT_EPAPER_AUDIO_BUFFER_KB`
  Record buffer size, allocated from PSRAM by default
- `HOMEKIT_EPAPER_SLEEP_WAKE_INTERVAL_SEC`
  RTC wake interval used after deep sleep
- `HOMEKIT_EPAPER_LOW_BATTERY_LEVEL`
  HomeKit low-battery threshold
- `HOMEKIT_EPAPER_ENABLE_SD_LOGGING`
  Append readings to `/sdcard/homekit-epaper-log.csv`

## Quick Start

```sh
cp sdkconfig.defaults.local.epaper-v2.example sdkconfig.defaults.local
# Fill in your Wi-Fi credentials
# Format the Micro SD card as FAT32 if you want SD features

./scripts/build-device.sh epaper
./scripts/flash-device.sh epaper -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

Without the helper scripts:

```sh
rm -f sdkconfig sdkconfig.old
idf.py -DIDF_TARGET=esp32s3 -DHOMEKIT_DEVICE_TYPE=epaper reconfigure build
idf.py -p /dev/cu.usbmodemXXXX flash
idf.py -p /dev/cu.usbmodemXXXX monitor
```
