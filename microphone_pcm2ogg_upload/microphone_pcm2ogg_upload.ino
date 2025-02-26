#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include <SPIFFS.h>
#include "driver/i2s.h"

struct UploadResponse {
    String upload_url;
    String upload_filename;
};

// Network connection settings (SSID & Password)
const char *ssid = "SleepyBear";
const char *password = "Jennyshi0101";

// Flask 服务器地址
// const char* flask_server = "http://192.168.1.101:5001/upload";
const char* flask_server = "http://158.179.207.117:5001/upload"; // OCI

// Discord 相关
const char *bot_token = "YOUR_BOT_TOKEN";  // Replace
const char *channel_id = "YOUR_CHANNEL_ID";  // Replace

// INMP441 配置
#define SAMPLE_RATE 16000
#define I2S_WS  15  // LRCLK
#define I2S_SD  35  // DATA
#define I2S_SCK 14  // BCLK
#define SAMPLE_BITS 16  // 16-bit 采样
#define RECORD_TIME 4   // 录音时长（秒）

// #define BUFFER_SIZE (SAMPLE_RATE * SAMPLE_BITS / 8 * RECORD_TIME)  // 8000*2*3 = 48KB


#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME / 2)

// 录制 PCM 数据
int16_t audio_buffer[BUFFER_SIZE];

// **初始化 I2S (INMP441)**
void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = -1, // 仅 RX
        .data_in_num = I2S_SD
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

// **录制音频**
void recordAudio() {
    Serial.println("Recording...");
    size_t bytes_read;
    memset(audio_buffer, 0, sizeof(audio_buffer));
    i2s_read(I2S_NUM_0, audio_buffer, sizeof(audio_buffer), &bytes_read, portMAX_DELAY);

    Serial.printf("Recording Complete. Bytes Read: %d\n", bytes_read);
    if (bytes_read == 0) {
        Serial.println("Error: No data read from I2S.");
        return;
    }

    // **打印录音时长**
    float recorded_time = (float)bytes_read / (SAMPLE_RATE * sizeof(int16_t));
    Serial.printf("Estimated recorded duration: %.2f seconds\n", recorded_time);


    // 打印前 20 个样本，确保数据有效
    Serial.println("Non-Zero Sample Data:");

    int count = 0;
    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (audio_buffer[i] != 0) {
            Serial.printf("%d ", audio_buffer[i]);
            count++;
        }
        if (count >= 20) break;  // 只打印前 20 个非 0 样本
    }

    if (count == 0) {
        Serial.println("All samples are zero!");
    } else {
        Serial.println();
    }
}

// **上传音频到 Flask 服务器**
bool uploadAudio() {
    WiFiClient client;
    client.setTimeout(10000); 
    HTTPClient http;
    const int maxRetries = 10;  // 最大重试次数
    int attempt = 0;           // 当前尝试次数
    int httpResponseCode;

    Serial.println("Uploading PCM to Flask...");

    while (attempt < maxRetries) {
        http.begin(client, flask_server);
        http.addHeader("Content-Type", "application/octet-stream");

        Serial.printf("Attempt %d: Uploading PCM data...\n", attempt + 1);
        httpResponseCode = http.POST((uint8_t*)audio_buffer, sizeof(audio_buffer));

        if (httpResponseCode == 200) {
            Serial.println("Received OGG file from Flask!");

            // **创建 SPIFFS 文件**
            File file = SPIFFS.open("/audio.ogg", FILE_WRITE);
            if (!file) {
                Serial.println("Failed to open /audio.ogg for writing!");
                return false;
            }

            // **开始读取 HTTP 响应体**
            int contentLength = http.getSize();
            WiFiClient *stream = http.getStreamPtr();
            uint8_t buffer[512];  // 512字节缓冲区
            int totalBytesRead = 0;

            while (http.connected() && contentLength > 0) {
                int bytesRead = stream->readBytes(buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    file.write(buffer, bytesRead);
                }
            }

            file.close();
            http.end();

            // **检查最终 OGG 文件大小**
            file = SPIFFS.open("/audio.ogg", FILE_READ);
            if (!file) {
                Serial.println("Error: Failed to read /audio.ogg!");
                return false;
            }
            size_t fileSize = file.size();
            Serial.printf("Final OGG file size: %d bytes\n", fileSize);
            file.close();

            return fileSize > 0;
        } 
        else {
            Serial.printf("Flask upload failed. HTTP code: %d\n", httpResponseCode);
            
            // WiFi 断开时重新连接
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi disconnected. Reconnecting...");
                WiFi.reconnect();
                delay(5000);
            }

            attempt++;
            if (attempt < maxRetries) {
                Serial.println("Retrying in 2 seconds...");
                delay(2000);  // 等待 2 秒后重试
            }
        }
        http.end();
    }

    Serial.println("Max retries reached. Failed to upload PCM to Flask.");
    return false;
}


UploadResponse getDiscordUploadURL() {
    HTTPClient http;
    String url = "https://discord.com/api/v10/channels/" + String(channel_id) + "/attachments";

    String requestBody = "{\"files\":[{\"filename\":\"audio.ogg\",\"file_size\":10240,\"id\":\"2\"}]}";
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

        File file = SPIFFS.open("/audio.ogg", FILE_READ);
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
                         "\"filename\": \"audio.ogg\","
                         "\"uploaded_filename\": \"" + uploadedFilename + "\","
                         "\"duration_secs\": 5,"
                         "\"waveform\": \"" + waveform + "\""
                         "}]"
                         "}";

        Serial.println("Sending Request...");
        Serial.printf("Sending voice message with uploaded filename: %s\n", uploadedFilename.c_str());
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

void ensureWiFiConnected() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected. Reconnecting...");
        WiFi.disconnect();
        WiFi.begin(ssid, password);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(1000);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi reconnected.");
        } else {
            Serial.println("\nFailed to reconnect WiFi.");
        }
    }
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

void generateFakePCM() {
    Serial.println("Generating fake PCM data...");

    for (int i = 0; i < BUFFER_SIZE; i++) {
        audio_buffer[i] = (int16_t)(sin(i * 0.1) * 32767);  // 生成正弦波模拟音频
    }

    Serial.println("Fake PCM data generated.");
}

// **主函数**
void setup() {
    delay(500);
    Serial.begin(115200);
    delay(500);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(1000);

    setupI2S();
    SPIFFS.begin(true);

    Serial.println("Record after 5 minutes!");
    delay(5000);
    recordAudio();

    ensureWiFiConnected();
    Serial.println("Uploading Fake PCM Data...");

    if (uploadAudio()) {
        Serial.println("Uploading to Discord...");
        sendAudioToDiscord();
    } else {
        Serial.println("Flask upload failed after retries. Aborting Discord upload.");
    }
}

void loop() {}
