# WS2812B LED Strip — Seeed XIAO nRF52840 BLE

Drive a WS2812B (NeoPixel) strip from a Seeed XIAO nRF52840 BLE using the
nRF Connect SDK (Zephyr) `worldsemi,ws2812-i2s` driver.

## Wiring

| XIAO BLE Pin | WS2812B Strip |
|--------------|---------------|
| D10 (P1.15)  | DIN (Data In) |
| 5 V          | VCC           |
| GND          | GND           |

> **Important:** This project uses the nRF52840 I2S peripheral for accurate
> WS2812B timing. Only connect the strip data line to `D10`. The I2S clock
> pins `D8` and `D9` will toggle during transfers but must be left
> unconnected.

> **Logic level:** WS2812B LEDs expect a 5 V data signal. The nRF52840 outputs
> 3.3 V, which often works for short wiring runs. For best reliability, add a
> 3.3 V → 5 V level shifter such as `74AHCT125`.

> **Power:** Each WS2812B LED can draw up to 60 mA at full white. For more than
> a few LEDs, power the strip from an external 5 V supply and share ground with
> the XIAO.

### Pin Map

```text
D0=P0.02  D1=P0.03  D2=P0.28  D3=P0.29  D4=P0.04  D5=P0.05
D6=P1.11  D7=P1.12  D8=P1.13  D9=P1.14  D10=P1.15
```

### Changing the data pin

Edit `boards/xiao_ble_nrf52840.overlay` and update the `I2S_SDOUT` entry in
the `i2s0_default` pin group. For example, to move data output to `D6`:

```dts
psels = <NRF_PSEL(I2S_SCK_M, 1, 13)>,
	<NRF_PSEL(I2S_LRCK_M, 1, 14)>,
	<NRF_PSEL(I2S_SDOUT, 1, 11)>,
	<NRF_PSEL(I2S_SDIN, 0, 28)>;
```

### Changing the number of LEDs

Set `chain-length` in `boards/xiao_ble_nrf52840.overlay`:

```dts
chain-length = <30>;
```

## Prerequisites

### 1. Install nRF Connect SDK

Follow the [Nordic installation guide](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation.html)
or use the `nRF Connect for VS Code` extension.

Quick CLI path on macOS:

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

# Option B: ARM GNU Toolchain
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
```

## Build

```bash
export ZEPHYR_BASE=~/ncs/zephyr
west build --pristine -b xiao_ble/nrf52840 /Users/emil/Repos/nRF/nRF52840
```

## Flash

This board uses a UF2 bootloader.

1. Double-tap `RESET` so the board mounts as `XIAO-SENSE`.
2. Copy or drag `build/nRF52840/zephyr/zephyr.uf2` to the mounted drive.

## Project Structure

```text
├── CMakeLists.txt                      # Zephyr build system entry point
├── prj.conf                            # Kconfig: Bluetooth + LED strip
├── boards/
│   └── xiao_ble_nrf52840.overlay       # Device tree: I2S pin + WS2812B config
├── src/
│   └── main.c                          # BLE-controlled arrow animations
└── README.md
```
