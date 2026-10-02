#pragma once

#if !defined(LITE_VERSION)

// TSI Classroom Topography Wi-Fi Scanner for LilyGO T-Embed (ESP32-S3)
// Scans for strongest TSI / eduroam APs, assigns room numbers, and
// saves to onboard LittleFS flash and syncs with qr-relay cloud backend.
void tsi_scanner_menu();

#endif
