from pymodbus.server import StartTcpServer
from pymodbus.datastore import ModbusSequentialDataBlock, ModbusSlaveContext, ModbusServerContext

# Hàm khởi tạo dữ liệu cho server
def setup_server():
    # Tạo khối dữ liệu cho các thanh ghi (holding registers)
    # Ví dụ: 10 thanh ghi bắt đầu từ địa chỉ 0, giá trị ban đầu là 0
    store = ModbusSlaveContext(
        hr=ModbusSequentialDataBlock(0, [0] * 10)  # Holding registers (đọc/ghi)
    )
    
    # Tạo context cho server với ID thiết bị (unit_id) là 1
    context = ModbusServerContext(slaves={1: store}, single=False)
    
    # Khởi động server Modbus TCP tại localhost:502
    print("Khởi động server Modbus TCP tại localhost:502...")
    StartTcpServer(context=context, address=("localhost", 502))

if __name__ == "__main__":
    setup_server()
