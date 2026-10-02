#ifndef VENDOR_RESOLVER_H
#define VENDOR_RESOLVER_H

#include <Arduino.h>
#include <NimBLEDevice.h>

// Extended OUI lookup
inline String getVendorFromMac(const String &macStr) {
    String mac = macStr;
    mac.toUpperCase();
    mac.replace(":", "");
    mac.replace("-", "");
    
    if (mac.startsWith("000393") || mac.startsWith("000502") || mac.startsWith("000A27") || 
        mac.startsWith("000D93") || mac.startsWith("0010FA") || mac.startsWith("001124") || 
        mac.startsWith("040CCE") || mac.startsWith("041552") || mac.startsWith("DCA904") || 
        mac.startsWith("F40F24") || mac.startsWith("BC9A78") || mac.startsWith("A4C3F0") ||
        mac.startsWith("F01898") || mac.startsWith("E0C97A") || mac.startsWith("D0817A")) return "Apple";
    if (mac.startsWith("0000F0") || mac.startsWith("000278") || mac.startsWith("0007AB") || 
        mac.startsWith("000918") || mac.startsWith("000DAE") || mac.startsWith("001247") || 
        mac.startsWith("0018AF") || mac.startsWith("001A8A") || mac.startsWith("001B98") ||
        mac.startsWith("842519") || mac.startsWith("88366C") || mac.startsWith("907F61")) return "Samsung";
    if (mac.startsWith("18D6C7") || mac.startsWith("50C7BF") || mac.startsWith("E894F6") || 
        mac.startsWith("EC172F") || mac.startsWith("0014D1") || mac.startsWith("001966")) return "TP-Link";
    if (mac.startsWith("000C6E") || mac.startsWith("08606E") || mac.startsWith("1831BF") || 
        mac.startsWith("2C4D54") || mac.startsWith("40163B") || mac.startsWith("50465D")) return "ASUS";
    if (mac.startsWith("000C42") || mac.startsWith("000E8E") || mac.startsWith("00156D") || 
        mac.startsWith("18FD74") || mac.startsWith("2C16DB") || mac.startsWith("B869F4")) return "MikroTik";
    if (mac.startsWith("0C8063") || mac.startsWith("180373") || mac.startsWith("349672") || 
        mac.startsWith("640980") || mac.startsWith("983F9F") || mac.startsWith("A439B3")) return "Xiaomi";
    return "";
}

// Advanced BLE Advertised Payload Resolver
inline String getBLEDeviceDetails(const NimBLEAdvertisedDevice *dev) {
    String icon = "📶";
    String vendor = "";
    String modelType = "";

    // 1. Manufacturer Data Parsing
    if (dev->haveManufacturerData()) {
        std::string manuf = dev->getManufacturerData();
        if (manuf.length() >= 2) {
            uint16_t id = (uint8_t)manuf[0] | ((uint8_t)manuf[1] << 8);

            if (id == 0x004C) { // Apple
                vendor = "Apple";
                icon = "📱";
                if (manuf.length() >= 3) {
                    uint8_t type = (uint8_t)manuf[2];
                    if (type == 0x07 || type == 0x05) { modelType = "AirPods/Beats"; icon = "🎧"; }
                    else if (type == 0x10 || type == 0x12) { modelType = "AirTag"; icon = "🏷️"; }
                    else if (type == 0x09 || type == 0x0E) { modelType = "Mac/iPhone"; icon = "💻"; }
                    else if (type == 0x0B) { modelType = "Apple Watch"; icon = "⌚"; }
                    else { modelType = "iDevice"; }
                } else {
                    modelType = "iDevice";
                }
            } else if (id == 0x0075) { // Samsung
                vendor = "Samsung";
                icon = "📱";
                if (manuf.length() >= 4) {
                    if ((uint8_t)manuf[2] == 0x42 || (uint8_t)manuf[2] == 0x09) { modelType = "Galaxy Buds"; icon = "🎧"; }
                    else if ((uint8_t)manuf[2] == 0x01) { modelType = "Galaxy Watch"; icon = "⌚"; }
                    else { modelType = "Galaxy"; }
                } else {
                    modelType = "Galaxy";
                }
            } else if (id == 0x0006) { // Microsoft
                vendor = "Microsoft";
                modelType = "Win PC";
                icon = "💻";
            } else if (id == 0x00E0 || id == 0x02E5) { // Google / ESP32
                if (id == 0x02E5) { vendor = "ESP32"; modelType = "IoT Node"; icon = "⚡"; }
                else { vendor = "Google"; modelType = "FastPair"; icon = "🎧"; }
            } else if (id == 0x012D) {
                vendor = "Sony"; modelType = "Audio"; icon = "🎧";
            } else if (id == 0x009E) {
                vendor = "Bose"; modelType = "Headphones"; icon = "🎧";
            } else if (id == 0x0087) {
                vendor = "Garmin"; modelType = "Watch"; icon = "⌚";
            } else if (id == 0x038F || id == 0x01AA) {
                vendor = "Xiaomi"; modelType = "Smart Band"; icon = "⌚";
            } else if (id == 0x007E) {
                vendor = "Nintendo"; modelType = "Controller"; icon = "🎮";
            }
        }
    }

    // 2. Service UUID Check
    if (dev->haveServiceUUID()) {
        std::string uuid = dev->getServiceUUID().toString();
        for (auto &c : uuid) c = tolower(c);

        if (uuid.find("fe2c") != std::string::npos || uuid.find("fe9f") != std::string::npos) {
            icon = "🎧";
            if (modelType.isEmpty()) modelType = "FastPair Audio";
        } else if (uuid.find("110b") != std::string::npos || uuid.find("110e") != std::string::npos || uuid.find("110f") != std::string::npos || uuid.find("110a") != std::string::npos) {
            icon = "🎧";
            if (modelType.isEmpty()) modelType = "BT Headset";
        } else if (uuid.find("1812") != std::string::npos) {
            icon = "⌨️";
            if (modelType.isEmpty()) modelType = "Keyboard/Mouse";
        } else if (uuid.find("180d") != std::string::npos || uuid.find("1810") != std::string::npos) {
            icon = "⌚";
            if (modelType.isEmpty()) modelType = "Fitness Tracker";
        }
    }

    String fullMac = dev->getAddress().toString().c_str();
    String shortMac = fullMac.length() >= 5 ? fullMac.substring(fullMac.length() - 5) : fullMac;

    if (vendor.isEmpty()) {
        vendor = getVendorFromMac(fullMac);
    }

    String rawName = dev->getName().c_str();
    String result = "";

    // PRIORITY 1: Explicit Device Name
    if (!rawName.isEmpty() && rawName != "(null)" && rawName != "null" && rawName != "<no name>") {
        result = icon + " " + rawName;
        if (!vendor.isEmpty() && !rawName.startsWith(vendor)) {
            result += " [" + vendor + "]";
        }
    } 
    // PRIORITY 2: Resolved Model / Type / Vendor
    else if (!modelType.isEmpty() || !vendor.isEmpty()) {
        String label = !modelType.isEmpty() ? modelType : vendor;
        if (!vendor.isEmpty() && !label.startsWith(vendor)) {
            label = vendor + " " + label;
        }
        result = icon + " " + label + " (" + shortMac + ")";
    } 
    // PRIORITY 3: Truncated MAC when no payload signature matches
    else {
        result = icon + " BT-Dev (" + shortMac + ")";
    }

    // Truncate to 26 characters for T-Embed 320px TFT
    if (result.length() > 26) {
        result = result.substring(0, 23) + "..";
    }
    return result;
}

#endif // VENDOR_RESOLVER_H
