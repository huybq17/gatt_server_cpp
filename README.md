# GATT Server (C++ / BlueZ / D-Bus)

Standalone BLE **GATT Server** for Raspberry Pi 4 using **BlueZ** over **system D-Bus**, written in **C++** with **sdbus-c++**.

It exposes:
- LE Advertisement: `LocalName = PiGattServer`
- 1 GATT Service UUID
- 1 GATT Characteristic UUID
  - Flags: `read`, `write`, `notify`

## Requirements

- BlueZ running (`bluetoothd`)
- system D-Bus
- `sdbus-c++` development package
- CMake >= 3.10, C++17 compiler

Quick check:

```bash
systemctl status bluetooth
bluetoothctl show
```

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

Binary:

`./build/PiGattServer`

## Run

Most distros require elevated privileges (Polkit policy) for registering GATT/advertisements via BlueZ on the **system bus**.
If you run without `sudo`, BlueZ registration may appear to “hang” and eventually time out.

```bash
sudo ./build/PiGattServer
python3 web/app.py
```

Stop with `Ctrl+C`.

## Test

### Option A: Phone app (recommended)

Use nRF Connect (Android/iOS) or LightBlue:
1. Scan and find `PiGattServer`
2. Connect
3. Discover services → open the custom service
4. Read characteristic value
5. Write any bytes
6. Enable notifications

### Option B: bluetoothctl (basic)

`bluetoothctl` can discover and connect, but it’s limited for GATT read/write/notify.

## Notes

- This implementation follows BlueZ "GATT server via D-Bus" pattern:
  - Exports an ObjectManager at `/com/example/gatt/app`
  - Registers the application via `org.bluez.GattManager1.RegisterApplication`
  - Exports an LE advertisement and registers via `org.bluez.LEAdvertisingManager1.RegisterAdvertisement`
