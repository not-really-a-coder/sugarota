#include "audio.h"
#include "ble.h"
#include "input.h"

esp_codec_dev_handle_t playback = NULL;

// Forward declarations
void checkSerialConsole();
void updateUI();

void spinnerDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    checkSerialConsole();
    SugarotaBLE::getInstance().update();
    if (isBooting) {
      checkBootButtons();
    }
    if (!deviceOn)
      return;

    if (isFetching) {
      if (isTimerMode) {
        if (!isTimerStopped) {
          timerElapsedMs = millis() - timerStartTime;
        }
      }
      static unsigned long lastSpinnerFrameTime = 0;
      int interval = (isTimerMode && !isTimerStopped) ? 50 : 150;
      if (millis() - lastSpinnerFrameTime > interval) {
        lastSpinnerFrameTime = millis();
        updateUI();
      }
    }
    delay(10);
  }
}

void setVolume(int level) {
  if (level < 0) level = 0;
  if (level > 3) level = 3;
  volumeLevel = level;
  if (playback) {
    float volFloat = 0.0;
    if (level == 1) volFloat = 35.0;
    else if (level == 2) volFloat = 70.0;
    else if (level == 3) volFloat = 100.0;
    esp_codec_dev_set_out_vol(playback, volFloat);
    DBG_PRINTF("Audio: Volume preset set to %d (%.1f%%)\n", volumeLevel, volFloat);
  }
}

void initAudioCodec() {
  set_codec_board_type("S3_LCD_3_49");
  codec_init_cfg_t codec_cfg;
  codec_cfg.in_mode = CODEC_I2S_MODE_NONE;
  codec_cfg.out_mode = CODEC_I2S_MODE_TDM;
  codec_cfg.in_use_tdm = false;
  codec_cfg.reuse_dev = false;
  init_codec(&codec_cfg);
  playback = get_playback_handle();
  if (playback) {
    setVolume(volumeLevel);
    DBG_PRINTLN("Audio Codec Initialized");
  }
}

void codecBeep(int durationMs) {
  if (!playback)
    return;

  const int chunkFrames = 1200;
  static int16_t buf[chunkFrames * 2];
  static bool bufInitialized = false;

  if (!bufInitialized) {
    for (int i = 0; i < chunkFrames; i++) {
      int16_t val = ((i / 6) % 2 == 0) ? 15000 : -15000;
      buf[i * 2] = val;     // Left
      buf[i * 2 + 1] = val; // Right
    }
    bufInitialized = true;
  }

  esp_codec_dev_sample_info_t fs;
  memset(&fs, 0, sizeof(fs));
  fs.sample_rate = 24000;
  fs.channel = 2;
  fs.bits_per_sample = 16;
  esp_codec_dev_open(playback, &fs);

  int elapsed = 0;
  while (elapsed < durationMs) {
    int playMs = min(50, durationMs - elapsed);
    int playFrames = 24000 * playMs / 1000;
    esp_codec_dev_write(playback, buf, playFrames * 4);
    elapsed += playMs;
    spinnerDelay(playMs);
  }

  esp_codec_dev_close(playback);
}

// Generates tone with custom halfPeriodFrames (e.g. 4 => 3000 Hz tone, distinct
// from 2000 Hz timer beep)
void codecBeepTone(int durationMs, int halfPeriodFrames) {
  if (!playback)
    return;
  if (halfPeriodFrames < 1)
    halfPeriodFrames = 4;

  const int chunkFrames = 1200;
  int16_t toneBuf[chunkFrames * 2];

  for (int i = 0; i < chunkFrames; i++) {
    int16_t val = ((i / halfPeriodFrames) % 2 == 0) ? 28000 : -28000;
    toneBuf[i * 2] = val;     // Left
    toneBuf[i * 2 + 1] = val; // Right
  }

  esp_codec_dev_sample_info_t fs;
  memset(&fs, 0, sizeof(fs));
  fs.sample_rate = 24000;
  fs.channel = 2;
  fs.bits_per_sample = 16;
  esp_codec_dev_open(playback, &fs);

  int elapsed = 0;
  while (elapsed < durationMs) {
    int playMs = min(50, durationMs - elapsed);
    int playFrames = 24000 * playMs / 1000;
    esp_codec_dev_write(playback, toneBuf, playFrames * 4);
    elapsed += playMs;
    spinnerDelay(playMs);
  }

  esp_codec_dev_close(playback);
}

void playWav(const char *path) {
  if (!playback)
    return;

  File f = LittleFS.open(path, "r");
  if (!f) {
    DBG_PRINTLN("Failed to open WAV file!");
    return;
  }

  uint8_t header[44];
  if (f.read(header, 44) != 44) {
    f.close();
    return;
  }

  const int bufSize = 4096;
  static uint8_t buf[bufSize];

  esp_codec_dev_sample_info_t fs;
  memset(&fs, 0, sizeof(fs));
  fs.sample_rate = 24000;
  fs.channel = 2;
  fs.bits_per_sample = 16;
  esp_codec_dev_open(playback, &fs);

  while (f.available()) {
    int bytesRead = f.read(buf, bufSize);
    if (bytesRead <= 0)
      break;

    esp_codec_dev_write(playback, buf, bytesRead);
    spinnerDelay(5);
  }

  esp_codec_dev_close(playback);
  f.close();
}

void playBeeps(int longBeeps, int shortBeeps) {
  bool hasCustomLong = LittleFS.exists("/beep_long.wav");
  bool hasCustomShort = LittleFS.exists("/beep_short.wav");

  for (int i = 0; i < longBeeps; i++) {
    if (hasCustomLong) {
      playWav("/beep_long.wav");
    } else {
      codecBeep(450);
    }
    spinnerDelay(150);
  }
  for (int i = 0; i < shortBeeps; i++) {
    if (hasCustomShort) {
      playWav("/beep_short.wav");
    } else {
      codecBeep(112);
    }
    spinnerDelay(150);
  }
}

// Find Device Alert State Machine
// 5 consecutive beeps 3 times with 3 seconds pause between beep sets
static bool findDeviceRunning = false;
static int findDeviceRepetition = 0; // 0, 1, 2 (total 3)
static int findDeviceBeepInRep = 0;  // 0 to 4 (total 5 beeps)
static bool findDeviceIsBeeping = false;
static unsigned long findDeviceNextActionTime = 0;

void startFindDeviceAlert() {
  if (playback) {
    esp_codec_dev_set_out_vol(playback, 100.0);
  }
  findDeviceRunning = true;
  findDeviceRepetition = 0;
  findDeviceBeepInRep = 0;
  findDeviceIsBeeping = false;
  findDeviceNextActionTime = millis();
  DBG_PRINTLN(
      "FIND DEVICE: Alert started (5 beeps x 3 reps, 3s pause, 100% volume)");
}

void stopFindDeviceAlert() {
  if (findDeviceRunning) {
    findDeviceRunning = false;
    setVolume(volumeLevel);
    DBG_PRINTLN("FIND DEVICE: Alert cancelled");
  }
}

bool isFindDeviceActive() { return findDeviceRunning; }

void updateFindDevice() {
  if (!findDeviceRunning)
    return;

  unsigned long now = millis();
  if (now < findDeviceNextActionTime)
    return;

  if (findDeviceIsBeeping) {
    // Current beep just finished (it was played synchronously in codecBeepTone)
    findDeviceIsBeeping = false;
    findDeviceBeepInRep++;

    if (findDeviceBeepInRep >= 5) {
      // Finished 5 beeps for this repetition
      findDeviceRepetition++;
      findDeviceBeepInRep = 0;

      if (findDeviceRepetition >= 3) {
        // Finished all 3 repetitions!
        findDeviceRunning = false;
        setVolume(volumeLevel);
        DBG_PRINTLN("FIND DEVICE: Alert pattern complete");
        return;
      } else {
        // 3 seconds pause between repetitions
        findDeviceNextActionTime = now + 3000;
        return;
      }
    } else {
      // Inter-beep gap within the 5-beep sequence: 100ms
      findDeviceNextActionTime = now + 100;
      return;
    }
  } else {
    // Start next beep: 100ms duration at 3000 Hz tone (halfPeriodFrames = 4 at
    // 24kHz)
    findDeviceIsBeeping = true;
    codecBeepTone(100, 4);
    // Beep took 100ms; next action checks right away
    findDeviceNextActionTime = millis();
  }
}

// --- Night Mode Data Alert State Machine ---
// Requirement: 4 beeps, 1 sec pause, 4 beeps, 1 sec pause, 4 beeps.
static bool nmAlertRunning = false;
static int nmAlertRepetition = 0; // 0, 1, 2 (total 3 sets of 4 beeps)
static int nmAlertBeepInRep = 0;  // 0 to 3 (total 4 beeps per set)
static bool nmAlertIsBeeping = false;
static unsigned long nmAlertNextActionTime = 0;

void startNightModeDataAlert() {
  if (findDeviceRunning) return; // Don't interrupt Find Phone alert
  nmAlertRunning = true;
  nmAlertRepetition = 0;
  nmAlertBeepInRep = 0;
  nmAlertIsBeeping = false;
  nmAlertNextActionTime = millis();
  DBG_PRINTLN("NIGHT MODE ALERT: Started (4 beeps x 3 reps, 1s pause)");
}

void stopNightModeDataAlert() {
  if (nmAlertRunning) {
    nmAlertRunning = false;
    DBG_PRINTLN("NIGHT MODE ALERT: Stopped");
  }
}

bool isNightModeAlertActive() {
  return nmAlertRunning;
}

void updateNightModeAlert() {
  if (!nmAlertRunning) return;

  unsigned long now = millis();
  if (now < nmAlertNextActionTime) return;

  if (nmAlertIsBeeping) {
    nmAlertIsBeeping = false;
    nmAlertBeepInRep++;

    if (nmAlertBeepInRep >= 4) {
      // Finished 4 beeps for this repetition
      nmAlertRepetition++;
      nmAlertBeepInRep = 0;

      if (nmAlertRepetition >= 3) {
        // Finished all 3 sets
        nmAlertRunning = false;
        DBG_PRINTLN("NIGHT MODE ALERT: Pattern complete");
        return;
      } else {
        // 1 second pause between sets
        nmAlertNextActionTime = now + 1000;
        return;
      }
    } else {
      // Inter-beep gap within the 4-beep set: 100ms
      nmAlertNextActionTime = now + 100;
      return;
    }
  } else {
    // Play a short alert beep (112ms tone)
    nmAlertIsBeeping = true;
    codecBeepTone(112, 6); // ~2000 Hz tone
    nmAlertNextActionTime = millis();
  }
}

