#include "Arduino.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "driver/i2s.h"

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
String serverHost = "158.179.207.117:5002";  // OCI
// String serverHost = "192.168.1.101:5002"; // local
String serverRegisterURL = "http://" + serverHost + "/register_esp32";           // Update ESP32 IP
String serverTimestampURL = "http://" + serverHost + "/audio/latest_timestamp";  // Get the latest timestamp
String serverAudioURL = "http://" + serverHost + "/audio/latest.wav";            // Download the latest audio


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


AsyncWebServer server(80);


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
      WiFiClient* stream = http.getStreamPtr();
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
void playAudio(const char* filename) {
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
    i2s_write(I2S_NUM_0, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  }

  file.close();
  Serial.println("Playback complete");
}


void setup() {
  Serial.begin(115200);
  delay(2000);

  // Connect to WiFi
  // WiFi.disconnect(true);
  WiFi.begin(ssid, password);
  // WiFi.begin(ssid, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD); // eduroam
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected successfully");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

  // Only update lastRegisteredIP if registration is successful
  if (registerESP32(true)) {
    lastRegisteredIP = WiFi.localIP().toString();
  }

  // Initialize SPIFFS (save audio)
  if (!SPIFFS.begin(true)) {
    Serial.println("Fail to initialize SPIFFS!");
    return;
  }

  // Initialize I2S
  i2s_config_t i2s_config = {
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

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  Serial.println("Initializing I2S...");
  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S installation failed: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S pin configuration failed: %d\n", err);
    return;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("I2S initialization complete");

  // HTTP server handles uploads
  // server.on("/audio_upload", HTTP_POST, [](AsyncWebServerRequest *request) {
  //     Serial.println("Received HTTP POST request: /audio_upload");
  //     request->send(200, "text/plain", "Upload successful");
  //     listSPIFFSFiles();
  //     Serial.println("Audio processing complete!");
  // },
  // [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  //     // If this is the start of a new file, delete the old file
  //     if (index == 0) {
  //         if (SPIFFS.exists("/received_audio.wav")) {
  //             SPIFFS.remove("/received_audio.wav");
  //             Serial.println("Deleted old audio file /received_audio.wav");
  //         }
  //     }

  //     Serial.printf("Receiving file: %s, Data size: %d bytes, index: %d\n", filename.c_str(), len);

  //     File file = SPIFFS.open("/received_audio.wav", index == 0 ? FILE_WRITE : FILE_APPEND);
  //     if (!file) {
  //         Serial.println("Unable to open file for writing!");
  //         return;
  //     }
  //     file.write(data, len);
  //     file.close();

  //     if (final) {
  //         Serial.println("File reception complete, stored at /received_audio.wav");

  //         // Automatically play after file reception is complete
  //         playAudio("/received_audio.wav");
  //     }
  // });

  server.begin();
  Serial.println("Server started");
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