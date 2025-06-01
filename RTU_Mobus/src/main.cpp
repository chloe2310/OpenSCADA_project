#include <Arduino.h>
#include <ModbusRTU.h>
#include <dht11.h>

// Cấu hình hệ thống
#define SLAVE_ID 1
#define REG_TEMP 1          // Thanh ghi ánh sáng
#define REG_LED 2
#define BAUD_RATE 9600
#define RX_PIN 16            // UART2 RX (nối với RO của module RS485)
#define TX_PIN 17            // UART2 TX (nối với DI của module RS485)
#define RE_DE_PIN 4          // Chân điều khiển RE/DE của module RS485
#define LED_PIN 5

ModbusRTU mb;

void setup() {
  Serial.begin(115200); // Tần số baudrate
  DHT11_Init();
  Serial.println("ESP32 Modbus RTU Slave Light");

  // Cấu hình chân điều khiển RE/DE
  pinMode(RE_DE_PIN, OUTPUT);
  digitalWrite(RE_DE_PIN, LOW);  // Mặc định ở chế độ nhận

  // Khởi động UART2 cho RS485
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  mb.begin(&Serial2, BAUD_RATE);
  mb.slave(SLAVE_ID);

  // Cấu hình chân LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Đăng ký thanh ghi Modbus cho nhiet do
  mb.addHreg(REG_TEMP);
  mb.addHreg(REG_LED);  
}

void loop() {
  // Đọc giá trị ánh sáng từ cảm biến analog (0–4095)
  int temp_value =  (int)DHT11_ReadTemperature();
  // Kiểm tra giá trị hợp lệ và ghi vào thanh ghi
  if (temp_value >= 0 && temp_value <= 4095) {
    mb.Hreg(REG_TEMP, temp_value);
    
    Serial.println(temp_value);
  } else {
    Serial.println("Error!");
  }

  // Đọc giá trị từ thanh ghi REG_LED
  uint16_t led_status = mb.Hreg(REG_LED);
  if (led_status == 1) {
    digitalWrite(LED_PIN, HIGH); // Bật đèn
  } else {
    digitalWrite(LED_PIN, LOW); // Tắt đèn
  }

  // Gửi dữ liệu Modbus RTU
  digitalWrite(RE_DE_PIN, HIGH);
  delay(2);           // Đảm bảo RE/DE đã bật
  mb.task();          // Gửi gói tin
  delay(2);           // Đảm bảo gói tin hoàn tất
  digitalWrite(RE_DE_PIN, LOW);  // Quay lại chế độ nhận

  delay(100);  // Thời gian giữa các lần gửi (polling)
}
