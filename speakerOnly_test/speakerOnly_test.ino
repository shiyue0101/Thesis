#include <Arduino.h>
#include "SPIFFS.h"
#include "driver/dac.h"

#define AUDIO_PIN 25  // DAC1，对应 GPIO25

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 初始化 SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS 初始化失败！");
    return;
  }

  Serial.println("SPIFFS 初始化成功。开始播放音频...");
  dac_output_enable(DAC_CHANNEL_1);
  playAudio("/latest_audio.wav");
}

void loop() {
  // 不重复播放
}

// 播放 WAV 音频文件（8-bit PCM）
void playAudio(const char *filename) {
  if (!SPIFFS.exists(filename)) return;

  File file = SPIFFS.open(filename);
  if (!file) return;

  file.seek(44);  // 跳过 WAV 头

  const uint32_t sampleRate = 8000;
  const uint32_t delayTime = 1000000 / sampleRate;  // 微秒级采样周期

  uint8_t sample;
  uint32_t lastMicros = micros();

  while (file.available()) {
    sample = file.read();
    dacWrite(25, sample);

    // 更精确控制播放速率
    while (micros() - lastMicros < delayTime);
    lastMicros += delayTime;
  }

  file.close();
  // dacWrite(AUDIO_PIN, 0);  // 或 dacWrite(AUDIO_PIN, 128)
  dac_output_disable(DAC_CHANNEL_1);
  pinMode(25, INPUT);  // 让信号引脚悬空无电平输出
}
