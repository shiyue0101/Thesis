// Upload an ogg file to LittleFS and send a voice message to Discord
// Command + Shift + P > Upload LittleFS

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include <LittleFS.h>

struct UploadResponse {
    String upload_url;
    String upload_filename;
};

// WiFi 信息
const char *ssid = "YOUR_SSID"; // Replace
const char *password = "YOUR_PASSWORD"; // Replace

// Discord 相关
const char *bot_token = "YOUR_BOT_TOKEN";  // Replace
const char *channel_id = "YOUR_CHANNEL_ID";  // Replace

// 需要测试的本地 OGG 文件
const char *OGG_FILE_PATH = "/test.ogg";

// **初始化 SPIFFS**
void setupSPIFFS() {
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed!");
        return;
    }
    Serial.println("SPIFFS Mounted Successfully.");
}

void setupLittleFS() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed!");
        return;
    }
    Serial.println("LittleFS Mounted Successfully.");
}

// **WiFi 连接**
void setupWiFi() {
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi!");
}

void listSPIFFSFiles() {
    Serial.println("Listing SPIFFS files...");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
        Serial.print("File: ");
        Serial.print(file.name());
        Serial.print(" | Size: ");
        Serial.println(file.size());
        file = root.openNextFile();
    }
}

void listLittleFSFiles() {
    Serial.println("Listing LittleFS files...");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
        Serial.printf("File: %s | Size: %d bytes\n", file.name(), file.size());
        file = root.openNextFile();
    }
}


void writeOGGFile() {
    File file = SPIFFS.open("/test.ogg", FILE_WRITE);
    if (!file) {
        Serial.println("Failed to create file.");
        return;
    }

    const uint8_t dummyData[16] = {0x4F, 0x67, 0x67, 0x53, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // OGG 文件头
    file.write(dummyData, sizeof(dummyData)); // 写入 OGG 头部
    file.close();
    Serial.println("OGG file created.");
}

// **获取 Discord 上传 URL**
UploadResponse getDiscordUploadURL() {
    HTTPClient http;
    String url = "https://discord.com/api/v10/channels/" + String(channel_id) + "/attachments";

    String requestBody = "{\"files\":[{\"filename\":\"test.ogg\",\"file_size\":10240,\"id\":\"2\"}]}";
    int contentLength = requestBody.length();

    int maxRetries = 5;  // 最大重试次数
    int attempt = 0;      // 当前尝试次数
    int httpResponseCode;

    while (attempt < maxRetries) {
        Serial.printf("\nAttempt %d: Requesting Discord Upload URL...\n", attempt + 1);

        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bot " + String(bot_token));
        http.addHeader("Host", "discord.com");
        http.addHeader("Content-Length", String(contentLength));

        Serial.println("Sending Request...");
        httpResponseCode = http.POST(requestBody);

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println("Upload URL Response: " + response);

            // 检查 API 限制
            if (httpResponseCode == 429) {
                Serial.println("Rate limited. Waiting 5 seconds before retry...");
                delay(5000); // 5 秒后重试
            } else {
                // 解析 JSON 提取 upload_url
                int urlStart = response.indexOf("\"upload_url\":\"") + 14;
                int urlEnd = response.indexOf("\"", urlStart);
                int filenameStart = response.indexOf("\"upload_filename\":\"") + 19;
                int filenameEnd = response.indexOf("\"", filenameStart);

                if (urlStart > 13 && urlEnd > urlStart) {
                    UploadResponse result;
                    result.upload_url = response.substring(urlStart, urlEnd);
                    result.upload_filename = response.substring(filenameStart, filenameEnd);
                    return result;
                } else {
                    Serial.println("Failed to parse upload_url. Retrying...");
                }
            }
        } else {
            Serial.printf("Failed to get upload URL. HTTP Response: %d\n", httpResponseCode);
        }

        // WiFi 断开时重新连接
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected. Reconnecting...");
            WiFi.reconnect();
            delay(5000);
        }

        attempt++;
        delay(2000); // 2 秒后重试
    }

    Serial.println("Max retries reached. Failed to get upload URL.");
    return {"", ""}; // 返回空字符串，表示获取失败
}

// **上传 OGG 文件**
bool uploadAudioToDiscord(String uploadUrl) {
    if (uploadUrl == "") {
        Serial.println("Invalid upload URL.");
        return false;
    }

    const int maxRetries = 5;  // 最大重试次数
    int attempt = 0;           // 当前尝试次数
    int httpResponseCode;

    while (attempt < maxRetries) {
        Serial.printf("Attempt %d: Uploading Audio to Discord...\n", attempt + 1);

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected. Reconnecting...");
            WiFi.reconnect();
            delay(5000);
        }

        // File file = SPIFFS.open(OGG_FILE_PATH, FILE_READ);
        File file = LittleFS.open(OGG_FILE_PATH, FILE_READ);
        if (!file) {
            Serial.println("Failed to open file for reading.");
            return false;
        }

        size_t fileSize = file.size();
        Serial.printf("File size: %d bytes\n", fileSize);
        if (fileSize == 0) {
            Serial.println("File size is zero, aborting.");
            file.close();
            return false;
        }

        HTTPClient http;
        http.begin(uploadUrl);
        http.addHeader("Content-Type", "audio/ogg");
        http.addHeader("Host", "discord.com");

        httpResponseCode = http.sendRequest("PUT", &file, fileSize);

        file.close();
        http.end();

        Serial.print("uploadAudioToDiscord Response Code: ");
        Serial.println(httpResponseCode);

        // 如果上传成功 (HTTP 200)
        if (httpResponseCode == 200) {
            Serial.println("Audio uploaded successfully!");
            return true;
        } 
        // 如果服务器返回 429 (Rate Limited)，等待 5 秒重试
        else if (httpResponseCode == 429) {
            Serial.println("Rate limited. Waiting 5 seconds before retry...");
            delay(5000);
        } 
        // 其他错误，等待 2 秒后重试
        else {
            Serial.println("Upload failed. Retrying in 2 seconds...");
            delay(2000);
        }

        attempt++;
    }

    Serial.println("Max retries reached. Failed to upload audio.");
    return false;
}

// **发送语音消息**
void sendVoiceMessage(String uploadedFilename) {
    HTTPClient http;
    String url = "https://discord.com/api/v10/channels/" + String(channel_id) + "/messages";

    int maxRetries = 5;  // 最大重试次数
    int attempt = 0;      // 当前尝试次数
    int httpResponseCode;

    while (attempt < maxRetries) {
        Serial.printf("\nAttempt %d: Sending voice message to Discord...\n", attempt + 1);

        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bot " + String(bot_token));
        http.addHeader("Host", "discord.com");

        String waveform = "acU6Va9UcSVZzsVw7IU/80s0Kh/pbrTcwmpR9da4mvQejIMykkgo9F2FfeCd235K/atHZtSAmxKeTUgKxAdNVO8PAoZq1cHNQXT/PHthL2sfPZGSdxNgLH0AuJwVeI7QZJ02ke40+HkUcBoDdqGDZeUvPqoIRbE23Kr+sexYYe4dVq+zyCe3ci/6zkMWbVBpCjq8D8ZZEFo/lmPJTkgjwqnqHuf6XT4mJyLNphQjvFH9aRqIZpPoQz1sGwAY2vssQ5mTy5J5muGo+n82b0xFROZwsJpumDsFi4Da/85uWS/YzjY5BdxGac8rgUqm9IKh7E6GHzOGOy0LQIz3O4ntTg==";

        String payload = "{"
                         "\"flags\": 8192,"
                         "\"attachments\": [{"
                         "\"id\": \"0\","
                         "\"filename\": \"test.ogg\","
                         "\"uploaded_filename\": \"" + uploadedFilename + "\","
                         "\"duration_secs\": 14,"
                         "\"waveform\": \"" + waveform + "\""
                         "}]"
                         "}";

        Serial.println("Sending Request...");
        httpResponseCode = http.POST(payload);

        if (httpResponseCode == 200) {
            Serial.println("Voice message sent successfully!");
            http.end();
            return;  // 成功，直接返回
        } 
        else if (httpResponseCode == 429) {
            Serial.println("Rate limited. Waiting 5 seconds before retry...");
            delay(5000);
        } 
        else {
            Serial.printf("Failed to send voice message. HTTP Response: %d\n", httpResponseCode);
            Serial.println("Retrying in 2 seconds...");
            delay(2000);
        }

        // 关闭 HTTP 连接
        http.end();
        attempt++;
    }

    Serial.println("Max retries reached. Failed to send voice message.");
}

// **执行上传**
void sendAudioToDiscord() {
    Serial.println("Starting voice message process...");
    UploadResponse uploadInfo = getDiscordUploadURL();

    String uploadUrl = uploadInfo.upload_url;
    String uploadFilename = uploadInfo.upload_filename;

    if (!uploadUrl.isEmpty()) {
        if (uploadAudioToDiscord(uploadUrl)) {
            sendVoiceMessage(uploadFilename);
        } else {
            Serial.println("OGG 文件上传失败");
        }
    } else {
        Serial.println("获取上传 URL 失败");
    }
}

// **主程序**
void setup() {
    Serial.begin(115200);
    setupWiFi();
    // setupSPIFFS();
    setupLittleFS();
    // writeOGGFile();
    // listSPIFFSFiles();
    listLittleFSFiles();
    delay(10000);
    sendAudioToDiscord();
}

void loop() {
}