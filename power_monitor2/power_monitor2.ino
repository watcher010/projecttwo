#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <vector>

// WiFi credentials for AP mode
const char* ap_ssid = "PowerMonitor";
const char* ap_password = "12345678";

// WebSocket server configuration
const int wsPort = 8765;
WebSocketsServer webSocket = WebSocketsServer(wsPort);

// PZEM sensor
PZEM004Tv30 pzem(Serial2, 16, 17);  // RX, TX pins for PZEM-004T

// Room class definition
class EnergyRoom {
private:
    String id;
    uint8_t measSSRPin;      // Pin for PZEM reading relay
    uint8_t cutoffRelayPin;  // Pin for power cutoff relay
    float currentPower = -1;
    float lastValidPower = 0;
    unsigned long lastValidRead = 0;
    bool faultFlag = false;

public:
    String name;
    float threshold;
    bool isActive;

    EnergyRoom(String id, String name, uint8_t measPin, uint8_t cutoffPin, float threshold = 2500.0)
        : id(id), name(name), measSSRPin(measPin), cutoffRelayPin(cutoffPin), 
          threshold(threshold), isActive(true) {
        
        pinMode(measSSRPin, OUTPUT);
        pinMode(cutoffRelayPin, OUTPUT);
        digitalWrite(measSSRPin, LOW);
        digitalWrite(cutoffRelayPin, LOW);
    }

    String getId() const { return id; }
    uint8_t getMeasPin() const { return measSSRPin; }
    uint8_t getCutoffPin() const { return cutoffRelayPin; }

    void measure() {
        digitalWrite(measSSRPin, HIGH);
        delay(300);
        float newPower = pzem.power();
        digitalWrite(measSSRPin, LOW);

        if(!isnan(newPower)) {
            currentPower = newPower;
            lastValidPower = newPower;
            lastValidRead = millis();
            
            if(currentPower > threshold) {
                digitalWrite(cutoffRelayPin, HIGH);
            }
        }
        
        faultFlag = (digitalRead(cutoffRelayPin) == HIGH) && (lastValidPower > 10.0);
    }

    float getCurrentPower() const { return currentPower; }
    float getLastValidPower() const { return lastValidPower; }
    float getDisplayPower() const { return (currentPower >= 0) ? currentPower : lastValidPower; }
    bool hasFault() const { return faultFlag; }
    bool isPowerCutoff() const { return digitalRead(cutoffRelayPin) == HIGH; }
    unsigned long getLastUpdate() const { return lastValidRead; }

    void updateThreshold(float newThreshold) {
        threshold = constrain(newThreshold, 100.0, 10000.0);
    }

    void resetPower() {
        digitalWrite(cutoffRelayPin, LOW);
        faultFlag = false;
    }

    JsonObject toJson(JsonDocument& doc) {
        JsonObject obj = doc.createNestedObject();
        obj["id"] = id;
        obj["name"] = name;
        obj["display_power"] = getDisplayPower();
        obj["current_power"] = getCurrentPower();
        obj["threshold"] = threshold;
        obj["isCutoff"] = isPowerCutoff();
        obj["bypassDetected"] = hasFault();
        obj["lastActiveTime"] = getLastUpdate();
        obj["measPin"] = measSSRPin;
        obj["cutoffPin"] = cutoffRelayPin;
        return obj;
    }
};

std::vector<EnergyRoom> rooms;
unsigned long lastMeasure = 0;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
                Serial.printf("[%u] Connected!\n", num);
                // Send initial room data
                DynamicJsonDocument doc(1024);
                JsonArray array = doc.to<JsonArray>();
                for(auto& room : rooms) {
                    room.toJson(doc);
                }
                String response;
                serializeJson(doc, response);
                webSocket.sendTXT(num, response);
            }
            break;
        case WStype_TEXT:
            handleWebSocketMessage(num, payload, length);
            break;
    }
}

void handleWebSocketMessage(uint8_t num, uint8_t * payload, size_t length) {
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error) {
        Serial.println("JSON parsing failed!");
        return;
    }
    
    const char* action = doc["action"];
    const char* room_id = doc["room_id"];
    
    if (strcmp(action, "add") == 0) {
        if (rooms.size() >= 8) {
            sendError(num, "Maximum number of rooms reached");
            return;
        }
        
        String id = doc["room_id"] | String(random(100000, 999999));
        const char* name = doc["name"];
        uint8_t measPin = doc["meas_pin"] | 25;
        uint8_t cutoffPin = doc["cutoff_pin"] | 26;
        float threshold = doc["threshold"] | 2500.0;
        
        rooms.emplace_back(id, name, measPin, cutoffPin, threshold);
        sendSuccess(num, "Room added successfully");
    }
    else if (strcmp(action, "remove") == 0) {
        auto it = std::remove_if(rooms.begin(), rooms.end(),
            [room_id](const EnergyRoom& r){ return r.getId() == room_id; });
        
        if (it != rooms.end()) {
            digitalWrite(it->getCutoffPin(), LOW);
            rooms.erase(it, rooms.end());
            sendSuccess(num, "Room removed successfully");
        } else {
            sendError(num, "Room not found");
        }
    }
    else if (strcmp(action, "update") == 0) {
        for (auto& room : rooms) {
            if (room.getId() == room_id) {
                room.updateThreshold(doc["threshold"]);
                sendSuccess(num, "Threshold updated successfully");
                return;
            }
        }
        sendError(num, "Room not found");
    }
    else if (strcmp(action, "reconnect") == 0) {
        for (auto& room : rooms) {
            if (room.getId() == room_id) {
                room.resetPower();
                sendSuccess(num, "Power restored successfully");
                return;
            }
        }
        sendError(num, "Room not found");
    }
}

void sendSuccess(uint8_t num, const char* message) {
    DynamicJsonDocument doc(128);
    doc["status"] = "success";
    doc["message"] = message;
    String response;
    serializeJson(doc, response);
    webSocket.sendTXT(num, response);
}

void sendError(uint8_t num, const char* message) {
    DynamicJsonDocument doc(128);
    doc["status"] = "error";
    doc["message"] = message;
    String response;
    serializeJson(doc, response);
    webSocket.sendTXT(num, response);
}

void sendRoomData() {
    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();
    
    for(auto& room : rooms) {
        room.toJson(doc);
    }
    
    String response;
    serializeJson(doc, response);
    webSocket.broadcastTXT(response);
}

void setup() {
    Serial.begin(115200);
    
    // Setup WiFi AP
    WiFi.softAP(ap_ssid, ap_password);
    Serial.println("\nAP Mode Configuration:");
    Serial.print("SSID: ");
    Serial.println(ap_ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());

    // Start WebSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.println("WebSocket server started");

    // Initialize default rooms
    if(rooms.empty()) {
        rooms.emplace_back("1", "Living Room", 25, 26, 2500.0);
        rooms.emplace_back("2", "Bedroom", 27, 28, 2000.0);
        rooms.emplace_back("3", "Kitchen", 29, 30, 3000.0);
    }
}

void loop() {
    webSocket.loop();
    
    // Measure power for each room in rotation
    if(millis() - lastMeasure > 3000 && !rooms.empty()) {
        static size_t currentRoom = 0;
        rooms[currentRoom].measure();
        currentRoom = (currentRoom + 1) % rooms.size();
        lastMeasure = millis();
        
        // Send updated data
        sendRoomData();
    }
}