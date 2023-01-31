#include "network.h"

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>

#include <functional>

#include "ArduinoJson.h"
#include "async_ota.h"
#include "bms_relay.h"
#include "data.h"
#include "settings.h"
#include "task_queue.h"



namespace {
DNSServer dnsServer;
AsyncWebServer webServer(80);
AsyncWebSocket ws("/rawdata");

const String defaultPass("****");
BmsRelay *relay;

const String owie_version = "1.4.3";

String renderPacketStatsTable() {
  String result(
      PSTR("<table><tr><th>ID</th><th>Period</th><th>Deviation</th><th>Count</"
           "th></tr>"));
  for (const IndividualPacketStat &stat :
       relay->getPacketTracker().getIndividualPacketStats()) {
    if (stat.id < 0) {
      continue;
    }
    result.concat(PSTR("<tr><td>"));

    char buffer[16];
    snprintf_P(buffer, sizeof(buffer), PSTR("%X"), stat.id);
    result.concat(buffer);

    result.concat(PSTR("</td><td>"));
    result.concat(stat.mean_period_millis());
    result.concat(PSTR("</td><td>"));
    result.concat(stat.deviation_millis());
    result.concat(PSTR("</td><td>"));
    result.concat(stat.total_num);
    result.concat(PSTR("</td></tr>"));
  }

  result.concat(PSTR(
      "<tr><th>Unknown Bytes</th><th>Checksum Mismatches</th></tr><tr><td>"));
  result.concat(
      relay->getPacketTracker().getGlobalStats().total_unknown_bytes_received);
  result.concat(PSTR("</td><td>"));
  result.concat(relay->getPacketTracker()
                    .getGlobalStats()
                    .total_packet_checksum_mismatches);
  result.concat(PSTR("</td></tr></table>"));
  return result;
}

String uptimeString() {
  const unsigned long nowSecs = millis() / 1000;
  const int hrs = nowSecs / 3600;
  const int mins = (nowSecs % 3600) / 60;
  const int secs = nowSecs % 60;
  String ret;
  if (hrs) {
    ret.concat(hrs);
    ret.concat('h');
  }
  ret.concat(mins);
  ret.concat('m');
  ret.concat(secs);
  ret.concat('s');
  return ret;
}

String getTempString() {
  const int8_t *thermTemps = relay->getTemperaturesCelsius();
  String temps;
  temps.reserve(256);
  temps.concat("<tr>");
  for (int i = 0; i < 5; i++) {
    temps.concat("<td>");
    temps.concat(thermTemps[i]);
    temps.concat("</td>");
  }
  temps.concat("<tr>");
  return temps;
}

String generateOwieStatusJson() {
  DynamicJsonDocument status(1024);
  String jsonOutput;
  const uint16_t *cellMillivolts = relay->getCellMillivolts();
  String out;
  out.reserve(256);
  for (int i = 0; i < 3; i++) {
    out.concat("<tr>");
    for (int j = 0; j < 5; j++) {
      out.concat("<td>");
      out.concat(cellMillivolts[i * 5 + j] / 1000.0);
      out.concat("</td>");
    }
    out.concat("<tr>");
  }

  status["TOTAL_VOLTAGE"] =
      String(relay->getTotalVoltageMillivolts() / 1000.0, 2);
  status["CURRENT_AMPS"] = String(relay->getCurrentInAmps(), 1);
  status["BMS_SOC"] = String(relay->getBmsReportedSOC());
  status["OVERRIDDEN_SOC"] = String(relay->getOverriddenSOC());
  status["USED_CHARGE_MAH"] = String(relay->getUsedChargeMah());
  status["REGENERATED_CHARGE_MAH"] =
      String(relay->getRegeneratedChargeMah());
  status["UPTIME"] = uptimeString();
  status["CELL_VOLTAGE_TABLE"] = out;
  status["TEMPERATURE_TABLE"] = getTempString();

  serializeJson(status, jsonOutput);
  return jsonOutput;
}

bool lockingPreconditionsMet() {
  return strlen(Settings->ap_self_password) > 0;
}
const char *lockedStatusDataAttrValue() {
  return Settings->is_locked ? "1" : "";
};

String templateProcessor(const String &var) {
  if (var == "TOTAL_VOLTAGE") {
    return String(relay->getTotalVoltageMillivolts() / 1000.0,
                  /* decimalPlaces = */ 2);
  } else if (var == "CURRENT_AMPS") {
    return String(relay->getCurrentInAmps(),
                  /* decimalPlaces = */ 1);
  } else if (var == "BMS_SOC") {
    return String(relay->getBmsReportedSOC());
  } else if (var == "OVERRIDDEN_SOC") {
    return String(relay->getOverriddenSOC());
  } else if (var == "USED_CHARGE_MAH") {
    return String(relay->getUsedChargeMah());
  } else if (var == "REGENERATED_CHARGE_MAH") {
    return String(relay->getRegeneratedChargeMah());
  } else if (var == "OWIE_version") {
    return owie_version;
  } else if (var == "SSID") {
    return Settings->ap_name;
  } else if (var == "PASS") {
    if (strlen(Settings->ap_password) > 0) {
      return defaultPass;
    }
    return "";
  } else if (var == "GRACEFUL_SHUTDOWN_COUNT") {
    return String(Settings->graceful_shutdown_count);
  } else if (var == "UPTIME") {
    return uptimeString();
  } else if (var == "IS_LOCKED") {
    return lockedStatusDataAttrValue();
  } else if (var == "CAN_ENABLE_LOCKING") {
    return lockingPreconditionsMet() ? "1" : "";
  } else if (var == "LOCKING_ENABLED") {
    return Settings->locking_enabled ? "1" : "";
  } else if (var == "PACKET_STATS_TABLE") {
    return renderPacketStatsTable();
  } else if (var == "CELL_VOLTAGE_TABLE") {
    const uint16_t *cellMillivolts = relay->getCellMillivolts();
    String out;
    out.reserve(256);
    for (int i = 0; i < 3; i++) {
      out.concat("<tr>");
      for (int j = 0; j < 5; j++) {
        out.concat("<td>");
        out.concat(cellMillivolts[i * 5 + j] / 1000.0);
        out.concat("</td>");
      }
      out.concat("<tr>");
    }
    return out;
  } else if (var == "TEMPERATURE_TABLE") {
    return getTempString();
  } else if (var == "AP_PASSWORD") {
    return Settings->ap_self_password;
  } else if (var == "AP_SELF_NAME") {
    return Settings->ap_self_name;
  } else if (var == "DISPLAY_AP_NAME") {
    char apDisplayName[64];
    if (strlen(Settings->ap_self_name) > 0) {
      snprintf(apDisplayName, sizeof(apDisplayName), "%s",
               Settings->ap_self_name);
    } else {
      snprintf(apDisplayName, sizeof(apDisplayName), "Owie-%04X",
               ESP.getChipId() & 0xFFFF);
    }
    return String(apDisplayName);
  } else if (var == "WIFI_POWER") {
    return String(Settings->wifi_power);
  } else if (var == "WIFI_POWER_OPTIONS") {
    String opts;
    opts.reserve(256);
    for (int i = 8; i < 18; i++) {
      opts.concat("<option value='");
      opts.concat(String(i));
      opts.concat("'");
      if (i == Settings->wifi_power) {
        opts.concat(" selected ");
      }
      opts.concat(">");
      opts.concat(String(i));
      opts.concat("</option>");
    }
    return opts;
  }
  return "<script>alert('UNKNOWN PLACEHOLDER')</script>";
}

}  // namespace

void setupWifi() {
	
WiFi.mode(WIFI_OFF);

}


void streamBMSPacket(uint8_t *const data, size_t len) {
  ws.binaryAll((char *const)data, len);
}
