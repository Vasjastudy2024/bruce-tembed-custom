#if !defined(LITE_VERSION)
#include "tsi_scanner.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/settings.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <globals.h>
#include <vector>
#include <map>
#include <algorithm>

struct ApScanResult {
    String ssid;
    String bssid;
    int32_t rssi;
    int32_t channel;
    bool isTsi;
    String vendor;
    int qualityPct;
};

static const char *TSI_API_URL = "https://qr-relay-sable.vercel.app/api/learn_room";
static const double TSI_LAT = 56.9385;
static const double TSI_LON = 24.1595;

// Recognize enterprise AP vendor from MAC address (OUI)
static String getVendorFromMac(const String &mac) {
    if (mac.length() < 8) return "Generic AP";
    String prefix = mac.substring(0, 8);
    prefix.toLowerCase();

    if (prefix.startsWith("c0:06:c3") || prefix.startsWith("00:1d:a2") ||
        prefix.startsWith("00:24:6c") || prefix.startsWith("58:97:bd") ||
        prefix.startsWith("00:18:0a") || prefix.startsWith("00:22:90") ||
        prefix.startsWith("00:26:99")) {
        return "Cisco Systems";
    }
    if (prefix.startsWith("00:0b:86") || prefix.startsWith("d8:c7:c8") ||
        prefix.startsWith("24:de:c6") || prefix.startsWith("94:b4:0f") ||
        prefix.startsWith("d4:6d:50") || prefix.startsWith("90:4c:81")) {
        return "Aruba Networks";
    }
    if (prefix.startsWith("48:8f:5a") || prefix.startsWith("b8:69:f4") ||
        prefix.startsWith("74:4d:28") || prefix.startsWith("08:55:31") ||
        prefix.startsWith("dc:2c:6e")) {
        return "Ubiquiti UniFi";
    }
    if (prefix.startsWith("6c:3b:6b") || prefix.startsWith("cc:2d:e0") ||
        prefix.startsWith("48:8d:36") || prefix.startsWith("2c:c8:1b")) {
        return "MikroTik RouterOS";
    }
    if (prefix.startsWith("00:e0:fc") || prefix.startsWith("70:72:cf") ||
        prefix.startsWith("e4:68:a3") || prefix.startsWith("48:43:5a")) {
        return "Huawei Enterprise";
    }
    if (prefix.startsWith("58:8d:09") || prefix.startsWith("5c:e2:8c") ||
        prefix.startsWith("70:4f:57") || prefix.startsWith("98:48:27")) {
        return "TP-Link Enterprise";
    }
    if (prefix.startsWith("00:1f:33") || prefix.startsWith("00:19:92") ||
        prefix.startsWith("00:17:c5")) {
        return "Netgear Enterprise";
    }
    if (prefix.startsWith("e0:22:03") || prefix.startsWith("e4:aa:5d")) {
        return "Ruckus Wireless";
    }
    return "Institutional AP";
}

// Calculate signal quality percentage from RSSI
static int calculateQuality(int rssi) {
    if (rssi >= -50) return 100;
    if (rssi <= -100) return 0;
    return constrain((int)(2 * (rssi + 100)), 5, 100);
}

// Color helper for RSSI signal bar
static uint16_t getRssiColor(int rssi) {
    if (rssi >= -55) return TFT_GREEN;
    if (rssi >= -68) return 0x97E0; // Yellow-Green
    if (rssi >= -78) return TFT_ORANGE;
    return TFT_RED;
}

// Draw stylized scanning screen with multi-pass progress
static void drawScanningBanner(const String &subtitle, int pass, int totalPasses) {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawRoundRect(4, 4, tftWidth - 8, tftHeight - 8, 6, bruceConfig.priColor);
    
    // Header
    tft.setTextSize(FM);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawCentreString("TSI-SCANNER ULTRA", tftWidth / 2, 12, 1);

    tft.setTextSize(FP);
    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawCentreString("Triple-Sweep Cluster Hunter", tftWidth / 2, 34, 1);

    // Dynamic message
    tft.setTextSize(FM);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.drawCentreString(subtitle, tftWidth / 2, 68, 1);

    // Multi-pass visual bar
    int barX = 30;
    int barY = 100;
    int barW = tftWidth - 60;
    int barH = 12;
    tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
    int stepW = (barW - 4) * pass / totalPasses;
    tft.fillRect(barX + 2, barY + 2, stepW, barH - 4, TFT_CYAN);

    tft.setTextSize(FP);
    tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
    tft.setCursor(30, 122);
    tft.printf("Pass %d of %d: Scanning Euro Ch 1-13", pass, totalPasses);
}

// Draw survey result card for best AP and cluster
static void drawSurveyCard(const ApScanResult &ap, const std::vector<ApScanResult> &backups, const String &storageTarget) {
    tft.fillScreen(bruceConfig.bgColor);
    tft.drawRoundRect(4, 4, tftWidth - 8, tftHeight - 8, 6, bruceConfig.priColor);

    // Title banner
    tft.fillRect(5, 5, tftWidth - 10, 22, bruceConfig.priColor);
    tft.setTextSize(FM);
    tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);
    tft.drawCentreString("BEST AUDITORIUM AP", tftWidth / 2, 8, 1);

    // SSID & Vendor
    tft.setTextSize(FP);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.setCursor(12, 34);
    tft.printf("SSID: %s  [%s]", ap.ssid.substring(0, 14).c_str(), ap.vendor.c_str());

    // BSSID
    tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
    tft.setCursor(12, 48);
    tft.printf("BSSID: %s", ap.bssid.c_str());

    // Signal & Quality
    tft.setCursor(12, 62);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.printf("Sig: %d dBm (Q: %d%%) Ch: %d", ap.rssi, ap.qualityPct, ap.channel);

    // Signal bar
    int barX = 12;
    int barY = 76;
    int barW = tftWidth - 24;
    int barH = 10;
    tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
    int fillW = constrain(map(ap.rssi, -95, -35, 2, barW - 4), 2, barW - 4);
    tft.fillRect(barX + 2, barY + 2, fillW, barH - 4, getRssiColor(ap.rssi));

    // Cluster Backup APs info
    tft.setCursor(12, 94);
    tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
    tft.printf("Cluster Backups (%d APs):", (int)backups.size());

    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    for (size_t b = 0; b < backups.size() && b < 2; ++b) {
        tft.setCursor(12, 108 + (b * 12));
        tft.printf("#%d: %s (%d dBm, Ch %d)", 
            (int)b + 1, backups[b].bssid.c_str(), backups[b].rssi, backups[b].channel);
    }

    // Action line
    tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
    tft.drawCentreString("[ Press Wheel / SEL to Assign Room ]", tftWidth / 2, 146, 1);
}

// Save room topography to LittleFS / SD storage in JSON & CSV
static bool saveToLocalStorage(FS *fs, const String &room, const ApScanResult &ap, const std::vector<ApScanResult> &backups) {
    if (!fs) return false;

    // 1. Update /tsi_rooms.json
    JsonDocument doc;
    if (fs->exists("/tsi_rooms.json")) {
        File f = fs->open("/tsi_rooms.json", FILE_READ);
        if (f) {
            deserializeJson(doc, f);
            f.close();
        }
    }

    JsonObject roomObj = doc[room].to<JsonObject>();
    roomObj["bssid"] = ap.bssid;
    roomObj["ssid"] = ap.ssid;
    roomObj["vendor"] = ap.vendor;
    roomObj["rssi"] = ap.rssi;
    roomObj["quality"] = ap.qualityPct;
    roomObj["channel"] = ap.channel;
    roomObj["lat"] = TSI_LAT;
    roomObj["lon"] = TSI_LON;
    roomObj["device"] = "LilyGO T-Embed CC1101";

    JsonArray backupArr = roomObj["backups"].to<JsonArray>();
    for (const auto &bk : backups) {
        JsonObject bObj = backupArr.add<JsonObject>();
        bObj["bssid"] = bk.bssid;
        bObj["ssid"] = bk.ssid;
        bObj["rssi"] = bk.rssi;
        bObj["channel"] = bk.channel;
    }

    File outFile = fs->open("/tsi_rooms.json", FILE_WRITE);
    if (outFile) {
        serializeJson(doc, outFile);
        outFile.close();
    } else {
        return false;
    }

    // 2. Append to /tsi_rooms.csv
    bool exists = fs->exists("/tsi_rooms.csv");
    File csvFile = fs->open("/tsi_rooms.csv", FILE_APPEND);
    if (csvFile) {
        if (!exists) {
            csvFile.println("Room,SSID,BSSID,Vendor,RSSI,Quality,Channel,Lat,Lon");
        }
        csvFile.printf("%s,\"%s\",%s,\"%s\",%d,%d,%d,%.4f,%.4f\n",
            room.c_str(), ap.ssid.c_str(), ap.bssid.c_str(), ap.vendor.c_str(),
            ap.rssi, ap.qualityPct, ap.channel, TSI_LAT, TSI_LON);
        csvFile.close();
    }

    return true;
}

// Sync room topography to Vercel cloud over WiFi
static bool syncToCloudBackend(const String &room, const ApScanResult &ap, const std::vector<ApScanResult> &backups) {
    if (!WiFi.isConnected()) {
        displayInfo("Connecting to WiFi...", false);
        wifiConnectMenu();
    }

    if (!WiFi.isConnected()) {
        displayError("WiFi Not Connected!", true);
        return false;
    }

    displayInfo("Uploading to Cloud...", false);

#ifdef HAS_RGB_LED
    bruceConfig.setLedColor(0x0000FF); // Blue while syncing
#endif

    WiFiClientSecure client;
    client.setInsecure(); // Skip TLS validation for serverless endpoint

    HTTPClient https;
    https.setTimeout(15000);

    if (!https.begin(client, TSI_API_URL)) {
        displayError("HTTPS Init Failed", true);
        return false;
    }

    https.addHeader("Content-Type", "application/json");

    JsonDocument reqDoc;
    reqDoc["action"] = "learn_room";
    reqDoc["room"] = room;
    reqDoc["bssid"] = ap.bssid;
    reqDoc["ssid"] = ap.ssid;
    reqDoc["vendor"] = ap.vendor;
    reqDoc["rssi"] = ap.rssi;
    reqDoc["quality"] = ap.qualityPct;
    reqDoc["channel"] = ap.channel;
    reqDoc["lat"] = TSI_LAT;
    reqDoc["lon"] = TSI_LON;
    reqDoc["device"] = "LilyGO T-Embed CC1101";

    JsonArray backupArr = reqDoc["backups"].to<JsonArray>();
    for (const auto &bk : backups) {
        JsonObject bObj = backupArr.add<JsonObject>();
        bObj["bssid"] = bk.bssid;
        bObj["ssid"] = bk.ssid;
        bObj["rssi"] = bk.rssi;
        bObj["channel"] = bk.channel;
    }

    String jsonPayload;
    serializeJson(reqDoc, jsonPayload);

    int httpResponseCode = https.POST(jsonPayload);
    bool success = (httpResponseCode >= 200 && httpResponseCode < 300);

    https.end();

#ifdef HAS_RGB_LED
    bruceConfig.setLedColor(success ? 0x00FF00 : 0xFF0000);
#endif

    return success;
}

// Main Menu Entry Point
void tsi_scanner_menu() {
    returnToMenu = false;

    FS *fs = nullptr;
    bool hasStorage = getFsStorage(fs);
    String storageName = (sdcardMounted && fs == &SD) ? "SD Card" : "Flash (LittleFS)";

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    // --- TRIPLE-SWEEP MULTI-PASS RF SCAN ---
    const int TOTAL_PASSES = 3;
    std::map<String, std::pair<ApScanResult, std::vector<int32_t>>> bssidMap;

    for (int pass = 1; pass <= TOTAL_PASSES; ++pass) {
#ifdef HAS_RGB_LED
        bruceConfig.setLedColor((pass % 2 == 0) ? 0x00FFFF : 0x0088FF);
#endif
        drawScanningBanner("Triple-Sweep Scanning...", pass, TOTAL_PASSES);

        int netCount = WiFi.scanNetworks(false, true, false, 120); // 120ms dwell per channel
        if (netCount > 0) {
            for (int i = 0; i < netCount; ++i) {
                String bssid = WiFi.BSSIDstr(i);
                bssid.toLowerCase();
                String ssid = WiFi.SSID(i);
                int32_t rssi = WiFi.RSSI(i);
                int32_t channel = WiFi.channel(i);

                String lowerSsid = ssid;
                lowerSsid.toLowerCase();
                bool isTsi = (lowerSsid.indexOf("tsi") >= 0 ||
                              lowerSsid.indexOf("eduroam") >= 0 ||
                              lowerSsid.indexOf("student") >= 0 ||
                              lowerSsid.indexOf("transport") >= 0);

                if (bssidMap.find(bssid) == bssidMap.end()) {
                    ApScanResult ap;
                    ap.ssid = ssid;
                    ap.bssid = bssid;
                    ap.rssi = rssi;
                    ap.channel = channel;
                    ap.isTsi = isTsi;
                    ap.vendor = getVendorFromMac(bssid);
                    ap.qualityPct = calculateQuality(rssi);

                    bssidMap[bssid] = {ap, {rssi}};
                } else {
                    bssidMap[bssid].second.push_back(rssi);
                }
            }
        }
        WiFi.scanDelete();
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    if (bssidMap.empty()) {
#ifdef HAS_RGB_LED
        bruceConfig.setLedColor(0);
#endif
        displayError("No Networks Detected!", true);
        return;
    }

    // Average RSSI per BSSID
    std::vector<ApScanResult> apList;
    apList.reserve(bssidMap.size());

    for (auto &pair : bssidMap) {
        ApScanResult ap = pair.second.first;
        const auto &rssiVec = pair.second.second;
        int32_t sum = 0;
        for (int32_t r : rssiVec) sum += r;
        ap.rssi = sum / rssiVec.size();
        ap.qualityPct = calculateQuality(ap.rssi);
        apList.push_back(ap);
    }

    // Sort: TSI networks first, then descending by RSSI
    std::sort(apList.begin(), apList.end(), [](const ApScanResult &a, const ApScanResult &b) {
        if (a.isTsi != b.isTsi) return a.isTsi > b.isTsi;
        return a.rssi > b.rssi;
    });

    ApScanResult bestAp = apList[0];

    // Collect top 2-3 cluster backup APs
    std::vector<ApScanResult> backups;
    for (size_t i = 1; i < apList.size() && backups.size() < 2; ++i) {
        if (apList[i].bssid != bestAp.bssid) {
            backups.push_back(apList[i]);
        }
    }

#ifdef HAS_RGB_LED
    bruceConfig.setLedColor(0x00FF00); // Green when locked
#endif

    // Show best AP details card
    drawSurveyCard(bestAp, backups, storageName);

    // Wait for wheel press or Esc
    while (!check(SelPress)) {
        if (check(EscPress) || returnToMenu) {
#ifdef HAS_RGB_LED
            bruceConfig.setLedColor(0);
#endif
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Auditorium Selection Menu
    String selectedRoom = "";
    std::vector<Option> roomOptions = {
        {"Room 220 (Main Lab)", [&]() { selectedRoom = "220"; }},
        {"Room 222",            [&]() { selectedRoom = "222"; }},
        {"Room 224",            [&]() { selectedRoom = "224"; }},
        {"Room 226",            [&]() { selectedRoom = "226"; }},
        {"Room 201",            [&]() { selectedRoom = "201"; }},
        {"Room 203",            [&]() { selectedRoom = "203"; }},
        {"Room 218",            [&]() { selectedRoom = "218"; }},
        {"Room 100",            [&]() { selectedRoom = "100"; }},
        {"Room 110",            [&]() { selectedRoom = "110"; }},
        {"Room 114",            [&]() { selectedRoom = "114"; }},
        {"Room 117",            [&]() { selectedRoom = "117"; }},
        {"Room 130",            [&]() { selectedRoom = "130"; }},
        {"Room 303",            [&]() { selectedRoom = "303"; }},
        {"Room 305",            [&]() { selectedRoom = "305"; }},
        {"Room 307",            [&]() { selectedRoom = "307"; }},
        {"Room 310",            [&]() { selectedRoom = "310"; }},
        {"Room 314",            [&]() { selectedRoom = "314"; }},
        {"Room 316",            [&]() { selectedRoom = "316"; }},
        {"Room 401",            [&]() { selectedRoom = "401"; }},
        {"Room 404",            [&]() { selectedRoom = "404"; }},
        {"Room 407",            [&]() { selectedRoom = "407"; }},
        {"Room I",              [&]() { selectedRoom = "I"; }},
        {"Room II",             [&]() { selectedRoom = "II"; }},
        {"Room III",            [&]() { selectedRoom = "III"; }},
        {"[ Enter Custom Room ]", [&]() {
            selectedRoom = num_keyboard("220", 8, "Auditorium Number:");
        }}
    };

    loopOptions(roomOptions, MENU_TYPE_REGULAR, "Select Room", 0, true);

    if (selectedRoom.isEmpty() || selectedRoom == "\x1B") {
#ifdef HAS_RGB_LED
        bruceConfig.setLedColor(0);
#endif
        return;
    }

    selectedRoom.toUpperCase();

    // Action Menu: Save locally vs Save & Cloud Sync
    int actionChoice = 0;
    std::vector<Option> actionOptions = {
        {"1. Save & Sync Cloud", [&]() { actionChoice = 1; }},
        {"2. Save Flash Only",   [&]() { actionChoice = 2; }},
        {"3. Cancel",            [&]() { actionChoice = 3; }}
    };

    String actionTitle = "Room " + selectedRoom + " -> " + bestAp.bssid.substring(9);
    loopOptions(actionOptions, MENU_TYPE_REGULAR, actionTitle.c_str(), 0, true);

    if (actionChoice == 1 || actionChoice == 2) {
        // Save locally to LittleFS (or SD)
        bool localOk = false;
        if (hasStorage) {
            localOk = saveToLocalStorage(fs, selectedRoom, bestAp, backups);
        }

        if (actionChoice == 1) {
            // Save & Cloud Sync
            bool cloudOk = syncToCloudBackend(selectedRoom, bestAp, backups);
            if (cloudOk) {
                displaySuccess("Cloud Synced & Saved! [Tg Alert Sent]", true);
            } else if (localOk) {
                displaySuccess("Saved to Flash! (Cloud offline)", true);
            } else {
                displayError("Sync & Save Failed!", true);
            }
        } else {
            // Local Only
            if (localOk) {
                displaySuccess("Saved to " + storageName + "!", true);
            } else {
                displayError("Flash Write Error!", true);
            }
        }
    }

#ifdef HAS_RGB_LED
    bruceConfig.setLedColor(0);
#endif
}

#endif
