ESP32 BLE OTA (NimBLE + Python Uploader)
This project demonstrates fast Over-The-Air (OTA) firmware updates for ESP32 using Bluetooth Low Energy (BLE) with the NimBLE host stack. A cross-platform Python uploader script (based on Bleak) is provided to send the firmware binary wirelessly.
✨ Features
•	Uses NimBLE (lightweight BLE stack) in ESP-IDF
•	OTA service (UUID 0xFFF0) with:
•	 - 0xFFF1 → Write firmware size (4 bytes LE)
•	 - 0xFFF2 → Write firmware data (stream, Write Without Response)
•	Optimized for speed: MTU up to 512, Write Without Response, progress logging every 5%
•	Works with any central that supports GATT writes (tested with Python + Bleak)
📂 Repo Layout
firmware/  → ESP-IDF NimBLE OTA firmware
tools/     → Python uploader script
⚙️ Firmware (ESP-IDF Project)
1. Configure project using menuconfig:
- Component config → Bluetooth → Bluetooth → Host → NimBLE - BLE only
- Component config → Bluetooth → NimBLE Options → Preferred MTU size in octets → set 517 (max)
- For ESP32S3/ESP32C6/ESP32H2, set NimBLE Host task stack size → 8192
- Serial flasher config → Flash size → 4 MB
2. Build and flash:
idf.py build flash monitor
Device will advertise as "nimble-ble-ota".
🐍 Python Uploader (Bleak)
Uploader script is in tools/ota_upload.py.
1. Install requirements:
pip install bleak
2. Run uploader:
python tools/ota_upload.py
3. Config: Update FIRMWARE_FILE to your .bin file. Default CHUNK_SIZE=240.
🚀 OTA Workflow
1.	ESP32 boots into current firmware
2.	Python uploader connects over BLE
3.	Firmware size written to 0xFFF1
4.	Firmware streamed in chunks to 0xFFF2 (Write Without Response)
5.	ESP32 writes into OTA partition
6.	On completion → validates, sets partition bootable, reboots into new firmware
📊 Performance
- Default BLE (20-byte packets): ~5 KB/s (3 MB = ~10 min)
- With MTU=247 + Write Without Response: ~50–80 KB/s (3 MB = ~30–40 sec)
- With MTU=512: even faster
🔒 Notes
•	Keep two OTA partitions in partition table (ota_0, ota_1).
•	Never upload private keys (rsa_key/) to GitHub.
•	New firmware must be built with the same partition table.
📜 License
Apache 2.0 (same as ESP-IDF).
📌 References
•	ESP-IDF OTA API: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html
•	NimBLE in ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/nimble/index.html
•	Bleak Python BLE: https://github.com/hbldh/bleak

