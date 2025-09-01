import asyncio
from bleak import BleakClient, BleakScanner

DEVICE_NAME = "nimble-ble-ota"
UUID_LEN  = "0000fff1-0000-1000-8000-00805f9b34fb"
UUID_DATA = "0000fff2-0000-1000-8000-00805f9b34fb"

FIRMWARE_FILE = r"C:\Users\abhishek\projects_iot\build\A_CODE.bin" #Change the directory that points to the binary file of the Project
CHUNK_SIZE = 240  # safe for MTU=247

async def main():
    print("Scanning for ESP32...")
    device = await BleakScanner.find_device_by_filter(lambda d, ad: d.name == DEVICE_NAME)
    if not device:
        print("Device not found.")
        return

    async with BleakClient(device) as client:
        print("Connected to", DEVICE_NAME)

        with open(FIRMWARE_FILE, "rb") as f:
            firmware = f.read()

        size = len(firmware)
        print(f"Firmware size = {size} bytes")

        # Send total size first
        await client.write_gatt_char(UUID_LEN, size.to_bytes(4, "little"), response=True)

        total_chunks = (size + CHUNK_SIZE - 1) // CHUNK_SIZE
        for i in range(0, size, CHUNK_SIZE):
            chunk = firmware[i:i+CHUNK_SIZE]
            await client.write_gatt_char(UUID_DATA, chunk, response=False)  # 🚀 no ACK

            percent = (i + len(chunk)) * 100 // size
            if percent % 5 == 0:  # print every 5%
                print(f"Progress: {percent}%")

        print("Upload complete ✅ ESP32 should reboot")

asyncio.run(main())

