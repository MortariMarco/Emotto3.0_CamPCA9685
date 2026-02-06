#pragma once
#include <Arduino.h>

void ExpressionsCmd_Init(HWCDC* serial);
void ExpressionsCmd_Poll();  // da chiamare ogni loop
void ExpressionsCmd_UpdateIdle(unsigned long now);
