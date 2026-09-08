#ifndef SUGAROTA_NET_CLIENT_H
#define SUGAROTA_NET_CLIENT_H

#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SensorPCF85063.hpp>

extern SensorPCF85063 rtc;

void connectWiFi();
void fetchData();
void parseResponse(const String& payload);
bool loginDexcom();
String parseTrend(JsonObject obj);
void restoreTimeFromRTC();

#endif // SUGAROTA_NET_CLIENT_H
