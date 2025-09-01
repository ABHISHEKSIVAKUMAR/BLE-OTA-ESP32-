# ESP32 BLE OTA (NimBLE + Python Uploader)

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/) 
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](./LICENSE)

This project demonstrates fast **Over-The-Air (OTA)** firmware updates for ESP32 using **Bluetooth Low Energy (BLE)** with the **NimBLE host stack**.  
A cross-platform Python uploader script (based on Bleak) is provided to send the firmware binary wirelessly.  

---

## ✨ Features
- Uses NimBLE (lightweight BLE stack) in ESP-IDF  
- Custom OTA service (UUIDs:  
  - **0xFFF1** – Write firmware size (4 bytes LE)  
  - **0xFFF2** – Write firmware data (stream, Write Without Response)  
- Optimized for speed: MTU up to 512, Write Without Response, progress logging every 5%  
- Works with any central that supports GATT writes (tested with Python + Bleak)  

---

## 🔧 1. Configure project using `menuconfig`
```bash
idf.py menuconfig
```

- Component config → Bluetooth → Bluetooth → Host → **NimBLE - BLE only**  
- Component config → Bluetooth → NimBLE Options → **Preferred MTU size in octets = 517 (max)**  
- For ESP32S3 / ESP32C6 / ESP32H2 → set **NimBLE Host task stack size = 8192**  
- Serial flasher config → Flash size = **4 MB**  

---

## 🏗️ 2. Build and flash
```bash
idf.py build flash monitor
```
Device will advertise as **"nimble-ble-ota"**.

---

## 📦 3. Install Python requirements
```bash
pip install bleak
```

---

## 🐍 4. Run the uploader
Uploader script is inside `tools/ota_upload.py`  
```bash
python tools/ota_upload.py
```

---

## ⚙️ 5. Configure uploader
- Update `FIRMWARE_FILE` to your `.bin` file  
- Default `CHUNK_SIZE = 240` (safe for MTU=247)  

---

## 🔄 OTA Workflow
1. ESP32 boots into current firmware  
2. Python uploader connects over BLE  
3. Firmware size written to **0xFFF1**  
4. Firmware streamed in chunks to **0xFFF2** (Write Without Response)  
5. ESP32 writes into OTA partition  
6. On completion → validates, sets partition bootable, reboots into new firmware  

---

## ⚡ Performance

| Config           | Speed (approx.)     | Example 3 MB update |
|------------------|---------------------|---------------------|
| Default (20 B)   | ~5 KB/s             | ~10 min             |
| MTU=247 (240 B)  | ~50–80 KB/s         | ~30–40 sec          |
| MTU=512 (508 B)  | ~100 KB/s+          | ~15–20 sec          |

> ⚠️ Note: Keep two OTA partitions in partition table (ota_0, ota_1).  
> Never upload private keys (`rsa_key/`) to GitHub.  

---

## 📚 References
- [ESP-IDF OTA API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)  
- [NimBLE in ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/nimble/index.html)  
- [Bleak Python BLE](https://github.com/hbldh/bleak)  

---

## 📜 License
Licensed under the **Apache License 2.0**, same as ESP-IDF.  
