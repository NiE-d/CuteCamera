# 📷 CuteCamera

A compact DIY digital camera built using the **Seeed Studio XIAO ESP32-S3 Sense**, **OV3660 camera**, **1.28-inch round GC9A01A TFT display**, physical buttons, and a **microSD card**.

The project is designed to work as a small standalone camera with live preview, photo capture, photo storage, and a simple gallery system.

---

## ✨ Features

- 📸 Live camera preview
- 🔘 Physical shutter button
- ⚡ Shutter flash and scan animation
- 💾 Saves captured photos as JPEG files
- 🖼️ Built-in photo viewer/gallery
- ⬅️ Previous photo
- ➡️ Next photo
- 🔄 Return to live preview
- 💽 microSD card storage
- 🔋 Standalone battery operation
- 🖥️ 240 × 240 round TFT display
- 🎨 Minimal camera interface

---

# 🧩 Hardware Components

| Component | Quantity |
|---|---:|
| Seeed Studio XIAO ESP32-S3 Sense | 1 |
| OV3660 Camera | 1 |
| 1.28" GC9A01A Round TFT Display | 1 |
| 6 × 6 × 5 mm Tactile Push Buttons | 3 |
| microSD Card | 1 |
| LiPo Battery / Suitable Power Source | 1 |
| Connecting Wires | As required |

---

# 🔌 TFT Display Connections

The GC9A01A TFT is connected using SPI.

| TFT Pin | XIAO ESP32-S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA / MOSI | D10 |
| SCL / SCK | D8 |
| CS | D1 |
| DC | D2 |
| RST | D3 |

### TFT Configuration

- Display: GC9A01A
- Resolution: 240 × 240 pixels
- Interface: SPI
- Display rotation used in firmware: Rotation 3

---

# 🔘 Button Connections

All buttons use the ESP32's internal pull-up resistors.

No external resistor is required.

## Shutter Button

| Button | Connection |
|---|---|
| D4 | Button |
| Other side | GND |

### Function

**D4 — Single Click**

- Captures the current frame
- Displays the shutter animation
- Converts the frame to JPEG
- Saves the image to the microSD card
- Returns to live preview

---

## Gallery / Previous Button

| Button | Connection |
|---|---|
| D7 | Button |
| Other side | GND |

### Functions

**D7 — Double Click**

- Opens the latest saved photograph
- Enters photo viewer/gallery mode

**D7 — Single Click while viewing a photo**

- Shows the previous saved photograph

---

## Next / Return Button

| Button | Connection |
|---|---|
| D6 | Button |
| Other side | GND |

### Functions

**D6 — Single Click while viewing a photo**

- Shows the next saved photograph

**D6 — Double Click while viewing a photo**

- Closes the photo viewer
- Returns to live camera preview

---

# 💾 microSD Card

The microSD card is used to store captured photographs.

The SD card chip-select pin used by the firmware is:

| SD Function | GPIO |
|---|---:|
| CS | GPIO 21 |

The SD card is connected through the **XIAO ESP32-S3 Sense expansion board**.

Captured images are stored with filenames such as:

- `/IMG_1.jpg`
- `/IMG_2.jpg`
- `/IMG_3.jpg`

The firmware searches for the next available image number before saving a new photograph.

---

# 📷 OV3660 Camera

The project uses the **OV3660 camera** available with the XIAO ESP32-S3 Sense.

The camera is configured for:

- Resolution: 240 × 240
- Pixel format: RGB565
- Frame buffer location: PSRAM
- Frame buffers: 2
- Frame grab mode: `CAMERA_GRAB_LATEST`

## Camera GPIO Configuration

| Camera Signal | GPIO |
|---|---:|
| XCLK | 10 |
| SIOD | 40 |
| SIOC | 39 |
| Y9 | 48 |
| Y8 | 11 |
| Y7 | 12 |
| Y6 | 14 |
| Y5 | 16 |
| Y4 | 18 |
| Y3 | 17 |
| Y2 | 15 |
| VSYNC | 38 |
| HREF | 47 |
| PCLK | 13 |

These GPIO definitions are used by the camera initialization code.

---

# 🔋 Power

The camera is intended to operate as a standalone device.

A suitable LiPo battery or other appropriate power source can be used with the XIAO ESP32-S3 Sense.

⚠️ **Important:** Do not connect a raw LiPo battery directly to the 3V3 pin. Use the board's appropriate battery/power input according to the board specifications.

For development, the board can also be powered through USB.

---

# 🔗 Complete Wiring Summary

## TFT

- VCC → 3V3
- GND → GND
- SDA/MOSI → D10
- SCL/SCK → D8
- CS → D1
- DC → D2
- RST → D3

## Buttons

- D4 → Shutter button → GND
- D7 → Gallery/Previous button → GND
- D6 → Next/Return button → GND

## SD

- CS → GPIO 21
- microSD → XIAO ESP32-S3 Sense expansion board

## Camera

- OV3660 → XIAO ESP32-S3 Sense camera interface

---

# 🎮 Camera Controls

| Button | Action | Mode |
|---|---|---|
| D4 | Capture photo | Live Preview |
| D7 double click | Open latest photo | Live Preview |
| D7 single click | Previous photo | Photo Viewer |
| D6 single click | Next photo | Photo Viewer |
| D6 double click | Return to live preview | Photo Viewer |

---

# 📸 Photo Capture Workflow

When the shutter button is pressed:

1. The current camera frame is captured.
2. The display performs a white shutter flash.
3. A cyan scanning animation moves across the display.
4. The captured frame is displayed.
5. The RGB565 frame is converted to JPEG.
6. The JPEG image is saved to the microSD card.
7. The image number is updated.
8. The camera returns to live preview.

---

# 🖼️ Gallery Workflow

The camera has two main states:

- **LIVE_PREVIEW**
- **PHOTO_VIEW**

### Live Preview

The camera continuously captures frames from the OV3660 and displays them on the TFT.

### Opening Gallery

Double-click **D7** to open the latest saved photograph.

### Browsing Photos

While viewing a photograph:

- Single-click **D7** → Previous photo
- Single-click **D6** → Next photo

### Returning to Camera

Double-click **D6** to close the photo viewer and return to live preview.

---

# 💻 Software

The project is developed using:

- Arduino IDE
- ESP32 Arduino Core
- C/C++

## Libraries Used

The firmware uses:

- `esp_camera.h`
- `SPI.h`
- `SD.h`
- `FS.h`
- `Adafruit_GFX.h`
- `Adafruit_GC9A01A.h`
- `JPEGDEC.h`

---

# 🧠 Firmware Architecture

The camera firmware is based around two main states:

- `LIVE_PREVIEW`
- `PHOTO_VIEW`

The program continuously checks the physical buttons and changes behaviour according to the current state.

This keeps the camera controls simple without requiring a touchscreen or complicated menu system.

---

# ⚙️ Button Detection

The buttons use:

`INPUT_PULLUP`

Therefore:

- Button released → HIGH
- Button pressed → LOW

The firmware also includes:

- Button debounce
- Single-click detection
- Double-click detection

The double-click timing used by the firmware is approximately:

`1200 ms`

The debounce time is:

`50 ms`

---

# 🖥️ Display

The project uses a:

**1.28-inch GC9A01A round TFT**

Display resolution:

**240 × 240 pixels**

The display is used for:

- Live camera preview
- Captured photo preview
- Shutter animation
- Photo gallery
- Previous/next image navigation

---

# 📦 Image Processing

The camera captures frames in:

`RGB565`

For displaying the live preview, the framebuffer is written directly to the TFT.

When a photograph is captured, the frame is converted to:

`JPEG`

using the ESP32 JPEG conversion functionality and then written to the microSD card.

The JPEG files can later be opened by the camera's built-in photo viewer.

---

# 🗂️ Project Structure

The main firmware is an Arduino `.ino` sketch.

The project can be uploaded directly to the XIAO ESP32-S3 Sense using the Arduino IDE.

---

# 🔧 Standalone Operation

The camera can be operated without the laptop or Serial Monitor.

The firmware includes a compile-time serial debug option.

When debugging is not required, serial output can be disabled so that the camera can operate independently.

The current standalone configuration uses:

`DEBUG_SERIAL = 0`

This allows the camera to run without relying on the Serial Monitor.

---

# 🧰 Hardware Overview

The complete system consists of:

**XIAO ESP32-S3 Sense**
↓  
**OV3660 Camera**

and

**XIAO ESP32-S3**
↓  
**GC9A01A TFT Display**

and

**XIAO ESP32-S3**
↓  
**Physical Buttons**

and

**XIAO ESP32-S3 Sense Expansion Board**
↓  
**microSD Card**

Together, these components form a compact standalone digital camera.

---

# 🚀 Camera Workflow

The overall workflow is:

**Power ON**

↓

**Camera Initialization**

↓

**Live Preview**

↓

**Press D4**

↓

**Capture Frame**

↓

**Shutter Animation**

↓

**JPEG Conversion**

↓

**Save to SD Card**

↓

**Return to Live Preview**

From the live preview:

**D7 Double Click**

↓

**Open Latest Photo**

↓

**Photo Viewer**

From the photo viewer:

**D7 Single Click** → Previous Photo

**D6 Single Click** → Next Photo

**D6 Double Click** → Live Preview

---

# 📌 Quick Pin Reference

| Function | XIAO Pin / GPIO |
|---|---|
| TFT MOSI | D10 |
| TFT SCK | D8 |
| TFT CS | D1 |
| TFT DC | D2 |
| TFT RST | D3 |
| Shutter Button | D4 |
| Next / Return Button | D6 |
| Gallery / Previous Button | D7 |
| SD CS | GPIO 21 |
| Camera XCLK | GPIO 10 |
| Camera SIOD | GPIO 40 |
| Camera SIOC | GPIO 39 |
| Camera VSYNC | GPIO 38 |
| Camera HREF | GPIO 47 |
| Camera PCLK | GPIO 13 |

---

# 🎯 Project Goal

The goal of CuteCamera is to build a small, functional digital camera from scratch using an ESP32-S3-based embedded system.

The project combines:

- Embedded systems
- Camera interfacing
- SPI communication
- TFT display control
- SD card storage
- Image processing
- Physical button control
- 3D-printed mechanical design

The final device is designed to be compact, portable, and completely independent of a computer during normal operation.

---

# 👩‍💻 Development

Built as a personal embedded-systems project using the **Seeed Studio XIAO ESP32-S3 Sense**.

The project combines hardware, firmware, electronics, image processing, and 3D mechanical design into one compact camera system.

---

## ⭐ CuteCamera

**A tiny DIY digital camera built from an ESP32-S3.**

📷 Capture  
💾 Save  
🖼️ Browse  
🔋 Carry  
🚀 Build
