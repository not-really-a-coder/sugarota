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
    if (!deviceOn) return;
    
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

void initAudioCodec() {
  set_codec_board_type("S3_LCD_3_49");
  codec_init_cfg_t codec_cfg;
  codec_cfg.in_mode = CODEC_I2S_MODE_TDM;
  codec_cfg.out_mode = CODEC_I2S_MODE_TDM;
  codec_cfg.in_use_tdm = false;
  codec_cfg.reuse_dev = false;
  init_codec(&codec_cfg);
  playback = get_playback_handle();
  if (playback) {
    esp_codec_dev_set_out_vol(playback, 75.0);
    DBG_PRINTLN("Audio Codec Initialized (Volume: 75%)");
  }
}

void codecBeep(int durationMs) {
  if (!playback) return;
  
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

// Generates tone with custom halfPeriodFrames (e.g. 4 => 3000 Hz tone, distinct from 2000 Hz timer beep)
void codecBeepTone(int durationMs, int halfPeriodFrames) {
  if (!playback) return;
  if (halfPeriodFrames < 1) halfPeriodFrames = 4;
  
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
  if (!playback) return;
  
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
    if (bytesRead <= 0) break;
    
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
  DBG_PRINTLN("FIND DEVICE: Alert started (5 beeps x 3 reps, 3s pause, 100% volume)");
}

void stopFindDeviceAlert() {
  if (findDeviceRunning) {
    findDeviceRunning = false;
    if (playback) {
      esp_codec_dev_set_out_vol(playback, 75.0);
    }
    DBG_PRINTLN("FIND DEVICE: Alert cancelled");
  }
}

bool isFindDeviceActive() {
  return findDeviceRunning;
}

void updateFindDevice() {
  if (!findDeviceRunning) return;

  unsigned long now = millis();
  if (now < findDeviceNextActionTime) return;

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
        if (playback) {
          esp_codec_dev_set_out_vol(playback, 75.0);
        }
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
    // Start next beep: 100ms duration at 3000 Hz tone (halfPeriodFrames = 4 at 24kHz)
    findDeviceIsBeeping = true;
    codecBeepTone(100, 4);
    // Beep took 100ms; next action checks right away
    findDeviceNextActionTime = millis();
  }
}

