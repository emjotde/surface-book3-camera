table.insert(v4l2_monitor.rules, {
  matches = {
    {
      { "device.name", "matches", "v4l2_device.pci-0000_00_05.0*" },
    },
  },
  apply_properties = {
    ["device.disabled"] = true,
  },
})
