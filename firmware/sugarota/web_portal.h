#ifndef SUGAROTA_WEB_PORTAL_H
#define SUGAROTA_WEB_PORTAL_H

#include "config.h"
#include <WebServer.h>
#include <ESPmDNS.h>

extern WebServer server;

void setupWebPortal();
void handleConfigPage();
void handleGetConfig();
void handleSaveConfig();

#endif // SUGAROTA_WEB_PORTAL_H
