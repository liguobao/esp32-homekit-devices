# Dashboard Device

This is a three-light HomeKit panel device type.

Runtime behavior:

- Exposes 3 independent HomeKit light services
- Uses the left `ST7789` strip for time and weather
- Uses the right `ST7789/ST7899` compatible strip for IP and light state

Recommended wiring (classic `ESP32`):

- Shared SPI
  `GPIO18` -> both panels `SCL/CLK/SCK`
  `GPIO23` -> both panels `SDA/MOSI/DIN`
- Left `ST7789` (`76x284`)
  `GPIO16` -> `CS`
  `GPIO17` -> `DC/RS/A0`
  `GPIO21` -> `RST/RES`
  `GPIO22` -> `BL/LED`
- Right `ST7789/ST7899` (`76x284`, default dedicated software SPI)
  `GPIO11` -> `SCL/CLK/SCK`
  `GPIO5` -> `SDA/MOSI/DIN`
  `GPIO9` -> `CS`
  `GPIO8` -> `DC/RS/A0`
  `GPIO4` -> `RST/RES`
  `BL/LED` can be tied to `3V3`, or configured separately in `menuconfig` / `sdkconfig.defaults.local`
- Three light outputs
  `GPIO13` -> Light 1
  `GPIO14` -> Light 2
  `GPIO33` -> Light 3
- Power
  Both panel `VCC` pins -> `3V3`
  Both panel `GND` pins -> `GND`

If you do not need software backlight control, wire `BL/LED` directly to `3V3`
and set the matching `BL` option to `-1` in `menuconfig` or `sdkconfig.defaults.local`.

Mirror these GPIO values in `menuconfig -> Example Configuration -> Dashboard Device Configuration`.

Build example:

```sh
HOMEKIT_DEVICE_TYPE=dashboard idf.py reconfigure build
```
