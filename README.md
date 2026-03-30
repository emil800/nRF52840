# WS2812B LED Strip — Seeed XIAO nRF52840 BLE

Drive a WS2812B (NeoPixel) LED strip from a Seeed XIAO nRF52840 BLE using the
nRF Connect SDK (Zephyr) SPI-based LED strip driver.

## Wiring

| XIAO BLE Pin | WS2812B Strip |
|--------------|---------------|
| D0 (P0.02)   | DIN (Data In) |
| 5 V (VUSB)   | VCC           |
| GND          | GND           |

> **Note:** WS2812B LEDs expect a 5 V data signal. The nRF52840 outputs 3.3 V,
> which usually works for short strips. For long runs or reliability, add a
> 3.3 V → 5 V level shifter (e.g. 74AHCT125) on the data line.

> **Power:** Each WS2812B LED can draw up to 60 mA at full white. For more than
> ~8 LEDs, power the strip from an external 5 V supply (share GND with the
> XIAO).

### Changing the data pin

Edit `boards/xiao_ble_nrf52840.overlay` and update the `psels` line in both
`spi3_default` and `spi3_sleep`. The pin mapping is:

```
D0=P0.02  D1=P0.03  D2=P0.28  D3=P0.29  D4=P0.04  D5=P0.05
D6=P1.11  D7=P1.12  D8=P1.13  D9=P1.14  D10=P1.15
```

For example, to use D6 (P1.11):
```dts
psels = <NRF_PSEL(SPIM_MOSI, 1, 11)>;
```

### Changing the number of LEDs

Set `chain-length` in the overlay to match your strip:
```dts
chain-length = <30>;   /* for a 30-LED strip */
```

## Prerequisites

### 1. Install nRF Connect SDK

Follow the [Nordic installation guide](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation.html)
or use `nRF Connect for VS Code` extension.

The quickest CLI path (macOS):

```bash
pip3 install west
west init -m https://github.com/nrfconnect/sdk-nrf --mr main ~/ncs
cd ~/ncs
west update
west zephyr-export
pip3 install -r zephyr/scripts/requirements.txt
pip3 install -r nrf/scripts/requirements.txt
```

### 2. Install the ARM toolchain

```bash
# Option A: Zephyr SDK (recommended)
west sdk install

# Option B: ARM GNU Toolchain — download from
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
```

## Build & Flash

```bash
# Set ZEPHYR_BASE if not already in your environment
export ZEPHYR_BASE=~/ncs/zephyr

# Build
west build -b xiao_ble/nrf52840 /Users/emil/Repos/nRF/nRF52840

# Flash via USB (the XIAO BLE has a UF2 bootloader)
west flash
```

### UF2 Bootloader (alternative flash method)

1. Double-tap the RESET button on the XIAO — it mounts as a USB drive.
2. Copy the UF2 file to the drive:
   ```bash
   cp build/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE/
   ```
   (The volume name may vary.)

To generate the UF2 file, make sure `CONFIG_BUILD_OUTPUT_UF2=y` is in
`prj.conf` (already implied by the board defaults for xiao_ble).

## Project Structure

```
├── CMakeLists.txt                      # Zephyr build system entry point
├── prj.conf                            # Kconfig — enables SPI + LED strip
├── boards/
│   └── xiao_ble_nrf52840.overlay       # Device tree: SPI pin + WS2812B config
├── src/
│   └── main.c                          # Rainbow-cycling demo
└── README.md
```
