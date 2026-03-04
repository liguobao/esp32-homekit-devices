# Dashboard Device

This is a three-light HomeKit panel device type.

Runtime behavior:

- Exposes 3 independent HomeKit light services
- Uses the left `ST7789` strip for time and weather
- Uses the right `NV3007` strip for IP and light state

Recommended wiring (classic `ESP32`):

- Shared SPI
  `GPIO18` -> both panels `SCL/CLK/SCK`
  `GPIO23` -> both panels `SDA/MOSI/DIN`
- Left `ST7789` (`76x284`)
  `GPIO16` -> `CS`
  `GPIO17` -> `DC/RS/A0`
  `GPIO21` -> `RST/RES`
  `GPIO22` -> `BL/LED`
- Right `NV3007` (`142x428`)
  `GPIO25` -> `CS`
  `GPIO26` -> `DC/RS/A0`
  `GPIO27` -> `RST/RES`
  `GPIO32` -> `BL/LED`
- Three light outputs
  `GPIO13` -> Light 1
  `GPIO14` -> Light 2
  `GPIO33` -> Light 3
- Power
  Both panel `VCC` pins -> `3V3`
  Both panel `GND` pins -> `GND`

If you do not need software backlight control, wire `BL/LED` directly to `3V3`
and set the matching `BL` option to `-1` in `menuconfig`.

Mirror these GPIO values in `menuconfig -> Example Configuration -> Dashboard Device Configuration`.

Build example:

```sh
HOMEKIT_DEVICE_TYPE=dashboard idf.py reconfigure build
```
