#ifndef SUGAROTA_WEB_PORTAL_H
#define SUGAROTA_WEB_PORTAL_H

#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

extern WebServer server;

void setupWebPortal();
void handleConfigPage();
void handleGetConfig();
void handleSaveConfig();
void handleOTAStatus();
void handleOTAUpload();

#endif // SUGAROTA_WEB_PORTAL_H

