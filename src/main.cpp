#include <Arduino.h>

#include "ESP8266WiFi.h"
#include "bms_main.h"
#include "dprint.h"
#include "recovery.h"
#include "settings.h"
#include "task_queue.h"



extern "C" void setup() {
  WiFi.persistent(false);
  loadSettings();

    bms_setup();
  }


extern "C" void loop() { TaskQueue.process(); }