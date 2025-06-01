import time
import sys
sys.path.insert(0, "/home/debian/pylibs")
import re
import os
import paho.mqtt.client as mqtt
import threading
import json

# Cấu hình MQTT Broker
BROKER = "192.168.7.1"
PORT = 1883
CONTROL_TOPIC = "led/control"
STATUS_TOPIC = "led/status"
SENSOR_TOPIC = "data/sensor"
CLIENT_ID = "beaglebone_led_dht11_client"

LED_DEVICE_FILE = "/dev/led0"
DHT11_DEVICE_FILE = "/dev/dht11"

# Biến lưu trạng thái LED mong muốn
desired_led_state = '0'

def on_connect(client, userdata, flags, rc):
    """Callback khi kết nối tới MQTT broker."""
    if rc == 0:
        print(f"Kết nối tới MQTT Broker thành công với mã {rc}")
        client.subscribe(CONTROL_TOPIC)
        print(f"Đã đăng ký topic: {CONTROL_TOPIC}")
    else:
        print(f"Kết nối tới MQTT Broker thất bại với mã {rc}")

def on_message(client, userdata, msg):
    """Callback khi nhận được tin nhắn từ topic điều khiển LED."""
    global desired_led_state
    try:
        command = msg.payload.decode().strip()
        print(f"Nhận được tin nhắn trên {msg.topic}: {command}")
        if command not in ['0', '1']:
            print(f"Lệnh không hợp lệ: {command}")
            return
        with open(LED_DEVICE_FILE, 'w') as f:
            f.write(command)
            print(f"Đã ghi lệnh '{command}' vào {LED_DEVICE_FILE}")
        with open(LED_DEVICE_FILE, 'r') as f:
            state = f.read(1)
            print(f"Trạng thái LED: {state}")
        desired_led_state = command
        client.publish(STATUS_TOPIC, state)
        print(f"Đã gửi trạng thái LED '{state}' tới {STATUS_TOPIC}")
    except Exception as e:
        print(f"Lỗi khi xử lý tin nhắn: {e}")

def read_dht11():
    """Đọc nhiệt độ, độ ẩm và checksum từ /dev/dht11."""
    try:
        start_time = time.time()
        with open(DHT11_DEVICE_FILE, 'r') as f:
            data = f.read()
        print(f"Dữ liệu thô từ DHT11: {data}")
        hum_match = re.search(r"Độ ẩm:\s*(\d+\.\d+)%", data)
        temp_match = re.search(r"Nhiệt độ:\s*(\d+\.\d+)°C", data)
        checksum_match = re.search(r"Checksum:\s*0x([0-9A-Fa-f]{2})", data)
        if hum_match and temp_match and checksum_match:
            humidity = float(hum_match.group(1))
            temperature = float(temp_match.group(1))
            checksum = int(checksum_match.group(1), 16)
            # Kiểm tra phạm vi hợp lệ
            if humidity > 100 or temperature > 50:
                print(f"Lỗi: Giá trị không hợp lệ (Độ ẩm: {humidity}, Nhiệt độ: {temperature})")
                return None
            print(f"Thời gian đọc DHT11: {time.time() - start_time:.3f} giây")
            return {
                "humidity": humidity,
                "temperature": temperature,
                "checksum": checksum
            }
        else:
            print(f"Lỗi: Không thể phân tích dữ liệu DHT11: {data}")
            return None
    except Exception as e:
        print(f"Lỗi khi đọc dữ liệu từ {DHT11_DEVICE_FILE}: {e}")
        return None

def sensor_thread(client):
    """Luồng riêng để đọc và gửi dữ liệu cảm biến."""
    global desired_led_state
    while True:
        for attempt in range(3):  # Thử đọc tối đa 3 lần
            data = read_dht11()
            if data:
                message = (f"humidity: {data['humidity']:.1f}\n"
                          f"temperature: {data['temperature']:.1f}\n"
                          f"Checksum: 0x{data['checksum']:02X}")
                client.publish(SENSOR_TOPIC, message)
                print(f"Đã gửi dữ liệu cảm biến tới {SENSOR_TOPIC}:\n{message}")
                break
            else:
                print(f"Thử đọc lần {attempt+1} thất bại")
                time.sleep(2)  # Chờ trước khi thử lại
        else:
            print("Không thể đọc dữ liệu cảm biến sau 3 lần thử")

        # Kiểm tra và khôi phục trạng thái LED
        try:
            with open(LED_DEVICE_FILE, 'r') as f:
                current_state = f.read(1)
                if current_state != desired_led_state:
                    print(f"Cảnh báo: Trạng thái LED hiện tại ({current_state}) khác mong muốn ({desired_led_state})")
                    with open(LED_DEVICE_FILE, 'w') as f:
                        f.write(desired_led_state)
                        print(f"Đã khôi phục trạng thái LED: {desired_led_state}")
        except Exception as e:
            print(f"Lỗi khi kiểm tra trạng thái LED: {e}")

        time.sleep(10)  # Gửi dữ liệu mỗi 10 giây

def main():
    """Hàm chính để khởi động chương trình."""
    if not os.path.exists(LED_DEVICE_FILE):
        print(f"File thiết bị {LED_DEVICE_FILE} không tồn tại.")
        return
    if not os.path.exists(DHT11_DEVICE_FILE):
        print(f"File thiết bị {DHT11_DEVICE_FILE} không tồn tại.")
        return

    client = mqtt.Client(client_id=CLIENT_ID)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(BROKER, PORT, keepalive=60)
        print(f"Đang kết nối tới MQTT Broker tại {BROKER}:{PORT}")
        client.loop_start()

        # Khởi động luồng riêng cho cảm biến
        threading.Thread(target=sensor_thread, args=(client,), daemon=True).start()

        while True:
            time.sleep(1)

    except KeyboardInterrupt:
        print("Chương trình bị gián đoạn bởi người dùng")
    except Exception as e:
        print(f"Lỗi: {e}")
    finally:
        client.loop_stop()
        client.disconnect()
        print("Đã ngắt kết nối khỏi MQTT Broker")

if __name__ == "__main__":
    main()

