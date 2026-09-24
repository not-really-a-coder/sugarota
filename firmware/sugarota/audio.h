#ifndef SUGAROTA_AUDIO_H
#define SUGAROTA_AUDIO_H

#include "config.h"
#include "src/codec_board/codec_board.h"
#include "src/codec_board/codec_init.h"
#include <LittleFS.h>

extern esp_codec_dev_handle_t playback;
extern esp_codec_dev_handle_t record;

void initAudioCodec();
void codecBeep(int durationMs);
void codecBeepTone(int durationMs, int halfPeriodFrames);
void playWav(const char *path);
void playBeeps(int longBeeps, int shortBeeps);
void spinnerDelay(unsigned long ms);

void startFindDeviceAlert();
void stopFindDeviceAlert();
void updateFindDevice();
bool isFindDeviceActive();

// Countdown Alarm Audio & Recording API
bool startVoiceRecording();
bool recordVoiceChunk();
void stopVoiceRecording();
bool hasVoiceRecording();
void deleteVoiceRecording();
void playVoiceRecording();
void playAlarmAudioSequence();

#endif // SUGAROTA_AUDIO_H
