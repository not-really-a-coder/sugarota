#include "audio.h"
#include "ble.h"
#include "input.h"

esp_codec_dev_handle_t playback = NULL;
esp_codec_dev_handle_t record = NULL;

// Forward declarations
void checkSerialConsole();
void updateUI();

// PSRAM Voice Recording Buffer (Up to 10 seconds at 24000 Hz 16-bit mono = 480,000 bytes)
// 24000 samples/sec * 2 bytes/sample * 1 channel * 10 sec = 480,000 bytes
static const uint32_t MAX_AUDIO_BYTES = 480000;
static uint8_t *voiceBuffer = NULL;
static uint32_t voiceRecordedLength = 0;
static bool recordDeviceOpened = false;

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
  record = get_record_handle();
  if (playback) {
    esp_codec_dev_set_out_vol(playback, 75.0);
    DBG_PRINTLN("Audio Codec Initialized (Playback Volume: 75%)");
  }
  if (record) {
    esp_codec_dev_set_in_gain(record, 35.0);
    DBG_PRINTLN("Audio Codec Initialized (Record Gain: 35dB)");
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

// ==========================================
// Countdown Alarm Voice Recording & Playback
// ==========================================

bool startVoiceRecording() {
  if (!record) {
    record = get_record_handle();
    if (!record) {
      DBG_PRINTLN("RECORD: Error - No record handle!");
      return false;
    }
  }

  // Allocate PSRAM buffer if not yet allocated
  if (!voiceBuffer) {
    voiceBuffer = (uint8_t *)heap_caps_malloc(MAX_AUDIO_BYTES, MALLOC_CAP_SPIRAM);
    if (!voiceBuffer) {
      DBG_PRINTLN("RECORD: Failed to allocate PSRAM voice buffer!");
      return false;
    }
  }

  // Clear previous recording
  voiceRecordedLength = 0;
  memset(voiceBuffer, 0, MAX_AUDIO_BYTES);

  esp_codec_dev_sample_info_t fs = {};
  fs.sample_rate = 24000;
  fs.channel = 2; // ES7210 TDM config uses 2/4 channel capture
  fs.bits_per_sample = 16;
  
  if (esp_codec_dev_open(record, &fs) != ESP_CODEC_DEV_OK) {
    DBG_PRINTLN("RECORD: Failed to open record device");
    return false;
  }
  recordDeviceOpened = true;
  DBG_PRINTLN("RECORD: Started voice recording...");
  return true;
}

bool recordVoiceChunk() {
  if (!record || !recordDeviceOpened || !voiceBuffer) return false;
  if (voiceRecordedLength >= MAX_AUDIO_BYTES) {
    stopVoiceRecording();
    return false;
  }

  const uint32_t chunkSize = 2048;
  uint32_t toRead = min(chunkSize, MAX_AUDIO_BYTES - voiceRecordedLength);
  int res = esp_codec_dev_read(record, voiceBuffer + voiceRecordedLength, toRead);
  if (res == ESP_CODEC_DEV_OK) {
    voiceRecordedLength += toRead;
    return (voiceRecordedLength < MAX_AUDIO_BYTES);
  }
  return false;
}

void stopVoiceRecording() {
  if (record && recordDeviceOpened) {
    esp_codec_dev_close(record);
    recordDeviceOpened = false;
  }
  DBG_PRINTF("RECORD: Stopped voice recording. Bytes recorded: %u\n", voiceRecordedLength);
}

bool hasVoiceRecording() {
  return (voiceBuffer != NULL && voiceRecordedLength > 0);
}

void deleteVoiceRecording() {
  voiceRecordedLength = 0;
  if (voiceBuffer) {
    memset(voiceBuffer, 0, MAX_AUDIO_BYTES);
  }
  DBG_PRINTLN("RECORD: Deleted voice recording");
}

void playVoiceRecording() {
  if (!playback || !hasVoiceRecording()) return;

  esp_codec_dev_sample_info_t fs = {};
  fs.sample_rate = 24000;
  fs.channel = 2;
  fs.bits_per_sample = 16;

  if (esp_codec_dev_open(playback, &fs) != ESP_CODEC_DEV_OK) {
    DBG_PRINTLN("PLAYBACK: Failed to open playback device for voice");
    return;
  }

  uint32_t offset = 0;
  const uint32_t chunkSize = 4096;
  while (offset < voiceRecordedLength) {
    uint32_t toWrite = min(chunkSize, voiceRecordedLength - offset);
    esp_codec_dev_write(playback, voiceBuffer + offset, toWrite);
    offset += toWrite;
    spinnerDelay(5);
  }

  esp_codec_dev_close(playback);
}

// When alarm expires:
// 1) 3 ascending tones
// 2) Pause 1 sec
// 3) Play recorded message (if recorded)
// 4) Pause 1 sec
// 5) 3 descending tones
void playAlarmAudioSequence() {
  if (!playback) return;

  int originalVol = 75;
  esp_codec_dev_get_out_vol(playback, &originalVol);
  esp_codec_dev_set_out_vol(playback, 100.0); // Full volume for alarm

  // 1) Three ascending tones
  // Frequencies: ~1500 Hz (halfPeriod 8), ~2200 Hz (halfPeriod 5), ~3000 Hz (halfPeriod 4)
  codecBeepTone(150, 8);
  spinnerDelay(100);
  codecBeepTone(150, 5);
  spinnerDelay(100);
  codecBeepTone(200, 4);

  // 2) Pause 1 sec
  spinnerDelay(1000);

  // 3) Play recorded message
  if (hasVoiceRecording()) {
    playVoiceRecording();
  }

  // 4) Pause 1 sec
  spinnerDelay(1000);

  // 5) Three descending tones
  // Frequencies: ~3000 Hz (halfPeriod 4), ~2200 Hz (halfPeriod 5), ~1500 Hz (halfPeriod 8)
  codecBeepTone(150, 4);
  spinnerDelay(100);
  codecBeepTone(150, 5);
  spinnerDelay(100);
  codecBeepTone(250, 8);

  esp_codec_dev_set_out_vol(playback, (float)originalVol);
}

