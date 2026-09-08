#ifndef SUGAROTA_INPUT_H
#define SUGAROTA_INPUT_H

#include "config.h"
#include <SensorQMI8658.hpp>

extern SensorQMI8658 qmi;
extern bool imuReady;

void initInputs();
void checkButtons();
void checkButton(ButtonState &btn, const char* name);
bool readTouch(int &tx, int &ty);
void checkTouch();
void pollIMU();

#endif // SUGAROTA_INPUT_H
