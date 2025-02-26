#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <vector>

// WiFi credentials for YOUR HOME NETWORK
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Cloud WebSocket Server (Replace with your server)
const char* wsHost = "your-cloud-websocket-server.com";
const int wsPort = 443;
const char* wsPath = "/ws";

WebSocketsClient webSocket;
PZEM004Tv30 pzem(Serial2, 16, 17);

class EnergyRoom {
public:
    String id;
    String name;
    float threshold;
    uint8_t measPin;
    uint8_t cutoffPin;
    float currentPower = -1;
    bool cutoffState = false;
    bool bypassDetected = false;

    EnergyRoom(String id, String name, uint8_t mPin, uint8_t cPin, float thresh)
    : id(id), name(name), measPin(mPin), cutoffPin(cPin), threshold(thresh) {
        pinMode(measPin, OUTPUT);
        pinMode(cutoffPin, OUTPUT);
        digitalWrite(measPin, LOW);
        digitalWrite(cutoffPin, LOW);
    }

    void measure() {
        digitalWrite(measPin, HIGH);
        delay(300);
        float power = pzem.power();
        digitalWrite(measPin, LOW);

        if(!isnan(power)) {
            currentPower = power;
            if(power > threshold) {
                digitalWrite(cutoffPin, HIGH);
                cutoffState = true;
            }
        }
        bypassDetected = (cutoffState && currentPower > 10);
    }

    void sendData() {
        DynamicJsonDocument doc(256);
        doc["id"] = id;
        doc["power"] = currentPower;
        doc["threshold"] = threshold;
        doc["cutoff"] = cutoffState;
        doc["bypass"] = bypassDetected;
        
        String output;
        serializeJson(doc, output);
        webSocket.sendTXT(output);
    }
};

std::vector<EnergyRoom> rooms;
unsigned long lastSend = 0;

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("Disconnected from cloud!");
            break;
            
        case WStype_CONNECTED:
            Serial.println("Connected to cloud!");
            // Send initial room configuration
            for(auto& room : rooms) {
                room.sendData();
            }
            break;
            
        case WStype_TEXT:
            handleCommand(payload, length);
            break;
    }
}

void handleCommand(uint8_t* payload, size_t length) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, payload, length);
    
    String command = doc["command"].as<String>();
    String roomId = doc["id"].as<String>();
    
    for(auto& room : rooms) {
        if(room.id == roomId) {
            if(command == "updateThreshold") {
                room.threshold = doc["value"];
            } 
            else if(command == "reset") {
                digitalWrite(room.cutoffPin, LOW);
                room.cutoffState = false;
            }
            room.sendData();
        }
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    
    while(WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi!");
    
    webSocket.beginSSL(wsHost, wsPort, wsPath);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);

    // Initialize rooms
    rooms.emplace_back("1", "Living Room", 25, 26, 2500);
    rooms.emplace_back("2", "Bedroom", 27, 28, 2000);
    rooms.emplace_back("3", "Kitchen", 29, 30, 3000);
}

void loop() {
    webSocket.loop();
    
    if(millis() - lastSend > 3000) {
        for(auto& room : rooms) {
            room.measure();
            room.sendData();
        }
        lastSend = millis();
    }
}