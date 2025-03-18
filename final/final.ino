#include "Arduino.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "ESPAsyncWebServer.h"
#include "driver/i2s.h"
#include "ArduinoJson.h"
#include <queue>
#include <string>
#include "LittleFS.h"

struct UploadResponse {
  String upload_url;
  String upload_filename;
};

// Network connection settings (SSID & Password)
// const char *ssid = "SleepyBear";
// const char *password = "Jennyshi0101";
const char *ssid = "Xiaomi_2FAD";
const char *password = "65141212";
// EDUROAM network connection settings
// #define EAP_ANONYMOUS_IDENTITY "mfc903@ku.dk" //anonymous@example.com, or you can use also nickname@example.com
// #define EAP_IDENTITY "mfc903@ku.dk" //nickname@example.com, at some organizations should work nickname only without realm, but it is not recommended
// #define EAP_PASSWORD "Xiaotu102827*" //password for eduroam account
// #define EAP_USERNAME "mfc903@ku.dk" // the Username is the same as the Identity in most eduroam networks.
// const char* ssid = "eduroam"; // eduroam SSID

// Request URLs
String portConvert = ":5001";
String portRead = ":5002";
String portProxy = ":5003";
// String serveHost = "192.168.1.101";  // local
String serverHost = "158.179.207.117";                  // OCI
String proxyHost = "http://" + serverHost + portProxy;  // http://158.179.207.117:5003
// String proxyHost = "http://192.168.1.101:5003";
String serverConvert = "http://" + serverHost + portConvert + "/upload";
String serverRegisterURL = "http://" + serverHost + portRead + "/register_esp32";           // Update ESP32 IP
String serverTimestampURL = "http://" + serverHost + portRead + "/audio/latest_timestamp";  // Get the latest timestamp
String serverAudioURL = "http://" + serverHost + portRead + "/audio/latest.wav";

// Discord tokens
const char *bot_token = "MTMzOTA1MTcxNzc1MjI2MjczNw.G9wDl1.LwnSu5dX482dgyZvNJqifL7mEsCINdIHUdoh3I";
const char *channel_id = "1351547706627325972";

// Pressure sensor settings
#define PRESSURE_PIN_1 34  // force sensor interface
#define PRESSURE_PIN_2 32
#define PRESSURE_PIN_3 33
bool isHere = false;
unsigned long isHereStartTime = 0;             // 记录检测到 weightKg > 3 && !isHere 的时间
unsigned long isLeavingStartTime = 0;          // 记录宠物离开的时间
unsigned long isHereDurationStart = 0;         // 记录宠物在这里停留的开始时间
const unsigned long PET_STAY_DURATION = 3000;  // 5 分钟 (180000 毫秒)，TODO: Test

// INMP441 microphone settings
#define SAMPLE_RATE 16000
#define SAMPLE_BITS 16  // 16-bit sample
#define RECORD_TIME 4   // recording duration
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME / 2)
int16_t audio_buffer[BUFFER_SIZE];  // Record PCM
// INMP441 interface
#define I2S_WS 15   // LRCLK
#define I2S_SD 35   // DATA
#define I2S_SCK 14  // BCLK

// MAX98357A interface
#define I2S_BCLK 25
#define I2S_LRC 26
#define I2S_DIN 22

// Time intervals
#define CHECK_IP_INTERVAL 300000  // Interval to check ESP32 IP address
#define CHECK_VOICE_INTERVAL 500  // Interval to check for new messages

// Variables for comparison
String lastRegisteredIP = "";     // Stores last registered IP
unsigned long lastTimestamp = 0;  // Stores last played timestamp
bool firstRun = true;             // 标志位，ESP32 启动后第一次检测

// Max retries
#define MAX_RETRY_IP_REGISTER 5
#define MAX_RETRY_CONVERT 5
#define MAX_RETRY_GET_URL 5
#define MAX_RETRY_UPLOAD_FILE 5
#define MAX_RETRY_SEND 5
#define MAX_RETRY_SEND_TEXT 5
#define MAX_RETRY_AUDIO_DISCORD 5

// Queue
std::queue<String> petTaskQueue;        // 存储待执行的任务
TaskHandle_t petTalkTaskHandle = NULL;  // 任务句柄

// Daily Report
int dailyVisitCount = 0;                // 记录 "I am here!" 的次数
unsigned long dailyStayDuration = 0;    // 记录 isHere == true 且 weightKg > 3 的总时长（毫秒）
int dailyTalkCount = 0;                 // 记录语音交互次数
unsigned long lastDailyReportTime = 0;  // 记录上次发送每日总结的时间
#define DAILY_REPORT_HOUR 24            // 设定每天几点发送总结（24小时制）
#define DAILY_REPORT_MINUTE 0           // 设定每天几分发送总结
bool dailyReportSent = false;           // 标记当天是否已发送每日总结
// time
const char *ntpServer = "pool.ntp.org";  // NTP 时间服务器
const long gmtOffset_sec = 3600;         // 你的时区（欧洲哥本哈根 GMT+1）
const int daylightOffset_sec = 3600;     // 夏令时调整（如果有）

unsigned long lastPetStayTalkTime = 0;  // 猫窝持续停留的专用语音定时器
#define PET_TALK_INTERVAL 600000

AsyncWebServer server(80);


// Initialize I2S (INMP441)
// Configure I2S_NUM_0 for receiving audio (INMP441)
void setupI2S_RX() {
  i2s_config_t i2s_rx_config = {
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

  i2s_pin_config_t pin_rx_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,  // No output
    .data_in_num = I2S_SD
  };

  Serial.println("Initializing I2S (INMP441)...");
  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_rx_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S (INMP441) installation failed: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_NUM_0, &pin_rx_config);
  if (err != ESP_OK) {
    Serial.printf("I2S (INMP441) pin configuration failed: %d\n", err);
    return;
  }

  Serial.println("I2S (INMP441) initialization complete");
}


// Initialize I2S (MAX98357A)
// Configure I2S_NUM_1 for sending audio (MAX98357A)
void setupI2S_TX() {
  i2s_config_t i2s_tx_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false
  };

  i2s_pin_config_t pin_tx_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  Serial.println("Initializing I2S (MAX98357A)...");
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2s_tx_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S (MAX98357A) installation failed: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_NUM_1, &pin_tx_config);
  if (err != ESP_OK) {
    Serial.printf("I2S (MAX98357A) pin configuration failed: %d\n", err);
    return;
  }

  i2s_zero_dma_buffer(I2S_NUM_1);
  Serial.println("I2S (MAX98357A) initialization complete");
}


// Make sure that the Wifi is connected
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


void listLittleFSFiles() {
  Serial.println("=== List of files in LittleFS ===");

  File root = LittleFS.open("/");
  File file = root.openNextFile();

  if (!file) {
    Serial.println("LittleFS is empty or inaccessible!");
    return;
  }

  while (file) {
    Serial.printf("File: %s, Size: %d bytes\n", file.name(), file.size());

    // 如果是 .wav 文件，读取 WAV 头信息
    String filename = file.name();
    if (filename.endsWith(".wav")) {
      uint8_t header[44];
      file.seek(0);
      size_t readBytes = file.read(header, 44);
      if (readBytes >= 44) {
        // 提取通道数
        uint16_t channels = header[22] | (header[23] << 8);
        // 提取采样率
        uint32_t sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
        // 提取位深度
        uint16_t bitsPerSample = header[34] | (header[35] << 8);

        Serial.printf("  WAV Format: %u-bit, %u Hz, %u channel(s)\n", bitsPerSample, sampleRate, channels);
      } else {
        Serial.println("  Failed to read WAV header.");
      }
    }

    file = root.openNextFile();
  }

  Serial.println("===============================");
}


// Update ESP32 IP address (with retry mechanism)
bool registerESP32(bool allowRetry) {
  if (WiFi.status() == WL_CONNECTED) {
    String currentIP = WiFi.localIP().toString();

    // If IP has not changed, return true
    if (currentIP == lastRegisteredIP) {
      Serial.println("IP unchanged, no need to register");
      return true;
    }

    HTTPClient http;
    String requestURL = serverRegisterURL + "?ip=" + currentIP;
    int httpResponseCode = -1;
    int retryCount = 0;
    const int maxRetries = allowRetry ? MAX_RETRY_IP_REGISTER : 1;  // Initially retry up to 5 times, later only once

    Serial.print("Reporting ESP32 IP address to server: ");
    Serial.println(requestURL);

    while (retryCount < maxRetries) {
      http.begin(requestURL);
      httpResponseCode = http.GET();

      if (httpResponseCode > 0) {
        Serial.printf("Server response: %d\n", httpResponseCode);
        lastRegisteredIP = currentIP;  // Update lastRegisteredIP only on success
        http.end();
        return true;  // Success, return true
      } else {
        Serial.printf("HTTP request failed (%d/%d): %s\n", retryCount + 1, maxRetries, http.errorToString(httpResponseCode).c_str());
        if (allowRetry) {
          delay(5000);  // Retry only after 5 seconds initially
        } else {
          break;  // No retries for later attempts
        }
      }

      retryCount++;
      http.end();
    }

    Serial.println("IP registration failed, waiting for next attempt");
  }
  return false;  // Return false on failure
}


// Record audio and save it to audio_buffer
void recordAudio() {
  Serial.println("--- Recording...");
  size_t bytes_read;
  memset(audio_buffer, 0, sizeof(audio_buffer));
  i2s_read(I2S_NUM_0, audio_buffer, sizeof(audio_buffer), &bytes_read, portMAX_DELAY);

  Serial.printf("Recording Complete. Bytes Read: %d\n", bytes_read);
  if (bytes_read == 0) {
    Serial.println("Error: No data read from I2S.");
    return;
  }

  // Print recorded duration
  float recorded_time = (float)bytes_read / (SAMPLE_RATE * sizeof(int16_t));
  Serial.printf("Estimated recorded duration: %.2f seconds\n", recorded_time);

  // Print the first non-zero 20 sample data
  Serial.println("Non-Zero Sample Data:");

  int countHelper = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    if (audio_buffer[i] != 0) {
      Serial.printf("%d ", audio_buffer[i]);
      countHelper++;
    }
    if (countHelper >= 20) break;
  }

  if (countHelper == 0) {
    Serial.println("All samples are zero!");
  } else {
    Serial.println();
  }
}


// Generate fake PCM data (used for test)
// void generateFakePCM() {
//   Serial.println("Generating fake PCM data...");

//   for (int i = 0; i < BUFFER_SIZE; i++) {
//     audio_buffer[i] = (int16_t)(sin(i * 0.1) * 32767);  // 生成正弦波模拟音频
//     if (i % 500 == 0) yield();
//   }

//   Serial.println("Fake PCM data generated.");
// }


// Upload the recorded audio (pcm) to the server, get converted audio (ogg), and save it to LittleFS
bool uploadAudio() {
  WiFiClient client;
  client.setTimeout(10000);
  HTTPClient http;
  int attempt = 0;
  int httpResponseCode;

  Serial.println("Uploading PCM to Flask...");

  while (attempt < MAX_RETRY_CONVERT) {
    http.begin(client, serverConvert);
    http.addHeader("Content-Type", "application/octet-stream");

    Serial.printf("Attempt %d: Uploading PCM data...\n", attempt + 1);
    httpResponseCode = http.POST((uint8_t *)audio_buffer, sizeof(audio_buffer));

    if (httpResponseCode == 200) {
      Serial.println("Received OGG file from Flask!");

      // create LittleFS file
      File file = LittleFS.open("/audio.ogg", FILE_WRITE);
      if (!file) {
        Serial.println("Failed to open /audio.ogg for writing!");
        return false;
      }

      // read HTTP response
      int contentLength = http.getSize();
      WiFiClient *stream = http.getStreamPtr();
      uint8_t buffer[512];
      int totalBytesRead = 0;

      while (http.connected() && contentLength > 0) {
        int bytesRead = stream->readBytes(buffer, sizeof(buffer));
        if (bytesRead > 0) {
          file.write(buffer, bytesRead);
        }
      }

      file.close();
      http.end();
      client.stop();

      // check the final ogg file size
      file = LittleFS.open("/audio.ogg", FILE_READ);
      if (!file) {
        Serial.println("Error: Failed to read /audio.ogg!");
        return false;
      }
      size_t fileSize = file.size();
      Serial.printf("Final OGG file size: %d bytes\n", fileSize);
      file.close();
      return fileSize > 0;
    } else {
      Serial.printf("Flask upload failed. HTTP code: %d\n", httpResponseCode);

      ensureWiFiConnected();  // check the WiFi connection

      attempt++;
      if (attempt < MAX_RETRY_CONVERT) {
        Serial.println("Retrying in 2 seconds...");
        delay(2000);  // 等待 2 秒后重试
      }
    }
    http.end();
    client.stop();
  }

  Serial.println("Max retries reached. Failed to upload PCM to Flask.");
  return false;
}


// 1st Step: get discord upload URL
UploadResponse getDiscordUploadURL() {
  HTTPClient http;
  // String url = "https://discord.com/api/v10/channels/" + String(channel_id) + "/attachments";

  // String requestBody = "{\"files\":[{\"filename\":\"audio.ogg\",\"file_size\":10240,\"id\":\"2\"}]}";
  // int contentLength = requestBody.length();

  int attempt = 0;
  int httpResponseCode;

  while (attempt < MAX_RETRY_GET_URL) {
    Serial.printf("\nAttempt %d: Requesting Discord Upload URL...\n", attempt + 1);

    // http.begin(url);
    // http.addHeader("Content-Type", "application/json");
    // http.addHeader("Authorization", "Bot " + String(bot_token));
    // http.addHeader("Host", "discord.com");
    // http.addHeader("Content-Length", String(contentLength));

    http.begin(proxyHost + "/get_discord_upload_url");

    Serial.println("Sending Request...");
    // httpResponseCode = http.POST(requestBody);
    httpResponseCode = http.POST("");

    if (httpResponseCode > 0) {
      String response = http.getString();
      // Serial.println("Upload URL Response: " + response);

      // check API restrictions
      if (httpResponseCode == 429) {
        Serial.println("Rate limited. Waiting 5 seconds before retry...");
        delay(5000);
      } else {
        // Parse JSON
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, response);

        if (error) {
          Serial.println("Failed to parse JSON: " + String(error.c_str()));
          return UploadResponse();  // return empty object
        }

        // get the first object in `attachments`
        JsonArray attachments = doc["attachments"];
        if (attachments.size() == 0) {
          Serial.println("No attachments found in JSON.");
          return UploadResponse();
        }

        JsonObject firstAttachment = attachments[0];
        String uploadUrl = firstAttachment["upload_url"].as<String>();
        String uploadFilename = firstAttachment["upload_filename"].as<String>();

        if (uploadUrl.length() > 0 && uploadFilename.length() > 0) {
          UploadResponse result;
          result.upload_url = uploadUrl;
          result.upload_filename = uploadFilename;
          return result;
        } else {
          Serial.println("Failed to extract upload_url or upload_filename.");
          return UploadResponse();
        }
      }
    } else {
      Serial.printf("Failed to get upload URL. HTTP Response: %d\n", httpResponseCode);
    }

    http.end();
    ensureWiFiConnected();
    attempt++;
    delay(2000);  // retry after 2 seconds
  }

  Serial.println("Max retries reached. Failed to get upload URL.");
  return { "", "" };  // return empty string, which indicates the failure
}


// 2nd Step: Upload ogg file to discord server
bool uploadAudioToDiscord(String uploadUrl) {
  if (uploadUrl == "") {
    Serial.println("Invalid upload URL.");
    return false;
  } else {
    Serial.print("Upload URL: ");
    Serial.println(uploadUrl);
  }

  int attempt = 0;
  int httpResponseCode;

  while (attempt < MAX_RETRY_UPLOAD_FILE) {
    Serial.printf("Attempt %d: Uploading Audio to Discord...\n", attempt + 1);

    ensureWiFiConnected();

    File file = LittleFS.open("/audio.ogg", FILE_READ);
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


    // build boundary
    String boundary = "----ESP32Boundary";
    String bodyStart = "--" + boundary + "\r\n"
                                         "Content-Disposition: form-data; name=\"file\"; filename=\"audio.ogg\"\r\n"
                                         "Content-Type: audio/ogg\r\n\r\n";
    String bodyMiddle = "\r\n--" + boundary + "\r\n"
                                              "Content-Disposition: form-data; name=\"upload_url\"\r\n\r\n"
                        + uploadUrl + "\r\n";
    String bodyEnd = "--" + boundary + "--\r\n";
    size_t totalSize = bodyStart.length() + fileSize + bodyMiddle.length() + bodyEnd.length();

    WiFiClient client;  // 创建 WiFiClient 实例

    // 连接到服务器
    if (!client.connect("158.179.207.117", 5003)) {  // 例如 server = "192.168.1.101", port = 5003
      Serial.println("Connection failed!");
      return false;
    }
    client.print("POST /upload_audio_to_discord HTTP/1.1\r\n");
    client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
    client.print("Content-Length: " + String(totalSize) + "\r\n\r\n");
    client.print("\r\n");
    client.print(bodyStart);

    const int bufferSize = 4096;  // 4KB 缓冲区
    byte buffer[bufferSize];
    int bytesRead = 0;

    while (file.available()) {
      bytesRead = file.readBytes((char *)buffer, bufferSize);
      client.write(buffer, bytesRead);
    }
    file.close();

    client.print(bodyMiddle);
    client.print(bodyEnd);

    // 读取服务器响应
    Serial.println("Waiting for server response...");
    while (client.available() == 0) {
      delay(100);
    }

    String response;
    while (client.available()) {
      response += client.readString();
    }
    Serial.println("Server Response: ");
    Serial.println(response);

    client.stop();

    // 解析 HTTP 响应
    if (response.indexOf("200 OK") != -1) {
      Serial.println("Audio uploaded successfully!");
      setupI2S_RX();  // uploading ogg file to discord server CONFILCTS with I2S
      Serial.println("I2S Re-enabled After Uploading");
      return true;
    } else if (response.indexOf("429 Too Many Requests") != -1) {
      Serial.println("Rate limited. Waiting 5 seconds before retry...");
      delay(5000);
    } else {
      Serial.println("Upload failed. Retrying in 2 seconds...");
      delay(2000);
    }

    attempt++;
  }

  Serial.println("Max retries reached. Failed to upload audio.");
  return false;
}


// 3rd (Final) Step: Send voice message to Discord
void sendVoiceMessage(String uploadedFilename) {
  HTTPClient http;

  int attempt = 0;
  int httpResponseCode;

  while (attempt < MAX_RETRY_SEND) {
    Serial.printf("\nAttempt %d: Sending voice message to Discord...\n", attempt + 1);

    http.begin(proxyHost + "/send_voice_message");
    http.addHeader("Content-Type", "application/json");

    String waveform = "acU6Va9UcSVZzsVw7IU/80s0Kh/pbrTcwmpR9da4mvQejIMykkgo9F2FfeCd235K/atHZtSAmxKeTUgKxAdNVO8PAoZq1cHNQXT/PHthL2sfPZGSdxNgLH0AuJwVeI7QZJ02ke40+HkUcBoDdqGDZeUvPqoIRbE23Kr+sexYYe4dVq+zyCe3ci/6zkMWbVBpCjq8D8ZZEFo/lmPJTkgjwqnqHuf6XT4mJyLNphQjvFH9aRqIZpPoQz1sGwAY2vssQ5mTy5J5muGo+n82b0xFROZwsJpumDsFi4Da/85uWS/YzjY5BdxGac8rgUqm9IKh7E6GHzOGOy0LQIz3O4ntTg==";
    String payload = "{ \"uploaded_filename\": \"" + uploadedFilename + "\" }";
    Serial.print("Uploaded filename: ");
    Serial.println(uploadedFilename);
    int contentLength = payload.length();
    http.addHeader("Content-Length", String(contentLength));

    Serial.println("Sending Request...");
    httpResponseCode = http.POST(payload);

    if (httpResponseCode == 200) {  // if success, just return
      Serial.println("Voice message sent successfully!");
      http.end();
      return;
    } else if (httpResponseCode == 429) {
      Serial.println("Rate limited. Waiting 5 seconds before retry...");
      delay(5000);
    } else {
      Serial.printf("Failed to send voice message. HTTP Response: %d\n", httpResponseCode);
      Serial.println("Retrying in 2 seconds...");
      delay(2000);
    }

    ensureWiFiConnected();
    http.end();
    attempt++;
  }

  Serial.println("Max retries reached. Failed to send voice message.");
}


// Process of sending voice message to Discord
void sendAudioToDiscord() {
  Serial.println("Starting voice message process...");

  int attempt = 0;
  bool success = false;

  while (attempt < MAX_RETRY_AUDIO_DISCORD) {
    UploadResponse uploadInfo = getDiscordUploadURL();  // Step 1: 获取上传URL

    String uploadUrl = uploadInfo.upload_url;
    String uploadFilename = uploadInfo.upload_filename;

    if (!uploadUrl.isEmpty()) {
      if (uploadAudioToDiscord(uploadUrl)) {  // Step 2: 上传音频文件
        sendVoiceMessage(uploadFilename);     // Step 3: 在频道中发送语音消息
        dailyTalkCount++;
        success = true;
        break;  // 成功后退出循环
      } else {
        Serial.printf("Attempt %d: Failed to upload OGG file.\n", attempt + 1);
      }
    } else {
      Serial.printf("Attempt %d: Failed to get upload URL.\n", attempt + 1);
    }

    attempt++;
    if (attempt < MAX_RETRY_AUDIO_DISCORD) {
      Serial.println("Retrying voice message process...");
      delay(1000);
    }
  }

  if (!success) {
    Serial.println("Failed to send voice message to Discord after all retries.");
  }
}


// Complete procress from recording to sending voice message to Discord
void petTalkToOwner() {
  // Recording
  recordAudio();                    // Start recording and save data to audio_buffer
  i2s_driver_uninstall(I2S_NUM_0);  // Disable I2S after recording to avoid HTTPS requests being influenced

  // Upload recorded audio
  ensureWiFiConnected();
  if (uploadAudio()) {  // upload pcm file to server, get converted ogg file and save it to LittleFS
    Serial.println("Uploading to Discord...");
    sendAudioToDiscord();  // upload the voice message to Discord according to APIs
  } else {
    Serial.println("Flask upload failed after retries. Aborting Discord upload.");
  }
}

// TaskHandle_t petTalkTaskHandle = NULL;  // 任务句柄

void petTalkTask(void *pvParameters) {
  // Serial.println("Task started: petTalkTask");

  petTalkToOwner();  // 运行 petTalkToOwner 发送语音消息

  // Serial.println("------- Task completed. Checking for next task...");

  // **先检查队列是否有下一个任务**
  if (!petTaskQueue.empty()) {
    // Serial.printf("Queue size before pop: %d\n", petTaskQueue.size());

    String nextTask = petTaskQueue.front();  // 取出队列中的任务
    petTaskQueue.pop();                      // **弹出任务**
    // Serial.printf("Starting next task from queue: %s\n", nextTask.c_str());

    // **在清除任务句柄之前启动新任务**
    xTaskCreatePinnedToCore(
      petTalkTask, "PetTalkTask", 8192, NULL, 1, &petTalkTaskHandle, 1);

  } else {
    // Serial.println("No more queued tasks.");
    petTalkTaskHandle = NULL;  // **只有队列为空时才清除任务句柄**
  }

  vTaskDelete(NULL);  // **最后删除任务**
}


// 创建任务
void startPetTalkTask() {
  if (petTalkTaskHandle == NULL) {
    // Serial.println("------- No active task. Starting new petTalkTask...");

    BaseType_t result = xTaskCreatePinnedToCore(
      petTalkTask, "PetTalkTask", 8192, NULL, 1, &petTalkTaskHandle, 1);

    if (result == pdPASS) {
      // Serial.println("Task created successfully");
    } else {
      // Serial.println("Task creation failed!");
      petTalkTaskHandle = NULL;
    }
  } else {
    Serial.println("------ Task is already running. Adding task to queue...");
    petTaskQueue.push("New Task");  // **将任务加入队列**
  }
}

// Check if there is a new audio file in Discord Channel (No retry mechanism due to short polling interval)
void checkAndDownloadAudio() {
  ensureWiFiConnected();

  HTTPClient http;
  http.begin(serverTimestampURL);
  int httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    String response = http.getString();

    // Manually parse JSON to extract "timestamp"
    int startIndex = response.indexOf(":") + 1;  // Find position after ":"
    int endIndex = response.indexOf("}");        // Find position before "}"

    String timestampStr = response.substring(startIndex, endIndex);
    timestampStr.trim();  // Remove leading and trailing spaces

    // Convert to unsigned long
    unsigned long newTimestamp = (unsigned long)timestampStr.toDouble();

    http.end();

    // **如果是首次运行，仅更新 `lastTimestamp`，不下载音频**
    if (firstRun) {
      lastTimestamp = newTimestamp;  // **仅更新时间戳**
      firstRun = false;              // **标志位设为 false，后续检测正常运行**
      return;
    }

    if (newTimestamp > lastTimestamp) {  // Check if this is new audio
      listLittleFSFiles();
      Serial.println("--- New audio detected, starting download...");
      lastTimestamp = newTimestamp;  // Update play record
      downloadAndPlayAudio();
      dailyTalkCount++;

      delay(10000);
      startPetTalkTask();  // start recording and send voice message to owner
    } else {
      // Serial.println("No new audio, skipping playback");
    }
  } else {
    Serial.printf("Failed to get timestamp: HTTP %d\n", httpResponseCode);  // TODO: test
    ensureWiFiConnected();
    // WiFi.disconnect(true);
    // WiFi.begin(ssid, password);
    // Serial.println("Wifi reconnected!");
  }
  http.end();
}


// Download the latest audio from Discord (with retry mechanism)
// TODO: timeout
void downloadAndPlayAudio() {
  HTTPClient http;
  const int maxRetries = 5;
  const int retryDelay = 2000;  // 2 seconds
  int attempt = 0;
  int httpResponseCode;

  while (attempt < maxRetries) {
    http.begin(serverAudioURL);
    httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
      File file = LittleFS.open("/latest_audio.wav", FILE_WRITE);
      if (!file) {
        Serial.println("Unable to create audio file");
        http.end();
        return;
      }

      int totalSize = http.getSize();
      WiFiClient *stream = http.getStreamPtr();
      uint8_t buffer[2048];

      while (http.connected() && totalSize > 0) {
        size_t readBytes = stream->readBytes(buffer, sizeof(buffer));
        file.write(buffer, readBytes);
        totalSize -= readBytes;
      }
      file.close();

      // listLittleFSFiles();
      Serial.println("Audio download complete, starting playback...");
      playAudio("/latest_audio.wav");

      http.end();
      return;  // Download successful, exit function
    } else {
      Serial.printf("Audio download failed: HTTP %d, retrying (%d/%d)...\n", httpResponseCode, attempt + 1, maxRetries);
      http.end();
      delay(retryDelay);
      attempt++;
    }
  }

  Serial.println("Audio download failed, maximum retry attempts reached");
}


// Play the recently downloaded file from LittleFS
void playAudio(const char *filename) {
  Serial.printf("Attempting to play file: %s\n", filename);

  if (!LittleFS.exists(filename)) {
    Serial.println("Audio file does not exist");
    return;
  }

  File file = LittleFS.open(filename);
  if (!file) {
    Serial.println("Unable to open audio file");
    return;
  }

  size_t fileSize = file.size();
  Serial.printf("Reading file: %s (Size: %d bytes)\n", filename, fileSize);

  if (fileSize <= 44) {
    Serial.println("File too small.");
    file.close();
    return;
  }
  file.seek(44);

  uint8_t buffer[1024];
  unsigned long startPlayTime = millis();
  while (file.available()) {
    size_t bytesRead = file.read(buffer, sizeof(buffer));
    size_t bytesWritten;
    i2s_write(I2S_NUM_1, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
    // uint8_t sample = file.read();  // Read byte from audio file
    // dacWrite(I2S_BCLK, sample);          // Output to GPIO25 (DAC1)
    // delayMicroseconds(125);       // ~8kHz sample rate
  }

  file.close();

  int bitsPerSample = 16;
  int numChannels = 2;
  int sampleRate = 44100;
  // 实际音频数据大小（去掉44字节WAV头）
  size_t audioDataSize = fileSize - 44;
  // 总样本数 = 数据字节数 / 每个样本的总字节数
  float durationSeconds = (float)audioDataSize / (sampleRate * numChannels * (bitsPerSample / 8));
  Serial.printf("Estimated audio duration: %.2f seconds\n", durationSeconds);
  unsigned long playDuration = millis() - startPlayTime;
  unsigned long expectedDuration = (unsigned long)(durationSeconds * 1000);
  if (playDuration < expectedDuration) {
    unsigned long remainingDelay = expectedDuration - playDuration;
    // Serial.printf("Delaying %.0f ms to complete output\n", (float)remainingDelay);
    delay(remainingDelay);  // 等待输出完成
  }

  // 清除 DMA 缓冲+播放静音
  i2s_zero_dma_buffer(I2S_NUM_1);
  int16_t silence = 0;
  for (int i = 0; i < 2000; i++) {
    size_t bytesWritten;
    i2s_write(I2S_NUM_1, &silence, sizeof(silence), &bytesWritten, portMAX_DELAY);
  }

  Serial.println("Playback complete");
}


// Send text messages to Discord
void sendToDiscord(String message) {
  int attempt = 0;
  int httpResponseCode = -1;

  while (attempt < MAX_RETRY_SEND_TEXT) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(proxyHost + "/send_text_message");
      http.addHeader("Content-Type", "application/json");

      String payload = "{\"message\": \"" + message + "\"}";
      httpResponseCode = http.POST(payload);

      if (httpResponseCode > 0) {
        Serial.printf("--- Message sent to Discord via server (Attempt %d): %s\n", attempt + 1, message.c_str());
        http.end();
        return;  // 成功后直接返回
      } else {
        Serial.printf("Error sending message (Attempt %d): HTTP %d\n", attempt + 1, httpResponseCode);
      }

      http.end();
    } else {
      ensureWiFiConnected();
    }

    attempt++;
    delay(1000);  // 重试间隔
  }

  Serial.println("Failed to send message to Discord after retries.");
}


void printForceInfo(int sensorValue, float voltage, float weightKg, int sensorNum) {
  Serial.print("Sensor ");
  Serial.print(sensorNum);
  Serial.print(" -> Value: ");
  Serial.print(sensorValue);
  Serial.print(" | Voltage: ");
  Serial.print(voltage, 2);
  Serial.print("V | Weight: ");
  Serial.print(weightKg, 2);
  Serial.println(" kg");
}


String getRandomLineFromFile(const char *filename, int totalLines) {
  if (!LittleFS.exists(filename)) {
    return "Meow~ I cannot find my voice.";
  }

  File file = LittleFS.open(filename, FILE_READ);
  if (!file) {
    return "Meow~ File error.";
  }

  // 随机选择一行
  int targetLine = random(0, totalLines);  // 行号从 0 开始

  String line;
  for (int i = 0; i <= targetLine; ++i) {
    line = file.readStringUntil('\n');
  }

  file.close();
  line.trim();
  return line;
}


// Force detection
// If the pet leave shortly after come here, messages will be immediately sent without waiting for the recording to be completed
void forceDetectAndSendMessage() {
  // 第一个传感器
  int sensorValue1 = analogRead(PRESSURE_PIN_1);
  float voltage1 = sensorValue1 * (3.3 / 4095.0);
  float weightKg1 = (3.3 - voltage1) * (20.0 / 3.3);
  if (weightKg1 < 0) weightKg1 = 0;

  // 第二个传感器
  int sensorValue2 = analogRead(PRESSURE_PIN_2);
  float voltage2 = sensorValue2 * (3.3 / 4095.0);
  float weightKg2 = (3.3 - voltage2) * (20.0 / 3.3);
  if (weightKg2 < 0) weightKg2 = 0;

  // 第三个传感器
  int sensorValue3 = analogRead(PRESSURE_PIN_3);
  float voltage3 = sensorValue3 * (3.3 / 4095.0);
  float weightKg3 = (3.3 - voltage3) * (20.0 / 3.3);
  if (weightKg3 < 0) weightKg3 = 0;

  unsigned long currentTime = millis();
  String msg = "Failed!";
  // printForceInfo(sensorValue1, voltage1, weightKg1, 1);
  // printForceInfo(sensorValue2, voltage2, weightKg2, 2);
  // printForceInfo(sensorValue3, voltage3, weightKg3, 3);

  // 1. 检测宠物到达
  if ((weightKg1 > 3 || weightKg2 > 3 || weightKg3 > 3) && !isHere) {  // 宠物来了，但之前 isHere = false
    if (isHereStartTime == 0) {                                        // **只在第一次进入时记录时间**
      isHereStartTime = currentTime;
    }

    if (currentTime - isHereStartTime >= 2000) {  // **宠物持续 2 秒以上**
      printForceInfo(sensorValue1, voltage1, weightKg1, 1);
      printForceInfo(sensorValue2, voltage2, weightKg2, 2);
      printForceInfo(sensorValue3, voltage3, weightKg3, 3);

      // 假设您已统计过这个文件总共有 1000 行
      msg = getRandomLineFromFile("/cat_entry_messages.txt", 1000);
      sendToDiscord(msg);

      dailyVisitCount++;
      isHere = true;
      isHereDurationStart = currentTime;  // **记录宠物开始停留的时间**
      lastPetStayTalkTime = currentTime;
      startPetTalkTask();  // 开始录音并发送语音消息

      isHereStartTime = 0;  // **状态改变后重置计时**
    }
  } else if (weightKg1 <= 3 && weightKg2 <= 3 && weightKg3 <= 3) {
    isHereStartTime = 0;  // **如果宠物没有继续停留，则重置计时**
  }

  // 2. 检测宠物离开
  if ((weightKg1 < 0.1 && weightKg2 < 0.1 && weightKg3 < 0.1) && isHere) {  // 宠物离开，但之前 isHere = true
    if (isLeavingStartTime == 0) {                                          // **只在第一次检测到离开时记录时间**
      isLeavingStartTime = currentTime;
    }

    if (currentTime - isLeavingStartTime >= 2000) {  // **宠物真正离开 2 秒**
      printForceInfo(sensorValue1, voltage1, weightKg1, 1);
      printForceInfo(sensorValue2, voltage2, weightKg2, 2);
      isHere = false;
      msg = getRandomLineFromFile("/cat_exit_messages.txt", 1000);
      sendToDiscord(msg);
      dailyStayDuration += (currentTime - isHereDurationStart);  // 累计停留时间
      isHereDurationStart = 0;                                   // **重置停留时间**
      startPetTalkTask();                                        // 录音并发送语音消息

      isLeavingStartTime = 0;  // **状态改变后重置计时**
    }
  } else if (weightKg1 > 0 || weightKg2 > 0 || weightKg3 > 0) {
    isLeavingStartTime = 0;  // **如果宠物又回来了，重置离开计时**
  }

  // 3. **宠物持续在此处超过 5 分钟**
  if ((weightKg1 > 3 || weightKg2 > 3 || weightKg3 > 3) && isHere) {
    if (isHereDurationStart > 0 && (currentTime - isHereDurationStart >= PET_STAY_DURATION)) {
      String cozyMessage = getRandomLineFromFile("/cat_stay_messages.txt", 1000);
      cozyMessage += "\\n";
      cozyMessage += "```";
      cozyMessage += "    |\\\\      _,,,---,,_\\n";
      cozyMessage += "ZZZ /,.-''    -.  ;-;;,_\\n";
      cozyMessage += "   |,4-  ) )-,_. ,\\\\ (  '-'\\n";
      cozyMessage += "  '---''(_/--'  -'_)";
      cozyMessage += "```";
      sendToDiscord(cozyMessage);

      // sendToDiscord("I enjoy lying here.");  // **发送 "I enjoy lying here."**
      isHereDurationStart = 0;  // **防止重复触发**
      lastPetStayTalkTime = 0;
    }
  }
}


// 每天定时发送统计信息
// TODO: 00:00卡住没发送，过了0点目前是无法再发的
void sendDailyReport() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  // 判断是否到达发送时间，并且当天还未发送
  if (((timeinfo.tm_hour = DAILY_REPORT_HOUR && timeinfo.tm_min >= DAILY_REPORT_MINUTE) || timeinfo.tm_hour >= DAILY_REPORT_HOUR) && !dailyReportSent) {
    String report = "Hi Yue,\\n";
    report += "- I visited my nest " + String(dailyVisitCount) + " times today.\\n";
    report += "- I stayed in my nest for " + String(dailyStayDuration / 60000) + " minutes.\\n";
    report += "- I talked to you " + String(dailyTalkCount) + " times today.\\n";
    report += "I enjoy a lot. ";

    sendToDiscord(report);
    dailyReportSent = true;
  }

  // 每天凌晨 00:00 重置标志，允许再次发送
  if (timeinfo.tm_hour == 0 && timeinfo.tm_min == 0 && (dailyVisitCount && dailyStayDuration && dailyTalkCount) != 0 && dailyReportSent == true) {
    dailyReportSent = false;
    dailyVisitCount = 0;
    dailyStayDuration = 0;
    dailyTalkCount = 0;
    Serial.println("Daily report reset for a new day.");
  }
}


void setup() {
  Serial.begin(115200);
  delay(2000);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  // WiFi.begin(ssid, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD); // eduroam
  // Detect the status of WiFi
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected successfully");

  // Get ESP32 IP address and register it
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());
  // Only update lastRegisteredIP if registration is successful
  if (registerESP32(true)) {
    lastRegisteredIP = WiFi.localIP().toString();
  }

  // Initialize I2S
  setupI2S_RX();  // INMP411
  setupI2S_TX();  // MAX98357A

  // Initialize LittleFS (save audio)
  if (!LittleFS.begin(true)) {
    Serial.println("Fail to initialize LittleFS!");
    return;
  }
  LittleFS.begin(true);

  // **同步 NTP 时间**
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Waiting for time sync...");

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {  // 等待时间同步成功
    Serial.println("Failed to obtain time. Retrying...");
    delay(2000);
  }
  Serial.println("Time synchronized successfully!");

  server.begin();
  Serial.println("Server started");
}


void loop() {
  static unsigned long lastCheckTime = 0, lastRegisterTime = 0;  // Track last IP registration time
  unsigned long currentMillis = millis();

  sendDailyReport();

  // Check if there is a new audio file in Discord Channel
  if (currentMillis - lastCheckTime >= CHECK_VOICE_INTERVAL) {
    lastCheckTime = currentMillis;
    checkAndDownloadAudio();
  }

  // Check if ESP32 IP address has to be updated
  if (currentMillis - lastRegisterTime >= CHECK_IP_INTERVAL) {
    lastRegisterTime = currentMillis;
    // Do not retry, just wait for the next round
    if (registerESP32(false)) {
      lastRegisteredIP = WiFi.localIP().toString();
    }
  }

  forceDetectAndSendMessage();

  if (isHere && millis() - lastPetStayTalkTime >= PET_TALK_INTERVAL) {
    startPetTalkTask();
    lastPetStayTalkTime = millis();
  }

  delay(500);
}
