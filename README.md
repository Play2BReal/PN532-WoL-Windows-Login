# ESP32 Windows Login NFC Reader

A modular ESP32 based NFC reader that automatically logs into Windows by reading NTAG cards, sending Wake-on-LAN packets, and typing passwords via USB HID.

## Features

- **NFC Authentication**: Reads NTAG cards and validates against configured URL
- **WiFi Connectivity**: Connects to your WiFi network automatically
- **Wake-on-LAN**: Sends magic packets to wake sleeping PCs
- **USB HID Keyboard**: Types Windows password automatically
- **LED Diagnostics**: Visual feedback for all operations
- **Modular Design**: Supports various ESP32 variants (C3, S3, etc.)

## Hardware Setup

### ESP32 Wiring
```
ESP32       PN532 NFC Reader
--------    ----------------
3.3V    →   VCC
GND     →   GND
GPIO4   →   SDA (I2C)
GPIO5   →   SCL (I2C)
```

### LED Configuration
- **NeoPixel**: Connect to GPIO2 (WS2812B compatible)
- **Normal LED**: Connect to GPIO2 (built-in LED on most dev boards)

## Software Setup

1. **Install ESP-IDF** (v5.5+)
2. **Set target**: `idf.py set-target esp32` (or your specific variant like `esp32s3`, `esp32c3`, etc.)
3. **Configure**: Edit `main/main.c` with your settings:
   ```c
   #define WIFI_SSID "YourWiFiName"
   #define WIFI_PASSWORD "YourWiFiPassword"
   #define PC_MAC_ADDRESS "AA:BB:CC:DD:EE:FF"
   #define PC_IP_ADDRESS "192.168.1.100"
   #define WINDOWS_PASSWORD "YourWindowsPassword"
   #define AUTH_URL "YourAuthURL.com"
   ```
4. **Build**: `idf.py build`
5. **Flash**: `idf.py flash monitor`

## Configuration

### WiFi Settings
- Configure your WiFi SSID and password
- Supports WPA2 authentication
- Automatic reconnection with keep-alive

### PC Settings
- Set your PC's MAC address for Wake-on-LAN
- Configure PC's IP address for connectivity checks
- Enable Wake-on-LAN in BIOS and Windows

### NFC Authentication
- Set the authentication URL that must be present on NTAG cards
- Case-insensitive matching
- Supports any NTAG213/215/216 cards

## LED Diagnostics

### NeoPixel (RGB)
- **Blue blink**: Boot indication
- **Rainbow cycle**: PC connection successful
- **Green 3 blinks**: Authentication success
- **Red 3 blinks**: Wrong card/NDEF failure
- **Red 1 blink**: Read failure (misread/cut-off)

### Normal LED (Single Color)
- **5 blinks**: Boot indication
- **2 blinks**: PC connection successful
- **3 blinks**: Authentication success
- **1 blink**: Any failure

## How It Works

1. **Boot**: Device initializes WiFi, HID keyboard, and NFC reader
2. **NFC Scan**: Waits for NTAG card to be presented
3. **Authentication**: Validates card against configured URL
4. **WiFi Check**: Ensures WiFi connection is active
5. **PC Detection**: Checks if PC is already online
6. **Wake-on-LAN**: Sends magic packets if PC is offline
7. **Login**: Types Windows password via USB HID
8. **Feedback**: Provides LED indication for all operations

## Troubleshooting

### WiFi Issues
- Check SSID and password are correct
- Verify router supports WPA2
- Check WiFi signal strength

### Wake-on-LAN Issues
- Enable WoL in BIOS
- Configure Windows power settings
- Check MAC address is correct
- Verify network allows WoL packets

### HID Keyboard Issues
- Use correct USB port (USB, not COM)
- Check Windows recognizes device as keyboard
- Verify password is correct

### NFC Issues
- Ensure NTAG card contains correct URL
- Check card is properly formatted
- Verify I2C connections

## Project Structure

```
├── main/
│   ├── main.c              # Main application logic
│   ├── CMakeLists.txt      # Build configuration
│   └── idf_component.yml   # Component dependencies
├── components/
│   ├── wifi_manager/       # WiFi connection management
│   ├── wol_client/         # Wake-on-LAN functionality
│   └── hid_keyboard/       # USB HID keyboard emulation
└── README.md               # This file
```

## Dependencies

- ESP-IDF v5.5+
- esp_tinyusb (for USB HID)
- led_strip (for NeoPixel support)
- nvs_flash (for configuration storage)

## License

This project is open source. Use responsibly and ensure you have permission to access any systems you're automating.

## Contributing

Feel free to submit issues and enhancement requests!