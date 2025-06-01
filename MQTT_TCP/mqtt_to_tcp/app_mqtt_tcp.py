import json
from pymodbus.client import ModbusTcpClient
from pymodbus.exceptions import ModbusException
import paho.mqtt.client as mqtt
import time

class ModbusWriter:
    def __init__(self, ip_address="localhost", port=502, slave_id=1):
        """Khởi tạo client Modbus TCP với các thông số cơ bản."""
        self.ip_address = ip_address
        self.port = port
        self.slave_id = slave_id
        self.client = None

    def connect(self):
        """Kết nối tới server Modbus TCP."""
        try:
            self.client = ModbusTcpClient(self.ip_address, port=self.port)
            if self.client.connect():
                print(f"Kết nối thành công tới {self.ip_address}:{self.port}")
                return True
            else:
                print("Không thể kết nối tới server Modbus!")
                return False
        except Exception as e:
            print(f"Lỗi kết nối: {e}")
            return False

    def disconnect(self):
        """Ngắt kết nối từ server Modbus TCP."""
        if self.client:
            self.client.close()
            print("Đã ngắt kết nối từ server Modbus.")
            self.client = None

    def write_register(self, address, value):
        """Ghi giá trị vào một thanh ghi cụ thể."""
        if not self.client or not self.client.is_socket_open():
            if not self.connect():
                return False
        try:
            result = self.client.write_register(address=address, value=value, slave=self.slave_id)
            if not result.isError():
                print(f"Ghi giá trị {value} vào thanh ghi {address} thành công!")
                return True
            else:
                print(f"Lỗi khi ghi vào thanh ghi {address}: {result}")
                return False
        except ModbusException as e:
            print(f"Lỗi Modbus: {e}")
            return False

    def read_register(self, address):
        """Đọc giá trị từ một thanh ghi cụ thể."""
        if not self.client or not self.client.is_socket_open():
            if not self.connect():
                return None
        try:
            result = self.client.read_holding_registers(address=address, count=1, slave=self.slave_id)
            if not result.isError():
                value = result.registers[0]
                print(f"Đọc giá trị {value} từ thanh ghi {address} thành công!")
                return value
            else:
                print(f"Lỗi khi đọc thanh ghi {address}: {result}")
                return None
        except ModbusException as e:
            print(f"Lỗi Modbus khi đọc: {e}")
            return None

    def update_registers(self, temperature, humidity):
        """Cập nhật giá trị cho thanh ghi 4 (temperature) và 5 (humidity)."""
        temp_int = int(temperature)  # Đã là int từ parsing
        hum_int = int(humidity)      # Đã là int từ parsing
        success_r4 = self.write_register(4, temp_int)  # Ghi temperature vào Register 4
        success_r5 = self.write_register(5, hum_int)   # Ghi humidity vào Register 5
        return success_r4 and success_r5

class MQTTModbusBridge:
    def __init__(self, mqtt_broker="localhost", mqtt_port=1883, mqtt_topic="data/sensor", led_topic="led/control"):
        """Khởi tạo cầu nối giữa MQTT và Modbus."""
        self.modbus_writer = ModbusWriter()
        self.mqtt_broker = mqtt_broker
        self.mqtt_port = mqtt_port
        self.mqtt_topic = mqtt_topic
        self.led_topic = led_topic
        self.mqtt_client = mqtt.Client()
        self.last_led_value = None  # Lưu giá trị trước đó của thanh ghi 6
        # Gán callback cho MQTT
        self.mqtt_client.on_connect = self.on_connect
        self.mqtt_client.on_message = self.on_message

    def on_connect(self, client, userdata, flags, rc):
        """Callback khi kết nối tới MQTT broker."""
        if rc == 0:
            print(f"Kết nối thành công tới MQTT broker {self.mqtt_broker}:{self.mqtt_port}")
            client.subscribe(self.mqtt_topic)
        else:
            print("Kết nối tới MQTT broker thất bại, mã lỗi:", rc)

    def on_message(self, client, userdata, msg):
        """Callback khi nhận được tin nhắn từ MQTT."""
        try:
            payload = msg.payload.decode("utf-8").strip()
            print(f"Nhận dữ liệu từ topic {msg.topic}: \n{payload}")
            
            # Parse payload từng dòng
            lines = payload.splitlines()
            data = {}
            for line in lines:
                if ':' in line:
                    key, val = line.split(':', 1)
                    key = key.strip().lower()
                    val = val.strip()
                    data[key] = val
            
            # Lấy giá trị humidity và temperature
            if "humidity" not in data or "temperature" not in data:
                raise ValueError("Payload thiếu trường 'humidity' hoặc 'temperature'")
            
            humidity = float(data["humidity"])
            temperature = float(data["temperature"])
            
            if self.modbus_writer.update_registers(temperature, humidity):
                print("Cập nhật thanh ghi từ dữ liệu MQTT thành công!")
                # Đọc giá trị thanh ghi 6 (LED)
                led_value = self.modbus_writer.read_register(6)
                if led_value is not None:
                    # Chỉ gửi bản tin nếu giá trị thay đổi hoặc lần đầu đọc
                    if led_value != self.last_led_value:
                        # Định dạng payload thành led:<value>
                        led_payload = f"{led_value}"
                        client.publish(self.led_topic, led_payload, qos=1)
                        print(f"Gửi giá trị LED {led_payload} tới topic {self.led_topic}")
                        self.last_led_value = led_value
                    else:
                        print(f"Giá trị LED {led_value} không thay đổi, không gửi MQTT.")
                else:
                    print("Lỗi khi đọc thanh ghi LED (6)")
            else:
                print("Lỗi khi cập nhật thanh ghi từ dữ liệu MQTT.")
        except ValueError as e:
            print(f"Lỗi phân tích payload: {e}")
        except Exception as e:
            print(f"Lỗi xử lý tin nhắn MQTT: {e}")

    def start(self):
        """Khởi động cầu nối MQTT-Modbus."""
        self.mqtt_client.connect(self.mqtt_broker, self.mqtt_port)
        self.mqtt_client.loop_start()
        print(f"Đang lắng nghe topic {self.mqtt_topic} trên {self.mqtt_broker}:{self.mqtt_port}...")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            self.stop()

    def stop(self):
        """Dừng cầu nối MQTT-Modbus."""
        self.mqtt_client.loop_stop()
        self.mqtt_client.disconnect()
        self.modbus_writer.disconnect()
        print("Đã dừng cầu nối MQTT-Modbus.")


if __name__ == "__main__":
    bridge = MQTTModbusBridge(mqtt_broker="localhost", mqtt_port=1883, mqtt_topic="data/sensor", led_topic="led/control")
    bridge.start()
