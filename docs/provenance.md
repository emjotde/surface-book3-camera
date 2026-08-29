# Source provenance and licensing

## Kernel modules

The IPU4P modules started from the mainline Linux Intel IPU6 driver.
They use IPU4 hardware definitions from Intel's `linux-intel-lts` `4.19/base` branch.
The port targets the Ice Lake IPU4P PCI device `8086:8a19`.

The INT346F module started from the mainline Linux INT3472 discrete camera driver.
The OV01A10 module started from the mainline Linux OV01A10 sensor driver.

Each source file keeps its original SPDX license identifier.
The combined kernel module work is distributed under GPL-2.0-only.

## Hardware values

The IPU4P PHY and lane configuration values were determined from Dell's Windows camera driver.
Only the required numeric hardware settings are present here.
This repository does not contain the Windows driver.

## Firmware

The required `$CPD` firmware comes from Dell package `4PY3P`.
The package maps `cpd_component_signed.bin` to Intel device `8086:8a19`.
The known firmware SHA-256 value is:

```text
ee534f37f979dfc20e1cb4681bae47c9ec57639f3a2d032b79d77b3097b74d97
```

The firmware is proprietary. Users must download and extract it themselves.
Do not add the firmware or the Dell Windows package to this repository.
