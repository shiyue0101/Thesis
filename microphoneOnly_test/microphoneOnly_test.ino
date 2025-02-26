#include <Arduino.h>
#include <driver/i2s.h>

#define I2S_WS  15  // LRCLK
#define I2S_SD  35  // DATA
#define I2S_SCK 14  // BCLK
#define SAMPLE_RATE  16000  // 采样率 16kHz
#define SAMPLE_BITS  I2S_BITS_PER_SAMPLE_16BIT  // 16-bit 采样
#define RECORD_TIME  5       // 录音时间（秒）

#define BUFFER_SIZE  1024  // 缓冲区大小
int16_t buffer[BUFFER_SIZE]; // 16-bit 音频数据

void i2s_init() {
    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = SAMPLE_BITS,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_SIZE,
        .use_apll = false
    };

    const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = -1, // 仅 RX
        .data_in_num = I2S_SD
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

void setup() {
    Serial.begin(115200);
    i2s_init();
    Serial.println("准备录音...");
    delay(1000);
}

void loop() {
    int16_t buffer[1024]; // 16-bit 音频数据

    delay(5000);
    Serial.println("开始录音...");
    
    int totalSamples = SAMPLE_RATE * RECORD_TIME; // 计算总采样点
    size_t bytesRead;
    
    for (int i = 0; i < totalSamples / (BUFFER_SIZE / 2); i++) {
        i2s_read(I2S_NUM_0, buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);
        Serial.write((uint8_t*)buffer, bytesRead);  // 发送二进制数据到串口

        if (bytesRead > 0) {
            Serial.print("Received: ");
            for (int j = 0; j < bytesRead / 2; j++) {  // 16-bit 采样
                Serial.print(buffer[j]);  // 打印采样值（十进制）
                Serial.print(" ");
            }
            Serial.println();  // 换行，提高可读性
        } else {
            Serial.println("I2S 没有读取到数据!");
        }
    }
    
    Serial.println("录音完成！");
    delay(10000);  // 等待 10 秒再开始下一次录音
}
