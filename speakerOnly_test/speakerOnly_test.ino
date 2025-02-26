#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

// I2S 引脚定义
#define I2S_BCLK 25
#define I2S_LRC 26
#define I2S_DOUT 22

// 旋律音符频率 (Hz)
#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 494
#define NOTE_C5 523

// "小星星" 旋律
int melody[] = {
    NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4,
    NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4, NOTE_D4, NOTE_C4
};

// 每个音符的时长 (ms)
int noteDurations[] = {
    500, 500, 500, 500, 500, 500, 1000,
    500, 500, 500, 500, 500, 500, 1000
};

// 初始化 I2S
void i2s_init() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 22050,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // **只输出左声道**
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,  // **增大 DMA 缓冲区，防止溢出**
        .use_apll = false,  // **关闭 APLL，防止 MAX98357A 失锁**
        .tx_desc_auto_clear = true
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,  // **LRC 连接 GND**
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_set_clk(I2S_NUM_0, 22050, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

// **填充静音数据，防止开机噪音**
void fill_silence() {
    int16_t silenceBuffer[1024] = { 0 };
    size_t bytesWritten;
    for (int i = 0; i < 10; i++) {
        i2s_write(I2S_NUM_0, silenceBuffer, sizeof(silenceBuffer), &bytesWritten, portMAX_DELAY);
    }
}

// **播放单个音符**
void play_note(int frequency, int duration_ms) {
    const int sampleRate = 22050;
    const float amplitude = (frequency == 0) ? 0 : 30000;  // **静音时发送 0**
    const float two_pi = 2.0 * M_PI;
    int bufferSize = 1024;

    int16_t sampleBuffer[bufferSize]; 
    size_t bytesWritten;
    int numSamples = (sampleRate * duration_ms) / 1000;

    for (int i = 0; i < numSamples; i += bufferSize / 2) {  
        for (int j = 0; j < bufferSize; j += 2) { 
            int16_t sampleValue = (int16_t)(sin((two_pi * frequency * j / sampleRate)) * amplitude);
            sampleBuffer[j] = 0;             // **左声道填充 0**
            sampleBuffer[j + 1] = sampleValue;  // **右声道填充音频数据**
        }
        i2s_write(I2S_NUM_0, sampleBuffer, bufferSize * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    }
}

// **播放旋律**
void play_melody() {
    for (int i = 0; i < 14; i++) {
        play_note(melody[i], noteDurations[i]);
        delay(100);
    }
}

void setup() {
    Serial.begin(115200);
    i2s_init();
    i2s_zero_dma_buffer(I2S_NUM_0);
    fill_silence();

    Serial.println("播放旋律...");
    play_melody();
}

void loop() {
    int16_t silenceBuffer[1024] = { 0 };
    size_t bytesWritten;
    while (true) {
        i2s_write(I2S_NUM_0, silenceBuffer, sizeof(silenceBuffer), &bytesWritten, portMAX_DELAY);
    }
}

