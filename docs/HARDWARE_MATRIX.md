# Hardware qualification matrix

## Development system

- Stable labels: `development-ryzen-9800x3d-radeon` and
  `development-ryzen-9800x3d-nvidia`
- CPU: AMD Ryzen 7 9800X3D
- GPUs: NVIDIA GeForce RTX 3070 Ti and AMD integrated Radeon graphics
- Network: Realtek RTL8126 Ethernet and Qualcomm WCN785x Wi-Fi 7
- Storage: Samsung 990 PRO NVMe plus SATA devices
- Purpose: demanding dual-GPU, NVIDIA, Wi-Fi 7, USB4, and desktop validation

## Required notebook reference class

The final device receives the stable label `reference-notebook` when its exact
SKU is selected. Until then, reports using that label are not accepted as M1 or
M4 qualification evidence.

Select one purchasable model before M2 freeze with:

- AMD or Intel integrated graphics supported by upstream Mesa
- 16 GiB or more RAM, NVMe, Wi-Fi, Bluetooth, audio, webcam, and suspend
- 120 Hz-capable display preferred; HiDPI required
- Firmware updates available through LVFS preferred
- No out-of-tree driver required for core operation

Record the exact SKU, firmware, kernel, Mesa version, display topology, and test
results here. Hardware is "supported" only after install, update, rollback,
suspend/resume, audio, Wi-Fi, Bluetooth, display scaling, and performance gates
pass on the published image.
