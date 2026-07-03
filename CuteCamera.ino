/*
  CuteCamera.ino

  Application logic only: the state machine and setup()/loop(). All
  reusable hardware classes (Camera, Display, Buttons, UI, Storage,
  Battery, Animations) live in CameraLib.h.

  Current milestone (feature slice 5 -- DSLR iris shutter):
    BOOT -> LIVE PREVIEW -> CAPTURE -> CAPTURED -> back to LIVE PREVIEW
  LIVE PREVIEW is full-screen with zero overlay (no HUD, no text, no
  icons -- see Display::drawRGB565Frame() in CameraLib.h). CAPTURE plays
  a procedural DSLR-style iris/aperture animation over the frozen frame:
  8 dark blades close inward and rotate, briefly cover the screen, then
  reopen to reveal the frozen photo
  (Display::beginShutterIris()/updateShutterIris()/finishShutterIris());
  CAPTURED just holds the result on screen -- no "Captured" text, no
  icon, the animation itself is the confirmation. Storage is disabled
  until SD hardware exists. Battery is removed for now (no confirmed ADC
  pin/circuit). GALLERY / SETTINGS / SLEEP states, and CameraMode
  switching, are not wired up yet -- they need Storage file listing,
  persisted settings, and power management that don't exist yet, and
  will be added feature by feature. Display::drawBatteryPercent() exists
  ready for Gallery's tiny "97%"-style readout, but nothing calls it yet.

  "No globals": setup()/loop() need one persistent object to bridge
  calls, so CuteCameraApp is held as a function-local static inside
  App() (a Meyer's-singleton accessor) rather than a plain file-scope
  variable. It is constructed on first use and is not reachable from
  anywhere except through App().
*/

#include "CameraLib.h"

enum class AppState : uint8_t {
  BOOT,
  LIVE_PREVIEW,
  CAPTURE,
  CAPTURED,    // holds the scanned frame on screen (no text/icon), then back to LIVE_PREVIEW
  FATAL_ERROR, // unrecoverable init failure (camera not detected, etc.)
};

enum class CameraMode : uint8_t {
  PHOTO,
  GALLERY,
  SETTINGS,
};

class CuteCameraApp {
public:
  void begin() {
    Serial.begin(SERIAL_BAUD_RATE);

    _display.begin();
    _ui.showBootScreen("CuteCamera");

    _buttons.begin();
    // Storage disabled until SD hardware exists: intentionally not calling
    // _storage.begin(). CAPTURED does not attempt to save.

    if (!_camera.begin()) {
      _ui.showMessage("Camera init failed");
      _state = AppState::FATAL_ERROR;
      return;
    }

    _state = AppState::LIVE_PREVIEW;
  }

  void update() {
    _buttons.update(); // must run first: everything else reads button events this cycle

    switch (_state) {
      case AppState::BOOT:
        // begin() always transitions out of BOOT before the first update().
        break;
      case AppState::LIVE_PREVIEW:
        updateLivePreview();
        break;
      case AppState::CAPTURE:
        updateCapture();
        break;
      case AppState::CAPTURED:
        updateCaptured();
        break;
      case AppState::FATAL_ERROR:
        // Camera failed to initialize; nothing further can run safely.
        // No delay() per project rules -- just idle.
        break;
    }
  }

private:
  void updateLivePreview() {
    camera_fb_t *fb = _camera.captureFrame();
    if (fb != nullptr) {
      _display.drawRGB565Frame(reinterpret_cast<const uint16_t *>(fb->buf), fb->width, fb->height);
      _camera.releaseFrame(fb);
    }

    if (_buttons.shutter().pressed()) {
      _state = AppState::CAPTURE;
    }
  }

  void updateCapture() {
    if (_capturedFrame == nullptr) {
      // First tick in this state: grab the frame, freeze it on screen
      // with the one full-frame blit, and start the iris timer.
      _capturedFrame = _camera.captureFrame();
      if (_capturedFrame == nullptr) {
        _state = AppState::LIVE_PREVIEW; // capture failed; don't get stuck
        return;
      }
      _display.drawRGB565Frame(reinterpret_cast<const uint16_t *>(_capturedFrame->buf),
                                _capturedFrame->width, _capturedFrame->height);
      _display.beginShutterIris();
      _flash.start(SHUTTER_IRIS_DURATION_MS);
    }

    const auto *pixels = reinterpret_cast<const uint16_t *>(_capturedFrame->buf);

    if (_flash.update()) {
      // Timer just completed: guarantee the frame is back to the clean
      // original photo, then move on. No text, no icons -- the
      // animation itself is the confirmation.
      _display.finishShutterIris(pixels);
      _state = AppState::CAPTURED;
      return;
    }

    _display.updateShutterIris(pixels, _flash.progress());
  }

  void updateCaptured() {
    if (_capturedFrame != nullptr) {
      // One-shot: release the frame and hold the frozen image (already
      // restored to its original pixels by finishShutterIris()) on
      // screen for a moment. No message, no icon -- nothing further is
      // drawn during the hold.
      _camera.releaseFrame(_capturedFrame);
      _capturedFrame = nullptr;

      _toast.start(CAPTURED_HOLD_MS);
      return; // stay in CAPTURED this cycle
    }

    _toast.update();
    if (!_toast.isRunning()) {
      _state = AppState::LIVE_PREVIEW;
    }
  }

  Camera _camera;
  Display _display;
  Buttons _buttons;
  UI _ui{_display};
  Storage _storage; // disabled: begin() not called until SD hardware exists
  Animations _flash;
  Animations _toast;

  AppState _state = AppState::BOOT;
  CameraMode _mode = CameraMode::PHOTO;
  camera_fb_t *_capturedFrame = nullptr;
};

CuteCameraApp &App() {
  static CuteCameraApp instance;
  return instance;
}

void setup() {
  App().begin();
}

void loop() {
  App().update();
}
