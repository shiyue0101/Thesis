#include "Arduino.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "driver/i2s.h"

struct UploadResponse {
  String upload_url;
  String upload_filename;
};

// Network connection settings (SSID & Password)
const char *ssid = "YOUR_SSID"; // Replace
const char *password = "YOUR_PASSWORD"; // Replace
// EDUROAM network connection settings
// #define EAP_ANONYMOUS_IDENTITY "mfc903@ku.dk" //anonymous@example.com, or you can use also nickname@example.com
// #define EAP_IDENTITY "mfc903@ku.dk" //nickname@example.com, at some organizations should work nickname only without realm, but it is not recommended
// #define EAP_PASSWORD "Xiaotu102827*" //password for eduroam account
// #define EAP_USERNAME "mfc903@ku.dk" // the Username is the same as the Identity in most eduroam networks.
// const char* ssid = "eduroam"; // eduroam SSID

// Request URLs
String portConvert = ":5001";
String portRead = ":5002";
// String serverHost = "192.168.1.101"; // local
String serverHost = "158.179.207.117";  // OCI
String serverConvert = "http://" + serverHost + portConvert + "/upload";
String serverRegisterURL = "http://" + serverHost + portRead + "/register_esp32";           // Update ESP32 IP
String serverTimestampURL = "http://" + serverHost + portRead + "/audio/latest_timestamp";  // Get the latest timestamp
String serverAudioURL = "http://" + serverHost + portRead + "/audio/latest.wav";

// Discord tokens
const char *bot_token = "YOUR_BOT_TOKEN";  // Replace
const char *channel_id = "YOUR_CHANNEL_ID";  // Replace

// INMP441 microphone settings
#define SAMPLE_RATE 8000
#define SAMPLE_BITS 16  // 16-bit sample
#define RECORD_TIME 4   // recording duration
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME)
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

// Max retries
#define MAX_RETRY_IP_REGISTER 5
#define MAX_RETRY_CONVERT 5
#define MAX_RETRY_GET_URL 5
#define MAX_RETRY_UPLOAD_FILE 5
#define MAX_RETRY_SEND 5


AsyncWebServer server(80);


// Initialize I2S (INMP441)
// Configure I2S_NUM_0 for receiving audio (INMP441)
void setupI2S_RX() {
  i2s_config_t i2s_rx_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
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


// List all files in SPIFFS
void listSPIFFSFiles() {
  Serial.println("=== List of files in SPIFFS ===");

  File root = SPIFFS.open("/");
  File file = root.openNextFile();

  if (!file) {
    Serial.println("SPIFFS is empty or inaccessible!");
    return;
  }

  while (file) {
    Serial.printf("File: %s, Size: %d bytes\n", file.name(), file.size());
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
  Serial.println("Recording...");
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

  int count = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    if (audio_buffer[i] != 0) {
      Serial.printf("%d ", audio_buffer[i]);
      count++;
    }
    if (count >= 20) break;
  }

  if (count == 0) {
    Serial.println("All samples are zero!");
  } else {
    Serial.println();
  }
}


// Generate fake PCM data (used for test)
void generateFakePCM() {
  Serial.println("Generating fake PCM data...");

  for (int i = 0; i < BUFFER_SIZE; i++) {
    audio_buffer[i] = (int16_t)(sin(i * 0.1) * 32767);  // 生成正弦波模拟音频
    if (i % 500 == 0) yield();
  }

  Serial.println("Fake PCM data generated.");
}


// Upload the recorded audio (pcm) to the server, get converted audio (ogg), and save it to SPIFFS
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

      // create SPIFFS file
      File file = SPIFFS.open("/audio.ogg", FILE_WRITE);
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
      file = SPIFFS.open("/audio.ogg", FILE_READ);
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
  String url = "https://discord.com/api/v10/channels/" + String(channel_id) + "/attachments";

  String requestBody = "{\"files\":[{\"filename\":\"audio.ogg\",\"file_size\":10240,\"id\":\"2\"}]}";
  int contentLength = requestBody.length();

  int attempt = 0;
  int httpResponseCode;

  while (attempt < MAX_RETRY_GET_URL) {
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

      // check API restrictions
      if (httpResponseCode == 429) {
        Serial.println("Rate limited. Waiting 5 seconds before retry...");
        delay(5000);
      } else {
        // get upload_url from JSON
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
  }

  int attempt = 0;
  int httpResponseCode;

  while (attempt < MAX_RETRY_UPLOAD_FILE) {
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

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(uploadUrl);
    http.addHeader("Content-Type", "audio/ogg");
    http.addHeader("Host", "discord.com");

    httpResponseCode = http.sendRequest("PUT", &file, fileSize);

    file.close();
    http.end();

    Serial.print("uploadAudioToDiscord Response Code: ");
    Serial.println(httpResponseCode);

    // If upload is successful (HTTP 200)
    if (httpResponseCode == 200) {
      Serial.println("Audio uploaded successfully!");
      setupI2S_RX();
      Serial.println("I2S Re-enabled After Uploading");
      return true;
    }
    // IF the server returns 429 (Rate Limited), wait 5 seconds and retry
    else if (httpResponseCode == 429) {
      Serial.println("Rate limited. Waiting 5 seconds before retry...");
      delay(5000);
    }
    // Other errors, wait 5 seconds and retry
    else {
      Serial.println("Upload failed. Retrying in 2 seconds...");
      delay(5000);
    }

    client.stop();
    http.end();

    ensureWiFiConnected();
    attempt++;
  }

  Serial.println("Max retries reached. Failed to upload audio.");
  return false;
}


// 3rd (Final) Step: Send voice message to Discord
void sendVoiceMessage(String uploadedFilename) {
  HTTPClient http;
  String url = "https://discord.com/api/v10/channels/" + String(channel_id) + "/messages";

  int attempt = 0;
  int httpResponseCode;

  while (attempt < MAX_RETRY_SEND) {
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
                     "\"uploaded_filename\": \""
                     + uploadedFilename + "\","
                                          "\"duration_secs\": 5,"
                                          "\"waveform\": \""
                     + waveform + "\""
                                  "}]"
                                  "}";

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


// Complete process of sending voice message to Discord
void sendAudioToDiscord() {
  Serial.println("Starting voice message process...");
  UploadResponse uploadInfo = getDiscordUploadURL();  // 1st step: get upload URL

  String uploadUrl = uploadInfo.upload_url;
  String uploadFilename = uploadInfo.upload_filename;

  if (!uploadUrl.isEmpty()) {
    if (uploadAudioToDiscord(uploadUrl)) {  // 2nd step: upload ogg file to discord server
      sendVoiceMessage(uploadFilename);     // erd step: send voice message to discord channel
    } else {
      Serial.println("Fail to upload OGG file!");
    }
  } else {
    Serial.println("Fail to get upload URL!");
  }
}


// Check if there is a new audio file in Discord Channel (No retry mechanism due to short polling interval)
void checkAndDownloadAudio() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot fetch audio");
    return;
  }

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

    if (newTimestamp > lastTimestamp) {  // Check if this is new audio
      listSPIFFSFiles();
      Serial.println("New audio detected, starting download...");
      lastTimestamp = newTimestamp;  // Update play record
      downloadAndPlayAudio();
    } else {
      // Serial.println("No new audio, skipping playback");
    }
  } else {
    Serial.printf("Failed to get timestamp: HTTP %d\n", httpResponseCode);
    WiFi.disconnect(true);
    WiFi.begin(ssid, password);
    Serial.println("Wifi reconnected!");
  }
  http.end();
}


// Download the latest audio from Discord (with retry mechanism)
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
      File file = SPIFFS.open("/latest_audio.wav", FILE_WRITE);
      if (!file) {
        Serial.println("Unable to create audio file");
        return;
      }

      int totalSize = http.getSize();
      WiFiClient *stream = http.getStreamPtr();
      uint8_t buffer[1024];

      while (http.connected() && totalSize > 0) {
        size_t readBytes = stream->readBytes(buffer, sizeof(buffer));
        file.write(buffer, readBytes);
        totalSize -= readBytes;
      }
      file.close();

      // listSPIFFSFiles();
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


// Play the recently downloaded file from SPIFFS
void playAudio(const char *filename) {
  Serial.printf("Attempting to play file: %s\n", filename);

  if (!SPIFFS.exists(filename)) {
    Serial.println("Audio file does not exist");
    return;
  }

  File file = SPIFFS.open(filename);
  if (!file) {
    Serial.println("Unable to open audio file");
    return;
  }

  Serial.printf("Reading file: %s (Size: %d bytes)\n", filename, file.size());

  uint8_t buffer[1024];
  while (file.available()) {
    size_t bytesRead = file.read(buffer, sizeof(buffer));
    size_t bytesWritten;
    i2s_write(I2S_NUM_1, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  }

  file.close();
  Serial.println("Playback complete");
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

  // Initialize SPIFFS (save audio)
  if (!SPIFFS.begin(true)) {
    Serial.println("Fail to initialize SPIFFS!");
    return;
  }
  SPIFFS.begin(true);

  server.begin();
  Serial.println("Server started");

  // Start recording and save data to audio_buffer
  // Serial.println("Generating Fake PCM Data...");
  // generateFakePCM();  // generate fake pcm data to test
  recordAudio();
  i2s_driver_uninstall(I2S_NUM_0);  // Disable I2S after recording to avoid HTTPS requests being influenced
  // Serial.println("I2S Disabled After Fake PCM");

  // Upload recorded audio
  ensureWiFiConnected();
  if (uploadAudio()) {  // upload pcm file to server, get converted ogg file and save it to SPIFFS
    Serial.println("Uploading to Discord...");
    sendAudioToDiscord();  // upload the voice message to Discord according to APIs
  } else {
    Serial.println("Flask upload failed after retries. Aborting Discord upload.");
  }
}


void loop() {
  static unsigned long lastCheckTime = 0, lastRegisterTime = 0;  // Track last IP registration time
  unsigned long currentMillis = millis();

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
}
