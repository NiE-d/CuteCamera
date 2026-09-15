📷 CuteCamera — Hardware Connections
🧩 Components
Seeed Studio XIAO ESP32-S3 Sense
OV3660 Camera Module
1.28" GC9A01A 240×240 round TFT display
microSD card
3 × 6×6×5 mm tactile push buttons
LiPo battery / USB power source
Jumper wires
🔌 TFT Display — GC9A01A

The display communicates with the XIAO ESP32-S3 using SPI.

GC9A01A Pin	XIAO ESP32-S3 Pin	Purpose
VCC	3V3	Power
GND	GND	Ground
SDA / MOSI	D10	SPI Data
SCL / SCK	D8	SPI Clock
CS	D1	Chip Select
DC	D2	Data/Command
RST	D3	Reset
TFT configuration used in code
#define TFT_CS   D1
#define TFT_DC   D2
#define TFT_RST  D3

The display is configured with:

tft.setRotation(3);

The camera displays frames at:

240 × 240 pixels
🔘 Buttons

All three buttons use the XIAO's internal pull-up resistors.

Wiring

Each button is connected between its GPIO pin and GND.

Button 1:
D4 ───── Push Button ───── GND

Button 2:
D7 ───── Push Button ───── GND

Button 3:
D6 ───── Push Button ───── GND

No external resistor is required because the code uses:

INPUT_PULLUP
🎛️ Button Functions
Button	XIAO Pin	Function
Shutter	D4	Single click → Take photo
Gallery / Previous	D7	Double click → Open latest photo
Gallery / Previous	D7	Single click → Previous photo
Next / Back	D6	Single click → Next photo
Next / Back	D6	Double click → Return to live preview
Button logic
LIVE PREVIEW

D4 single click
      ↓
Take photo
      ↓
Shutter animation
      ↓
Save JPEG to SD
LIVE PREVIEW

D7 double click
      ↓
Open latest saved photo
      ↓
PHOTO VIEW
PHOTO VIEW

D7 single click → Previous photo

D6 single click → Next photo

D6 double click → Live preview
💾 microSD Card

The microSD card is connected through the XIAO ESP32-S3 Sense expansion board.

The SD chip-select pin used by the code is:

#define SD_CS 21

Therefore:

SD Function	GPIO
SD CS	GPIO 21

The SD card is initialized using:

SD.begin(SD_CS);
Storage format

Photos are saved as JPEG files:

/IMG_1.jpg
/IMG_2.jpg
/IMG_3.jpg
...

The camera automatically finds the next available filename when saving a photo.

📸 OV3660 Camera

The OV3660 camera is part of the XIAO ESP32-S3 Sense camera hardware, so it does not require separate jumper-wire connections when using the Sense camera board.

The firmware configures the camera using these GPIOs:

Camera Signal	GPIO
XCLK	GPIO 10
SIOD / SDA	GPIO 40
SIOC / SCL	GPIO 39
Y2	GPIO 15
Y3	GPIO 17
Y4	GPIO 18
Y5	GPIO 16
Y6	GPIO 14
Y7	GPIO 12
Y8	GPIO 11
Y9	GPIO 48
VSYNC	GPIO 38
HREF	GPIO 47
PCLK	GPIO 13

The firmware uses:

FRAMESIZE_240X240

and:

PIXFORMAT_RGB565

for the live preview.

🔋 Power

The camera can be powered without a computer.

USB power

You can power the XIAO through USB from:

Laptop
Power bank
Phone
USB adapter
LiPo battery

For standalone operation, a suitable LiPo battery can be connected to the XIAO's battery input according to the XIAO ESP32-S3 Sense board's battery connector/pads.

LiPo
 ├── Positive (+) → BAT
 └── Negative (−) → GND

⚠️ Do not connect a raw LiPo directly to the 3V3 pin.

🧠 Complete Connection Overview
                    ┌─────────────────────────┐
                    │   XIAO ESP32-S3 Sense   │
                    │                         │
                    │                         │
   GC9A01A VCC ────►│ 3V3                     │
   GC9A01A GND ────►│ GND                     │
   GC9A01A MOSI ───►│ D10                     │
   GC9A01A SCK ────►│ D8                      │
   GC9A01A CS ─────►│ D1                      │
   GC9A01A DC ─────►│ D2                      │
   GC9A01A RST ────►│ D3                      │
                    │                         │
   Shutter ────────►│ D4 ── Button ── GND    │
   Gallery ────────►│ D7 ── Button ── GND    │
   Next/Back ──────►│ D6 ── Button ── GND    │
                    │                         │
   SD CS ──────────►│ GPIO21                  │
                    │                         │
                    │   OV3660 CAMERA         │
                    │   (Sense board)         │
                    └─────────────────────────┘
📌 Pin Summary
Component	Pin	XIAO ESP32-S3
GC9A01A	VCC	3V3
	GND	GND
	SDA / MOSI	D10
	SCL / SCK	D8
	CS	D1
	DC	D2
	RST	D3
Shutter Button	Signal	D4
	Other side	GND
Gallery Button	Signal	D7
	Other side	GND
Next/Back Button	Signal	D6
	Other side	GND
microSD	CS	GPIO21
OV3660	Camera interface	Built into Sense hardware
LiPo	+	BAT
	−	GND
⚙️ Software Configuration

The project is built using Arduino IDE.

Main libraries used:

#include "esp_camera.h"
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "Adafruit_GFX.h"
#include "Adafruit_GC9A01A.h"
#include <JPEGDEC.h>
Camera settings
Resolution: 240 × 240
Pixel format: RGB565
Frame buffers: 2
Frame location: PSRAM
Grab mode: CAMERA_GRAB_LATEST
Debug mode

The standalone version uses:

#define DEBUG_SERIAL 0

So the camera doesn't depend on an open Arduino Serial Monitor during normal standalone operation.

To enable debugging temporarily:

#define DEBUG_SERIAL 1
📁 Project flow
OV3660 Camera
      ↓
RGB565 Frame
      ↓
GC9A01A Live Preview
      ↓
D4 Shutter
      ↓
Shutter Animation
      ↓
RGB565 → JPEG
      ↓
microSD
      ↓
/IMG_N.jpg
      ↓
D7 Double Click
      ↓
Gallery
      ↓
D7 ← Previous | Next → D6

## 📄 License

This project is open source under the MIT License.
