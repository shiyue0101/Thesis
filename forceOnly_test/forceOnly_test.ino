#include <WiFi.h>
#include <HTTPClient.h>

// 传感器引脚
const int pressurePin = 34;  // GPIO34 读取压力传感器
bool isHere = false;
int count = 0;

// WiFi 信息
const char *ssid = "YOUR_SSID"; // Replace
const char *password = "YOUR_PASSWORD"; // Replace

// Discord Webhook URL
const char *webhook_url = "YOUR_WEBHOOK_URL";  // Discord Webhook URL // Replace

// **连接 WiFi**
void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi!");
}

// **发送文本消息到 Discord**
void sendToDiscord(String message) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(webhook_url);
        http.addHeader("Content-Type", "application/json");

        String payload = "{\"content\": \"" + message + "\"}";
        int httpResponseCode = http.POST(payload);

        if (httpResponseCode > 0) {
            Serial.println("Message sent to Discord!");
        } else {
            Serial.print("Error sending message: ");
            Serial.println(httpResponseCode);
        }

        http.end();
    } else {
        Serial.println("WiFi not connected!");
    }
}

// **主循环**
void loop() {
    int sensorValue = analogRead(pressurePin); // 使用 analogRead()

    float voltage = sensorValue * (3.3 / 4095.0);
    float weightKg = voltage * (20.0 / 3.3);

    Serial.print("Pressure Sensor Value: ");
    Serial.print(sensorValue);
    Serial.print(" | Voltage: ");
    Serial.print(voltage, 2);
    Serial.print("V | Weight: ");
    Serial.print(weightKg, 2);
    Serial.println(" kg");

    if (weightKg > 0 && !isHere) {
        sendToDiscord("I am here!");
        isHere = true;
        count = 0;
    } else if (weightKg == 0 && isHere) {
        isHere = false;
        count = 0;
        sendToDiscord("I am leaving to do other things.");
    } else if (weightKg > 0 && isHere) {
        count++;
        if (count == 2) {
            sendToDiscord("I enjoy lying here.");
        }
    }
    
    delay(500);
}