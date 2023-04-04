#include <Arduino.h>

#include "bms_relay.h"
#include "network.h"
#include "packet.h"
#include "settings.h"
#include "task_queue.h"




namespace {



HardwareSerial Serial(0);
}  // namespace

BmsRelay *relay;

void bms_setup() {
  relay = new BmsRelay([]() { return Serial.read(); },
                       [](uint8_t b) {
                         // This if statement is what implements locking.
                         if (!Settings->is_locked) {
                           Serial.write(b);
                         }
                       },
                       millis);
  Serial.begin(115200);

  // The B line idle is 0

  pinMode(LED_BUILTIN, OUTPUT);



  relay->addReceivedPacketCallback([](BmsRelay *, Packet *packet) {
    static uint8_t ledState = 0;
    digitalWrite(LED_BUILTIN, ledState);
    ledState = 1 - ledState;
    streamBMSPacket(packet->start(), packet->len());
  });
  relay->setUnknownDataCallback([](uint8_t b) {
    static std::vector<uint8_t> unknownData = {0};
    if (unknownData.size() > 128) {
      return;
    }
    unknownData.push_back(b);
    streamBMSPacket(&unknownData[0], unknownData.size());
  });

  relay->setPowerOffCallback([]() {
    Settings->graceful_shutdown_count++;
    saveSettings();
  });

  relay->setBMSSerialOverride(0xFFABCDEF);

  setupWifi();
  setupWebServer(relay);
  TaskQueue.postRecurringTask([]() { relay->loop(); });
}
