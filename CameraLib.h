/*
  CameraLib.h

  All reusable hardware classes for the CuteCamera firmware, in one
  header, for a two-file Arduino IDE project:

    - Button / Buttons  : debounced tactile input, 4 physical buttons
    - Camera             : OV3660 via esp_camera
    - Display             : GC9A01A 240x240 round TFT via Adafruit_GC9A01A
    - UI                  : simple text/status drawing on top of Display
    - Storage             : SD card mount + JPEG save
    - Battery             : ADC battery read (pin TBD, safe no-op until set)
    - Animations          : generic non-blocking millis()-based one-shot timer

  Target: Seeed Studio XIAO ESP32S3 Sense + OV3660 + 1.28" round GC9A01A.
  Arduino IDE only. No PlatformIO, no ESP-IDF project files, no TFT_eSPI,
  no LVGL, no WiFi, no dynamic allocation in this file's own code, and no
  delay() (millis()-based timing throughout). Every class owns exactly
  one hardware concern.

  This header is meant to be #include'd exactly once, from CuteCamera.ino.
*/

#pragma once

#include <Arduino.h>
#include <math.h> // sinf/cosf for the shutter iris's blade geometry
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include "esp_camera.h" // also pulls in sensor.h (resolution[]) and img_converters.h (frame2jpg)

// ===========================================================================
// Configuration constants
// ===========================================================================
// Preprocessor macros, not variables: they cost no RAM and are not "global
// state", just compile-time substitution shared by the classes below.

// --- Display (Adafruit_GC9A01A, 240x240 round, hardware SPI) --------------
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 240

#define TFT_CS_PIN  D1
#define TFT_DC_PIN  D2
#define TFT_RST_PIN D3
// MOSI = D10, SCK = D8 are the XIAO ESP32S3's default hardware SPI pins;
// Adafruit_GC9A01A's hardware-SPI constructor uses them implicitly.

// --- Buttons (tactile, wired to GND, INPUT_PULLUP, active-low) ------------
// D6/D7 double as UART0 TX/RX (GPIO43/44). XIAO ESP32S3's Arduino "Serial"
// is native USB-CDC, not this UART, so these pins are free for GPIO --
// just don't call Serial1.begin() anywhere in this firmware.
#define BTN_SHUTTER_PIN D4 // capture / select
#define BTN_MENU_PIN    D5 // open menu / back / mode switch
#define BTN_NEXT_PIN    D6 // navigate next / scroll down
#define BTN_PREV_PIN    D7 // navigate previous / scroll up

#define BUTTON_DEBOUNCE_MS     25
#define BUTTON_LONG_PRESS_MS   600
#define BUTTON_DOUBLE_CLICK_MS 300

// --- Camera (OV3660 via esp_camera, CAMERA_MODEL_XIAO_ESP32S3 pin map) ----
#define CAMERA_XCLK_FREQ_HZ 20000000
#define CAMERA_FRAME_SIZE   FRAMESIZE_240X240
#define CAMERA_PIXEL_FORMAT PIXFORMAT_RGB565
#define CAMERA_JPEG_QUALITY 12 // used only when encoding captures to JPEG for storage

// --- Shutter experience -----------------------------------------------------
#define SHUTTER_IRIS_DURATION_MS   300    // total iris close+open duration (~300 ms)
#define CAPTURED_HOLD_MS           800    // frozen-frame hold after the iris closes/opens
#define IRIS_BLADE_COLOR           0x2945 // dark charcoal gray blades (~RGB 40,40,44)

// --- Storage (XIAO ESP32S3 Sense onboard microSD slot) ---------------------
// This CS pin is physically shared with LED_BUILTIN on the Sense board;
// avoid driving the status LED while the SD card is in use.
#define SD_CS_PIN 21

// --- Battery monitoring -----------------------------------------------------
// Placeholder: no ADC pin has been confirmed against the battery/charge
// circuit yet. Left at -1 (invalid) on purpose so Battery::begin() safely
// reports "unavailable" instead of silently reading the wrong pin.
#define BATTERY_ADC_PIN -1

// --- Serial / diagnostics ---------------------------------------------------
#define SERIAL_BAUD_RATE 115200

// ===========================================================================
// Button - single debounced tactile input with press/release/long-press/
// click/double-click detection.
// ===========================================================================
//
// Call update() exactly once per loop() iteration; it is the only method
// that reads the pin. The event accessors (pressed/released/longPress/
// click/doubleClick) are edge-triggered: each returns true for exactly one
// update() cycle, then clears itself.
//
// click()/doubleClick()/longPress() interaction:
//  - A long press is its own gesture. Once longPress() has latched during
//    a hold, that hold's release can never start, complete, or be
//    absorbed into a click/double-click sequence.
//  - A tap (press+release, not a long press) is held "pending" until
//    either a second qualifying tap arrives within doubleClickMs (fires
//    doubleClick(), pending tap discarded) or doubleClickMs elapses with
//    no second tap (pending tap resolves into click()). This makes
//    doubleClick() always take priority over click() for the same tap.
class Button {
public:
  explicit Button(uint8_t pin,
                   uint32_t debounceMs = BUTTON_DEBOUNCE_MS,
                   uint32_t longPressMs = BUTTON_LONG_PRESS_MS,
                   uint32_t doubleClickMs = BUTTON_DOUBLE_CLICK_MS)
      : _pin(pin),
        _debounceMs(debounceMs),
        _longPressMs(longPressMs),
        _doubleClickMs(doubleClickMs) {}

  // Configures the pin (INPUT_PULLUP). Call once from setup().
  void begin() {
    pinMode(_pin, INPUT_PULLUP);
    const bool initialPressed = (digitalRead(_pin) == LOW);
    _lastRawReading = initialPressed;
    _stableState = initialPressed;
    _lastRawChangeTime = millis();
  }

  // Polls the pin, debounces it, and advances the click/long-press state
  // machine. Must be called once per loop() iteration.
  void update() {
    const uint32_t now = millis();

    const bool rawPressed = (digitalRead(_pin) == LOW); // active-low
    if (rawPressed != _lastRawReading) {
      _lastRawReading = rawPressed;
      _lastRawChangeTime = now;
    }

    if ((now - _lastRawChangeTime) >= _debounceMs && rawPressed != _stableState) {
      _stableState = rawPressed;

      if (_stableState) {
        // Confirmed press edge
        _pressedEvent = true;
        _pressStartTime = now;
        _longPressLatched = false;
      } else {
        // Confirmed release edge
        _releasedEvent = true;

        if (_longPressLatched) {
          // This release belongs to a long press -- its own gesture.
          // If an earlier tap was still pending double-click resolution,
          // that possibility is gone now, so resolve it as a single click.
          if (_awaitingSecondClick) {
            _clickEvent = true;
            _awaitingSecondClick = false;
          }
        } else if (_awaitingSecondClick && (now - _lastReleaseTime) <= _doubleClickMs) {
          _doubleClickEvent = true;
          _awaitingSecondClick = false;
        } else {
          // First tap of a possible pair; park it until the double-click
          // window (checked below) decides click() vs doubleClick().
          _awaitingSecondClick = true;
          _lastReleaseTime = now;
        }
      }
    }

    // Long-press detection, live, while held.
    if (_stableState && !_longPressLatched && (now - _pressStartTime) >= _longPressMs) {
      _longPressLatched = true;
      _longPressEvent = true;
    }

    // Resolve a pending tap into a click once the double-click window
    // elapses with no qualifying second tap. Runs every update(), not
    // just on edges, so it still fires while a later press is ongoing.
    if (_awaitingSecondClick && (now - _lastReleaseTime) > _doubleClickMs) {
      _clickEvent = true;
      _awaitingSecondClick = false;
    }
  }

  bool pressed() { const bool f = _pressedEvent; _pressedEvent = false; return f; }
  bool released() { const bool f = _releasedEvent; _releasedEvent = false; return f; }
  bool longPress() { const bool f = _longPressEvent; _longPressEvent = false; return f; }
  bool click() { const bool f = _clickEvent; _clickEvent = false; return f; }
  bool doubleClick() { const bool f = _doubleClickEvent; _doubleClickEvent = false; return f; }

  // Live debounced state, not edge-triggered.
  bool isPressed() const { return _stableState; }

private:
  const uint8_t _pin;
  const uint32_t _debounceMs;
  const uint32_t _longPressMs;
  const uint32_t _doubleClickMs;

  bool _lastRawReading = false;
  uint32_t _lastRawChangeTime = 0;

  bool _stableState = false;

  uint32_t _pressStartTime = 0;
  bool _longPressLatched = false;
  uint32_t _lastReleaseTime = 0;
  bool _awaitingSecondClick = false;

  bool _pressedEvent = false;
  bool _releasedEvent = false;
  bool _longPressEvent = false;
  bool _clickEvent = false;
  bool _doubleClickEvent = false;
};

// ===========================================================================
// Buttons - owns all 4 physical buttons and exposes them by role.
// ===========================================================================
class Buttons {
public:
  Buttons()
      : _shutter(BTN_SHUTTER_PIN),
        _menu(BTN_MENU_PIN),
        _next(BTN_NEXT_PIN),
        _prev(BTN_PREV_PIN) {}

  void begin() {
    _shutter.begin();
    _menu.begin();
    _next.begin();
    _prev.begin();
  }

  // Call once per loop() iteration, before reading any button's events.
  void update() {
    _shutter.update();
    _menu.update();
    _next.update();
    _prev.update();
  }

  Button &shutter() { return _shutter; }
  Button &menu() { return _menu; }
  Button &next() { return _next; }
  Button &prev() { return _prev; }

private:
  Button _shutter;
  Button _menu;
  Button _next;
  Button _prev;
};

// ===========================================================================
// Camera - owns the OV3660 via esp_camera. No display/UI/storage knowledge.
// ===========================================================================
class Camera {
public:
  // Brings up the sensor with the CAMERA_MODEL_XIAO_ESP32S3 pin map and
  // the CAMERA_* settings above. Returns true on success. Idempotent:
  // calling begin() again while already initialized is a no-op success.
  bool begin() {
    if (_initialized) {
      return true;
    }

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    config.pin_d0 = kY2Pin;
    config.pin_d1 = kY3Pin;
    config.pin_d2 = kY4Pin;
    config.pin_d3 = kY5Pin;
    config.pin_d4 = kY6Pin;
    config.pin_d5 = kY7Pin;
    config.pin_d6 = kY8Pin;
    config.pin_d7 = kY9Pin;
    config.pin_xclk = kXclkPin;
    config.pin_pclk = kPclkPin;
    config.pin_vsync = kVsyncPin;
    config.pin_href = kHrefPin;
    config.pin_sscb_sda = kSiodPin;
    config.pin_sscb_scl = kSiocPin;
    config.pin_pwdn = kPwdnPin;
    config.pin_reset = kResetPin;

    config.xclk_freq_hz = CAMERA_XCLK_FREQ_HZ;
    config.pixel_format = CAMERA_PIXEL_FORMAT;
    config.frame_size = CAMERA_FRAME_SIZE;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;

    // Frame buffer allocation is owned entirely by the esp_camera driver
    // (its own documented contract), not by this class.
    if (psramFound()) {
      config.fb_location = CAMERA_FB_IN_PSRAM;
      config.fb_count = 2;
    } else {
      config.fb_location = CAMERA_FB_IN_DRAM;
      config.fb_count = 1;
    }

    const esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
      Serial.printf("Camera: esp_camera_init failed (0x%x)\n", err);
      return false;
    }

    _initialized = true;
    return true;
  }

  void end() {
    if (!_initialized) {
      return;
    }
    esp_camera_deinit();
    _initialized = false;
  }

  bool isInitialized() const { return _initialized; }

  // Blocks until a frame is available (or the driver's internal timeout
  // elapses). Returns nullptr on failure. Every non-null return must be
  // passed to releaseFrame() exactly once.
  camera_fb_t *captureFrame() {
    return _initialized ? esp_camera_fb_get() : nullptr;
  }

  void releaseFrame(camera_fb_t *fb) {
    if (_initialized && fb != nullptr) {
      esp_camera_fb_return(fb);
    }
  }

  int getWidth() const {
    sensor_t *s = getSensor();
    return s ? resolution[s->status.framesize].width : 0;
  }

  int getHeight() const {
    sensor_t *s = getSensor();
    return s ? resolution[s->status.framesize].height : 0;
  }

  pixformat_t getFormat() const {
    sensor_t *s = getSensor();
    return s ? s->pixformat : CAMERA_PIXEL_FORMAT;
  }

  sensor_t *getSensor() const {
    return _initialized ? esp_camera_sensor_get() : nullptr;
  }

private:
  // CAMERA_MODEL_XIAO_ESP32S3 pin map (verified against the official
  // arduino-esp32 CameraWebServer example's camera_pins.h).
  static constexpr int kPwdnPin = -1;
  static constexpr int kResetPin = -1;
  static constexpr int kXclkPin = 10;
  static constexpr int kSiodPin = 40;
  static constexpr int kSiocPin = 39;
  static constexpr int kY9Pin = 48;
  static constexpr int kY8Pin = 11;
  static constexpr int kY7Pin = 12;
  static constexpr int kY6Pin = 14;
  static constexpr int kY5Pin = 16;
  static constexpr int kY4Pin = 18;
  static constexpr int kY3Pin = 17;
  static constexpr int kY2Pin = 15;
  static constexpr int kVsyncPin = 38;
  static constexpr int kHrefPin = 47;
  static constexpr int kPclkPin = 13;

  bool _initialized = false;
};

// ===========================================================================
// Display - owns the GC9A01A panel: init + optimized full-frame blit +
// exposes the Adafruit_GFX drawing surface for UI to draw text/shapes on.
// ===========================================================================
class Display {
public:
  // Initializes SPI + panel and clears to black. Call once from setup().
  void begin() {
    _tft.begin();
    _tft.setRotation(1);
    _tft.fillScreen(GC9A01A_BLACK);
    _initialized = true;
  }

  bool isInitialized() const { return _initialized; }

  void clear(uint16_t color = GC9A01A_BLACK) {
    if (_initialized) {
      _tft.fillScreen(color);
    }
  }

  // Blits a packed RGB565 buffer to the panel.
  //
  // bigEndianSource: true if the source bytes are already in the
  // display's expected (big-endian / MSB-first) order -- which is what
  // the OV3660 camera driver outputs -- so no per-pixel byte-swap is
  // needed (fast path). Pass false for little-endian sources such as a
  // locally-built uint16_t color array.
  //
  // If width/height don't match the 240x240 panel, the image is
  // centered and clipped (never scaled).
  void drawRGB565Frame(const uint16_t *pixels, uint16_t width, uint16_t height,
                        bool bigEndianSource = true) {
    if (!_initialized || pixels == nullptr) {
      return;
    }

    if (width == DISPLAY_WIDTH && height == DISPLAY_HEIGHT) {
      _tft.startWrite();
      _tft.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      _tft.writePixels(const_cast<uint16_t *>(pixels),
                        (uint32_t)DISPLAY_WIDTH * DISPLAY_HEIGHT, true, bigEndianSource);
      _tft.endWrite();
    } else {
      const int srcW = width;
      const int srcH = height;
      const int offX = (srcW - DISPLAY_WIDTH) / 2;
      const int offY = (srcH - DISPLAY_HEIGHT) / 2;

      _tft.startWrite();
      for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        const int sy = y + offY;
        if (sy < 0 || sy >= srcH) {
          continue;
        }

        int sx = offX;
        int rowW = DISPLAY_WIDTH;
        int dstX = 0;
        if (sx < 0) { dstX = -sx; rowW += sx; sx = 0; }
        if (sx + rowW > srcW) { rowW = srcW - sx; }
        if (rowW <= 0) {
          continue;
        }

        _tft.setAddrWindow(dstX, y, rowW, 1);
        _tft.writePixels(const_cast<uint16_t *>(pixels) + (size_t)sy * srcW + sx, rowW, true,
                          bigEndianSource);
      }
      _tft.endWrite();
    }
    // Normal shooting has zero overlay by design: nothing else is drawn
    // here. (Previous HUD concept -- LIVE badge/focus box/battery/counter
    // -- has been removed entirely, not just disabled.)
  }

  // Direct access to the drawing surface for text/shapes. Intentionally
  // exposed rather than re-wrapped: Adafruit_GFX already has a complete,
  // well-tested API and duplicating it here would add bulk without value.
  Adafruit_GC9A01A &gfx() { return _tft; }

  // ===========================================================================
  // Shutter iris animation -- a procedural DSLR-style aperture: N dark
  // rigid triangular "blades", each pivoted at a fixed point on the
  // round panel's rim. As the animation runs, each blade's free tip
  // sweeps inward *and* rotates about its pivot at the same time (the
  // way a real mechanical iris both closes and twists), so the
  // overlapping blades' silhouette closes the visible opening down to
  // nothing, holds for a single frame, then reopens back out to reveal
  // the frozen photo. Every blade is one Adafruit_GFX fillTriangle() call
  // -- no bitmaps, no sprites, no stored images, just three points
  // recomputed each tick from a closedness value (0 = fully open, 1 =
  // fully closed).
  //
  // Timing: driven by a single external Animations run (0..1 progress).
  // 0.0-0.5 is the closing half, 0.5-1.0 is the opening half, each eased
  // (smoothstep) for a mechanical accelerate/decelerate feel rather than
  // linear motion.
  //
  // Efficiency:
  //  - Closing: blade coverage only ever grows tick to tick, so nothing
  //    needs restoring -- each tick just draws the new (slightly larger,
  //    slightly more rotated) blade triangles on top of what's already
  //    on screen.
  //  - The instant closing completes, one single fillScreen() guarantees
  //    a perfectly gapless "fully closed" frame -- the brief full-screen
  //    cover the real animation calls for -- as a one-time cost, not a
  //    per-tick one.
  //  - Opening starts with one full-frame photo blit (also a one-time
  //    cost, same order as the original freeze-frame blit already paid
  //    for elsewhere), then every subsequent tick restores only the
  //    *previous* tick's blade bounding box back to the photo before
  //    drawing this tick's smaller blade there -- so, like every other
  //    effect in this file, no per-tick step ever touches the whole
  //    screen.
  // ===========================================================================

  // Resets iris state. Call once, right after freezing the captured
  // frame with drawRGB565Frame(), before the first updateShutterIris().
  void beginShutterIris() {
    for (int i = 0; i < kIrisBladeCount; i++) {
      _bladeState[i].valid = false;
    }
    _closedFrameDrawn = false;
    _openingPhotoRevealed = false;
    _lastIrisTickMs = 0;
  }

  // Call every loop() tick while the iris is in progress. progress is
  // the owning Animations instance's 0..1 progress (0..0.5 closing,
  // 0.5..1 opening). Internally throttled and cheap to call more often
  // than it actually needs to redraw.
  void updateShutterIris(const uint16_t *framePixels, float progress) {
    if (!_initialized || framePixels == nullptr) {
      return;
    }

    const uint32_t now = millis();
    if (now - _lastIrisTickMs < kIrisTickIntervalMs) {
      return;
    }
    _lastIrisTickMs = now;

    progress = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);

    // NOTE: deliberately no _tft.startWrite()/endWrite() wrapping this
    // whole method. Every call below (fillTriangle() via drawBlade(),
    // fillScreen(), drawRGB565Frame(), restoreBoundingBox()) already
    // opens/closes its own transaction. Wrapping them in another,
    // outer transaction here nests startWrite() calls -- and on ESP32,
    // SPI.beginTransaction() blocks on a non-recursive semaphore, so a
    // nested call deadlocks the whole sketch forever on the very first
    // tick. Each call below must stay a top-level, self-contained
    // transaction.

    if (progress < 0.5f) {
      // --- Closing: coverage only grows -- draw only, nothing to restore.
      const float closedness = easeInOut(progress * 2.0f);
      for (int i = 0; i < kIrisBladeCount; i++) {
        drawBlade(i, closedness);
      }
    } else if (!_closedFrameDrawn) {
      // --- The single "fully closed" instant: one deliberate full-screen
      // fill guarantees gapless coverage -- the brief moment the real
      // animation calls for -- rather than trusting triangle-union
      // rounding to seal every last seam. This tick does only this, so
      // the beat gets a dedicated frame; opening's photo reveal starts
      // on the next tick.
      _tft.fillScreen(IRIS_BLADE_COLOR);
      _closedFrameDrawn = true;
    } else if (!_openingPhotoRevealed) {
      // --- Start of opening: reveal the whole frozen photo in one blit
      // (nested inside this method's own transaction -- cheap, see
      // drawRGB565Frame()), then discard every blade's bounding box:
      // the photo is already clean everywhere, nothing to restore
      // against.
      drawRGB565Frame(framePixels, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      _openingPhotoRevealed = true;
      const float openT = (progress - 0.5f) * 2.0f;
      const float closedness = 1.0f - easeInOut(openT);
      for (int i = 0; i < kIrisBladeCount; i++) {
        drawBlade(i, closedness);
      }
    } else {
      // --- Opening, steady state: coverage only shrinks -- restore each
      // blade's previous (larger) bounding box back to the photo, then
      // draw this tick's smaller, further-rotated blade on top.
      const float openT = (progress - 0.5f) * 2.0f;
      const float closedness = 1.0f - easeInOut(openT);
      for (int i = 0; i < kIrisBladeCount; i++) {
        IrisBladeState &st = _bladeState[i];
        if (st.valid) {
          restoreBoundingBox(framePixels, st.x0, st.y0, st.x1, st.y1, st.x2, st.y2);
        }
        drawBlade(i, closedness);
      }
    }
  }

  // Call once when the iris's timer completes. One full-frame blit
  // guarantees the frame handed to CAPTURED is the clean, untouched
  // original photo, regardless of exactly which sub-phase the last tick
  // landed in -- simpler and safer than trying to restore N individual
  // blade bounding boxes for what is a one-time, whole-animation-ending
  // cleanup anyway.
  void finishShutterIris(const uint16_t *framePixels) {
    if (!_initialized || framePixels == nullptr) {
      return;
    }
    drawRGB565Frame(framePixels, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    for (int i = 0; i < kIrisBladeCount; i++) {
      _bladeState[i].valid = false;
    }
  }

  // Tiny battery-percentage readout for Gallery mode: top-right corner,
  // text only, no icon/border. NOT invoked anywhere yet -- Gallery
  // browsing (state, photo listing) isn't implemented, so nothing calls
  // this today. Ready to be wired up once Gallery exists.
  void drawBatteryPercent(int percent) {
    if (!_initialized) {
      return;
    }
    char text[6];
    snprintf(text, sizeof(text), "%d%%", percent);
    int16_t x1, y1;
    uint16_t w, h;
    _tft.setTextColor(GC9A01A_WHITE);
    _tft.setTextSize(1);
    _tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    _tft.fillRect(DISPLAY_WIDTH - 40, 0, 40, 16, GC9A01A_BLACK); // clear stale digits
    _tft.setCursor(DISPLAY_WIDTH - (int)w - 8, 8);
    _tft.print(text);
  }

private:
  // Computes blade i's rigid triangle for a given closedness (0..1):
  //  - (x0,y0)/(x1,y1): two points near the rim, kIrisOverlapFactor wider
  //    than the blade's even angular slot so neighboring blades overlap
  //    with no gaps once closed.
  //  - (x2,y2): the free tip. Its radius shrinks from the rim toward the
  //    center as closedness increases (closing the aperture) while its
  //    angle simultaneously advances by up to kIrisMaxRotationDeg
  //    (twisting the blade), matching how a real iris both closes and
  //    rotates at once.
  void computeBladeTriangle(int i, float closedness, int16_t &x0, int16_t &y0, int16_t &x1,
                             int16_t &y1, int16_t &x2, int16_t &y2) const {
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    const float slotDeg = 360.0f / (float)kIrisBladeCount;
    const float baseAngleDeg = (float)i * slotDeg;
    const float angle0 = baseAngleDeg * kDegToRad;
    const float angle1 = (baseAngleDeg + slotDeg * kIrisOverlapFactor) * kDegToRad;
    const float tipAngle =
        (baseAngleDeg + slotDeg * kIrisOverlapFactor * 0.5f + closedness * kIrisMaxRotationDeg) *
        kDegToRad;

    const int cx = DISPLAY_WIDTH / 2;
    const int cy = DISPLAY_HEIGHT / 2;
    const float rInner = kIrisOuterRadius * (1.0f - closedness);

    x0 = (int16_t)(cx + kIrisOuterRadius * cosf(angle0));
    y0 = (int16_t)(cy + kIrisOuterRadius * sinf(angle0));
    x1 = (int16_t)(cx + kIrisOuterRadius * cosf(angle1));
    y1 = (int16_t)(cy + kIrisOuterRadius * sinf(angle1));
    x2 = (int16_t)(cx + rInner * cosf(tipAngle));
    y2 = (int16_t)(cy + rInner * sinf(tipAngle));
  }

  // Computes and draws blade i at the given closedness, recording its
  // vertices in _bladeState[i] so a later opening tick can restore
  // exactly this bounding box.
  void drawBlade(int i, float closedness) {
    int16_t x0, y0, x1, y1, x2, y2;
    computeBladeTriangle(i, closedness, x0, y0, x1, y1, x2, y2);
    _tft.fillTriangle(x0, y0, x1, y1, x2, y2, IRIS_BLADE_COLOR);
    IrisBladeState &st = _bladeState[i];
    st.valid = true;
    st.x0 = x0;
    st.y0 = y0;
    st.x1 = x1;
    st.y1 = y1;
    st.x2 = x2;
    st.y2 = y2;
  }

  // Restores the axis-aligned bounding box of one blade triangle back to
  // the original frame pixels, via bulk per-row writePixels() calls (not
  // per-pixel), same technique used throughout this file. Used only
  // during opening, where blade coverage shrinks tick to tick and the
  // previous, larger triangle's box is exactly what needs to be erased.
  void restoreBoundingBox(const uint16_t *framePixels, int16_t x0, int16_t y0, int16_t x1,
                           int16_t y1, int16_t x2, int16_t y2) {
    int xMin = x0 < x1 ? x0 : x1;
    xMin = xMin < x2 ? xMin : x2;
    int xMax = x0 > x1 ? x0 : x1;
    xMax = xMax > x2 ? xMax : x2;
    int yMin = y0 < y1 ? y0 : y1;
    yMin = yMin < y2 ? yMin : y2;
    int yMax = y0 > y1 ? y0 : y1;
    yMax = yMax > y2 ? yMax : y2;

    if (xMin < 0) xMin = 0;
    if (yMin < 0) yMin = 0;
    if (xMax >= DISPLAY_WIDTH) xMax = DISPLAY_WIDTH - 1;
    if (yMax >= DISPLAY_HEIGHT) yMax = DISPLAY_HEIGHT - 1;
    if (xMin > xMax || yMin > yMax) {
      return;
    }

    // setAddrWindow()/writePixels() are raw primitives that expect an
    // already-open transaction (see drawRGB565Frame()'s convention) --
    // unlike fillTriangle()/fillScreen(), they do not open their own.
    // This is the only such raw call site in the iris animation, so it
    // owns exactly one top-level startWrite()/endWrite() pair itself.
    _tft.startWrite();
    uint16_t rowBuf[DISPLAY_WIDTH];
    const int count = xMax - xMin + 1;
    for (int y = yMin; y <= yMax; y++) {
      const uint16_t *src = framePixels + (size_t)y * DISPLAY_WIDTH + xMin;
      for (int x = 0; x < count; x++) {
        rowBuf[x] = __builtin_bswap16(src[x]);
      }
      _tft.setAddrWindow(xMin, y, count, 1);
      _tft.writePixels(rowBuf, count, true, false);
    }
    _tft.endWrite();
  }

  // Smoothstep easing: accelerate then decelerate, for a mechanical
  // rather than linear-feeling sweep.
  static float easeInOut(float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
  }

  // What was actually drawn for a blade on the last tick, so opening can
  // restore (erase back to the original frame) exactly that bounding box
  // before drawing the next, smaller blade.
  struct IrisBladeState {
    bool valid = false;
    int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  };

  static constexpr int kIrisBladeCount = 8;       // 7-9 blade DSLR-style iris; 8 chosen
  static constexpr float kIrisOuterRadius = (float)DISPLAY_WIDTH / 2.0f; // pivots sit at the rim
  static constexpr float kIrisOverlapFactor = 1.3f;  // each blade spans 30% more than its even
                                                       // slot, so neighbors overlap with no gaps
  static constexpr float kIrisMaxRotationDeg = 75.0f; // blades twist up to 75 deg while closing
  static constexpr uint32_t kIrisTickIntervalMs = 15; // bounds per-tick work; ~20 ticks over 300ms

  Adafruit_GC9A01A _tft{TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN};
  bool _initialized = false;
  IrisBladeState _bladeState[kIrisBladeCount];
  bool _closedFrameDrawn = false;
  bool _openingPhotoRevealed = false;
  uint32_t _lastIrisTickMs = 0;
};

// ===========================================================================
// UI - draws simple text/status screens on top of a Display. Does not own
// the display (composition, not inheritance) and knows nothing about the
// camera or storage.
// ===========================================================================
class UI {
public:
  explicit UI(Display &display) : _display(display) {}

  void showBootScreen(const char *title) {
    Adafruit_GC9A01A &gfx = _display.gfx();
    gfx.fillScreen(GC9A01A_BLACK);
    gfx.setTextColor(GC9A01A_WHITE);
    gfx.setTextSize(2);
    centerText(gfx, title);
  }

  // Centered, single-line status text over a solid background. Intended
  // for brief transient messages ("Saved", "Save failed", ...).
  void showMessage(const char *message) {
    Adafruit_GC9A01A &gfx = _display.gfx();
    gfx.fillScreen(GC9A01A_BLACK);
    gfx.setTextColor(GC9A01A_WHITE);
    gfx.setTextSize(2);
    centerText(gfx, message);
  }

  // (Instant full-screen white flash removed -- shutter feedback is now
  // the sensor-scan animation in Display::updateShutterScan(), which
  // draws directly on Display without going through UI.)

private:
  void centerText(Adafruit_GC9A01A &gfx, const char *text) {
    int16_t x1, y1;
    uint16_t w, h;
    gfx.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    gfx.setCursor((DISPLAY_WIDTH - (int)w) / 2, (DISPLAY_HEIGHT - (int)h) / 2);
    gfx.print(text);
  }

  Display &_display;
};

// ===========================================================================
// Storage - SD card mount + JPEG save. No camera/display/UI knowledge.
// ===========================================================================
class Storage {
public:
  // Mounts the microSD card. Returns true on success. It is safe for the
  // rest of the firmware to keep running even if this returns false --
  // saveFrameAsJpeg() will just report failure until a card is present.
  bool begin() {
    _mounted = SD.begin(SD_CS_PIN);
    return _mounted;
  }

  bool isMounted() const { return _mounted; }

  // Encodes a camera frame buffer to JPEG and writes it to `path` (e.g.
  // "/IMG_1234.jpg") on the SD card. Returns true only if both the JPEG
  // encode and the file write succeeded.
  //
  // frame2jpg() (from the esp_camera library) allocates its output
  // buffer internally; freeing it here is releasing memory owned by that
  // library call, per its documented API contract -- not dynamic
  // allocation performed by this class.
  bool saveFrameAsJpeg(camera_fb_t *fb, const char *path, uint8_t quality = CAMERA_JPEG_QUALITY) {
    if (!_mounted || fb == nullptr) {
      return false;
    }

    uint8_t *jpegBuf = nullptr;
    size_t jpegLen = 0;
    if (!frame2jpg(fb, quality, &jpegBuf, &jpegLen)) {
      return false;
    }

    bool ok = false;
    File file = SD.open(path, FILE_WRITE);
    if (file) {
      ok = (file.write(jpegBuf, jpegLen) == jpegLen);
      file.close();
    }

    free(jpegBuf);
    return ok;
  }

private:
  bool _mounted = false;
};

// ===========================================================================
// Battery - ADC-based battery read. Pin is a placeholder (BATTERY_ADC_PIN
// == -1) until the actual sense circuit is confirmed; all methods degrade
// safely to "unavailable" until then.
// ===========================================================================
class Battery {
public:
  bool begin() {
    _available = (BATTERY_ADC_PIN >= 0);
    if (_available) {
      pinMode(BATTERY_ADC_PIN, INPUT);
    }
    return _available;
  }

  bool isAvailable() const { return _available; }

  // Calibrated ADC reading in millivolts at the sense pin, or -1 if no
  // pin has been configured. NOTE: does not yet apply the battery
  // circuit's voltage-divider ratio -- confirm that ratio before treating
  // this as an absolute battery voltage.
  int readMilliVolts() const {
    if (!_available) {
      return -1;
    }
    return (int)analogReadMilliVolts(BATTERY_ADC_PIN);
  }

private:
  bool _available = false;
};

// ===========================================================================
// Animations - generic non-blocking one-shot timer (millis()-based). Used
// for shutter-flash duration, toast/message duration, and future UI
// transitions. Deliberately has no drawing knowledge of its own.
// ===========================================================================
class Animations {
public:
  void start(uint32_t durationMs) {
    _startTime = millis();
    _duration = durationMs;
    _running = true;
  }

  bool isRunning() const { return _running; }

  // Call once per loop() iteration while isRunning() is true. Returns
  // true on the single cycle where the timer just completed.
  bool update() {
    if (!_running) {
      return false;
    }
    if (millis() - _startTime >= _duration) {
      _running = false;
      return true;
    }
    return false;
  }

  // 0.0 .. 1.0 progress through the current run; 1.0 if not running.
  float progress() const {
    if (!_running) {
      return 1.0f;
    }
    const uint32_t elapsed = millis() - _startTime;
    return elapsed >= _duration ? 1.0f : (float)elapsed / (float)_duration;
  }

private:
  uint32_t _startTime = 0;
  uint32_t _duration = 0;
  bool _running = false;
};
