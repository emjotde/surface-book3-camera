# Architecture

The camera stack has five parts.

1. `int346f.ko` controls the INT346F power rails, clock, reset line, and privacy LED.
2. `ov01a10-i346f.ko` controls the OV01A10 image sensor.
3. `intel-ipu4p.ko` loads and authenticates the Intel IPU4P firmware.
4. `intel-ipu4p-isys.ko` receives CSI-2 data and exposes packed RAW10 frames.
5. `ipu4-softisp` converts RAW10 Bayer frames into YUYV frames for `v4l2loopback`.

## Data path

```text
OV01A10 sensor
  -> MIPI CSI-2
  -> Intel IPU4P ISYS
  -> 1280x800 packed RAW10
  -> ipu4-softisp
  -> 640x400 YUYV
  -> v4l2loopback /dev/video57
  -> browser or video-call application
```

## Hardware-specific settings

The Dell firmware describes camera port 6. Firmware source 6 maps to receiver `s1p0`.
The driver uses IPU4P receiver index 1 and MMIO base `0x6c000`.

The receiver needs the Windows PHY table values for building block 8.
It also needs port configuration `0x2e95` and a general-purpose reset pulse.

The sensor uses continuous MIPI clock mode with register `0x4800 = 0x04`.
The IPU6-derived iwake and LTR writes remain disabled because they stall IPU4P DMA.

## Recovery

Some failed CSI sessions return a valid prefix followed by `0xff` padding.
The software ISP rejects these frames and restarts the capture stream.
Firmware STOP or CLOSE failures cause an ISYS power cycle before the next stream.
