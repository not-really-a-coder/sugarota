#include "audio.h"
#include "sugarota_ble.h"

esp_codec_dev_handle_t playback = NULL;

// Forward declarations
void checkSerialConsole();
void updateUI();

void spinnerDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    checkSerialConsole();
    SugarotaBLE::getInstance().update();
    
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
