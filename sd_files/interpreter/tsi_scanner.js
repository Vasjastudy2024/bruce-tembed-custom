// ============================================================================
// TSI-Scanner for LilyGO T-Embed CC1101 (Bruce Firmware JS Interpreter)
// High-Precision Auditorium Wi-Fi Topography Hunter & Cloud Sync
// ============================================================================

var display = require('display');
var keyboard = require('keyboard');
var wifi = require('wifi');
var dialog = require('dialog');
var storage = require('storage');
var led = require('led');

var screenWidth = display.width();
var screenHeight = display.height();

// UI Palette
var COLOR_BG = display.color(12, 14, 22);
var COLOR_CARD = display.color(24, 28, 44);
var COLOR_CYAN = display.color(0, 220, 255);
var COLOR_GREEN = display.color(0, 255, 130);
var COLOR_YELLOW = display.color(255, 215, 0);
var COLOR_ORANGE = display.color(255, 120, 0);
var COLOR_RED = display.color(255, 60, 60);
var COLOR_WHITE = display.color(255, 255, 255);
var COLOR_GRAY = display.color(120, 130, 150);

var CLOUD_API_URL = "https://qr-relay-sable.vercel.app/api/learn_room";
var TSI_LAT = 56.9385;
var TSI_LON = 24.1595;

function setLedColor(r, g, b) {
  try {
    if (led && led.setColor) {
      led.setColor(r, g, b);
    }
  } catch(e) {}
}

function getRssiColor(rssi) {
  if (rssi >= -55) return COLOR_GREEN;
  if (rssi >= -68) return COLOR_YELLOW;
  if (rssi >= -78) return COLOR_ORANGE;
  return COLOR_RED;
}

function drawHeader(title) {
  display.drawFillRect(0, 0, screenWidth, 24, display.color(0, 90, 160));
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(2);
  display.drawString(title, 10, 4);
}

function drawScanningScreen(statusText) {
  display.drawFillRect(0, 0, screenWidth, screenHeight, COLOR_BG);
  display.drawRoundRect(4, 4, screenWidth - 8, screenHeight - 8, display.color(0, 140, 230));

  drawHeader("TSI-SCANNER");

  display.setTextColor(COLOR_WHITE);
  display.setTextSize(2);
  display.drawString("LilyGO T-Embed", 20, 40);

  display.setTextColor(COLOR_CYAN);
  display.setTextSize(1);
  display.drawString(statusText, 20, 75);

  // Signal searching bar animation
  display.drawRect(20, 105, screenWidth - 40, 14, COLOR_GRAY);
  display.drawFillRect(22, 107, screenWidth - 44, 10, display.color(0, 160, 220));

  display.setTextColor(COLOR_GRAY);
  display.drawString("Scanning 2.4 GHz channels 1-13...", 20, 130);
}

function drawCard(bestAp, roomName) {
  display.drawFillRect(0, 0, screenWidth, screenHeight, COLOR_BG);
  display.drawRoundRect(4, 4, screenWidth - 8, screenHeight - 8, display.color(0, 140, 230));

  drawHeader("AUDITORIUM AP FOUND");

  // Box 1: AP details
  display.drawFillRoundRect(10, 32, screenWidth - 20, 95, COLOR_CARD);
  display.drawRoundRect(10, 32, screenWidth - 20, 95, display.color(40, 50, 80));

  display.setTextColor(COLOR_WHITE);
  display.setTextSize(1);
  display.drawString("SSID: " + bestAp.SSID, 20, 42);

  display.setTextColor(COLOR_YELLOW);
  display.drawString("BSSID: " + bestAp.MAC, 20, 60);

  display.setTextColor(COLOR_WHITE);
  display.drawString("Signal: " + bestAp.RSSI + " dBm  (Ch " + (bestAp.channel || "?") + ")", 20, 78);

  // RSSI bar meter
  var barX = 20;
  var barY = 96;
  var barW = screenWidth - 60;
  var barH = 10;
  display.drawRect(barX, barY, barW, barH, COLOR_WHITE);
  var pct = (bestAp.RSSI + 100) / 70; // -100 to -30
  if (pct < 0.05) pct = 0.05;
  if (pct > 1) pct = 1;
  var fillW = Math.floor((barW - 4) * pct);
  display.drawFillRect(barX + 2, barY + 2, fillW, barH - 4, getRssiColor(bestAp.RSSI));

  // Footer instructions
  display.setTextColor(COLOR_GREEN);
  display.setTextSize(1);
  display.drawString("[ Click Wheel / SEL to Assign Room ]", 20, 140);
}

function saveLocalDatabase(room, ap) {
  var db = {};
  try {
    var raw = storage.read("/tsi_rooms.json");
    if (raw) {
      db = JSON.parse(raw);
    }
  } catch(e) {
    db = {};
  }

  db[room] = {
    bssid: ap.MAC,
    ssid: ap.SSID,
    rssi: ap.RSSI,
    channel: ap.channel || null,
    lat: TSI_LAT,
    lon: TSI_LON,
    device: "LilyGO T-Embed CC1101",
    updatedAt: new Date().toISOString()
  };

  var jsonStr = JSON.stringify(db);
  storage.write("/tsi_rooms.json", jsonStr, "write");

  // Also append to CSV log
  var csvLine = room + ",\"" + ap.SSID + "\"," + ap.MAC + "," + ap.RSSI + "," + (ap.channel || "") + "," + TSI_LAT + "," + TSI_LON + "\n";
  storage.write("/tsi_rooms.csv", csvLine, "append");
  return true;
}

function syncCloudBackend(room, ap) {
  if (!wifi.connected()) {
    dialog.info("Connect to Wi-Fi to sync...");
    wifi.connectDialog();
  }

  if (!wifi.connected()) {
    dialog.error("Wi-Fi not connected!");
    return false;
  }

  dialog.info("Sending to qr-relay Cloud...");
  setLedColor(0, 0, 255); // Blue during sync

  var payload = {
    action: "learn_room",
    room: room,
    bssid: ap.MAC,
    ssid: ap.SSID,
    rssi: ap.RSSI,
    channel: ap.channel || null,
    lat: TSI_LAT,
    lon: TSI_LON,
    device: "LilyGO T-Embed CC1101"
  };

  try {
    var res = wifi.httpFetch(CLOUD_API_URL, {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(payload)
    });

    if (res && res.status >= 200 && res.status < 300) {
      setLedColor(0, 255, 0); // Green on success
      return true;
    } else {
      setLedColor(255, 0, 0);
      dialog.error("Sync Error: HTTP " + (res ? res.status : "null"));
      return false;
    }
  } catch(e) {
    setLedColor(255, 0, 0);
    dialog.error("Fetch Exception: " + e);
    return false;
  }
}

// ----------------------------------------------------------------------------
// Main Application Loop
// ----------------------------------------------------------------------------
function main() {
  while (true) {
    setLedColor(0, 220, 255); // Cyan LED ring
    drawScanningScreen("Scanning Wi-Fi in auditorium...");

    var networks = wifi.scan();
    delay(200);

    if (!networks || networks.length === 0) {
      setLedColor(255, 0, 0);
      dialog.error("No Wi-Fi Networks Found!");
      break;
    }

    // Filter and sort for TSI / eduroam networks
    var targetNets = [];
    var allNets = [];

    for (var i = 0; i < networks.length; i++) {
      var net = networks[i];
      allNets.push(net);

      var lowerSsid = (net.SSID || "").toLowerCase();
      if (lowerSsid.indexOf("tsi") >= 0 ||
          lowerSsid.indexOf("eduroam") >= 0 ||
          lowerSsid.indexOf("student") >= 0 ||
          lowerSsid.indexOf("transport") >= 0) {
        targetNets.push(net);
      }
    }

    // Sort by RSSI (closest / highest signal first)
    var sortFn = function(a, b) { return b.RSSI - a.RSSI; };
    targetNets.sort(sortFn);
    allNets.sort(sortFn);

    var bestAp = (targetNets.length > 0) ? targetNets[0] : allNets[0];

    setLedColor(0, 255, 120); // Green locked LED
    drawCard(bestAp, "");

    // Wait for Encoder Wheel Click (SEL) or ESC
    var exitLoop = false;
    while (true) {
      if (keyboard.getEscPress()) {
        exitLoop = true;
        break;
      }
      if (keyboard.getSelPress()) {
        break;
      }
      delay(50);
    }

    if (exitLoop) break;

    // Room Selection Menu
    var roomOptions = {};
    roomOptions["Room 220 (Main Lab)"] = "220";
    roomOptions["Room 222"] = "222";
    roomOptions["Room 224"] = "224";
    roomOptions["Room 226"] = "226";
    roomOptions["Room 201"] = "201";
    roomOptions["Room 203"] = "203";
    roomOptions["Room 218"] = "218";
    roomOptions["Room 100"] = "100";
    roomOptions["Room 110"] = "110";
    roomOptions["Room 114"] = "114";
    roomOptions["Room 117"] = "117";
    roomOptions["Room 130"] = "130";
    roomOptions["Room 303"] = "303";
    roomOptions["Room 305"] = "305";
    roomOptions["Room 307"] = "307";
    roomOptions["Room 310"] = "310";
    roomOptions["Room 314"] = "314";
    roomOptions["Room 316"] = "316";
    roomOptions["Room 401"] = "401";
    roomOptions["Room 404"] = "404";
    roomOptions["Room 407"] = "407";
    roomOptions["Room I"] = "I";
    roomOptions["Room II"] = "II";
    roomOptions["Room III"] = "III";
    roomOptions["[ Enter Custom Room ]"] = "CUSTOM";

    var selectedRoom = dialog.choice(roomOptions);
    if (!selectedRoom || selectedRoom === "") {
      continue;
    }

    if (selectedRoom === "CUSTOM") {
      selectedRoom = keyboard.numKeyboard("Auditorium Number:", 8, "220");
      if (!selectedRoom || selectedRoom === "") {
        continue;
      }
    }

    selectedRoom = selectedRoom.toUpperCase().trim();

    // Action Menu: Save locally vs Save & Cloud Sync
    var actionOptions = {};
    actionOptions["1. Save & Sync Cloud (Flash + Vercel)"] = "sync";
    actionOptions["2. Save Flash Only (/tsi_rooms.json)"] = "save_only";
    actionOptions["3. Rescan Wi-Fi"] = "rescan";
    actionOptions["4. Cancel / Exit"] = "cancel";

    var action = dialog.choice(actionOptions);

    if (action === "sync") {
      saveLocalDatabase(selectedRoom, bestAp);
      var cloudSuccess = syncCloudBackend(selectedRoom, bestAp);
      if (cloudSuccess) {
        dialog.success("Saved & Synced!\nTelegram Alert Sent ✅", true);
      } else {
        dialog.warning("Saved to Flash!\n(Cloud offline)", true);
      }
    } else if (action === "save_only") {
      saveLocalDatabase(selectedRoom, bestAp);
      setLedColor(0, 255, 0);
      dialog.success("Saved to /tsi_rooms.json!\nRoom " + selectedRoom + " locked.", true);
    } else if (action === "cancel") {
      break;
    }
  }

  setLedColor(0, 0, 0);
  display.fillScreen(0);
}

main();
