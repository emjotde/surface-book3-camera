# Design

```text
OV5693 / OV8865 (stock sensor drivers)
  -> IPU4P controller + ISYS modules
  -> packed RAW10
  -> software ISP: upright YUYV, exposure and white balance
  -> surfaceisp GStreamer source
  -> Ubuntu v4l2-relayd
  -> v4l2loopback /dev/video51 (front), /dev/video50 (rear)
  -> PipeWire -> application
```

The relay keeps virtual cameras visible with idle black frames. Loopback
stream-usage events start the ISP child; the last stream close stops and reaps
it. Noble's relay/module event ABI is `0x08000000`. No replacement loopback
module is needed.

The discovery helper refreshes each virtual device after bridge start/stop.
WirePlumber hides raw Bayer nodes from desktop applications.

The Surface front receiver reset must run **before firmware stream setup**.
Its PHY path avoids Dell clock/reset writes, leaves BB8 untouched and configures
BB10. Controller reprobe reuses surviving firmware nodes to avoid `-EEXIST`.

Raw formats are BGGR RAW10: front 2592x1944, stride 3264; rear 3264x2448,
stride 4096. Resolve sensor subdevices by media entity name, not fixed numbers.
Test-generator pixels and all-`0xff` failed buffers are not real camera capture.
