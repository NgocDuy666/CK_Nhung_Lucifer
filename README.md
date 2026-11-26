# Smart Environmental Monitoring and Control System (ESP32-C3 + FreeRTOS)

Đồ án Cuối kỳ Hệ thống Nhúng — **Nhóm Lucifer** (nâng cấp từ đồ án Giữa kỳ *Environmental Monitor System*).

## 1. Cấu trúc thư mục dự án

```text
CK_Nhung_Lucifer/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
└── main/
    ├── CMakeLists.txt
    ├── main.c                  # Khởi tạo RTOS objects, hardware, launch 7 tasks
    ├── core/
    │   ├── app_config.h        # Pinout GPIO, PWM 500 Hz, ngưỡng mặc định, Priority, Bitmasks
    │   ├── app_types.h         # Struct & Enum truyền dữ liệu giữa các task
    │   ├── app_context.h       # Quản lý tập trung toàn bộ FreeRTOS handles
    │   ├── app_state.h/.c      # Mutex bảo vệ config, NVS Flash, Mailbox (Overwrite/Peek), Toast
    ├── drivers/
    │   ├── sensors.h/.c        # DHT22 (One-wire + Critical Section) & LDR (12-bit ADC)
    │   ├── lcd_i2c.h/.c        # LCD 16x2 PCF8574 I2C (0x27 / 0x3F)
    │   ├── actuators.h/.c      # Quạt 24V PWM (LEDC 500 Hz), Đèn LED, Alarm LED, Buzzer
    │   └── buttons.h/.c        # GPIO ISR 3 nút nhấn (Binary Sem, Counting Sem, ISR Queue)
    └── tasks/
        ├── tasks.h             # Khai báo các task & timer
        ├── app_timers.c        # 5 Software Timers (debounce, ui_timeout, toast, mute, alarm_blink)
        ├── task_button.c       # Priority 6 - Deferred ISR Handler & Menu 6 trang
        ├── task_dht.c          # Priority 5 - Đọc DHT22 chu kỳ 2000 ms (vTaskDelayUntil)
        ├── task_light.c        # Priority 5 - Đọc LDR chu kỳ 500 ms (vTaskDelayUntil)
        ├── task_control.c      # Priority 4 - QueueSet, AUTO/MANUAL + Hysteresis, Event Group, Mailbox
        ├── task_alarm.c        # Priority 4 - Event Group WaitBits + Timer Task Notification
        ├── task_lcd.c          # Priority 3 - LCD Gatekeeper Task + Task Notification Wait
        └── task_cli.c          # Priority 2 - UART/USB Console CLI
```

## 2. Sơ đồ đấu nối phần cứng (ESP32-C3)

| Thiết bị | Chân ESP32-C3 | Lưu ý đấu nối |
| :--- | :---: | :--- |
| **LDR (AO)** | `GPIO3` | `ADC1_CH3` (giữ nguyên giữa kỳ) |
| **DHT22 (DATA)** | `GPIO4` | One-wire Open-Drain (giữ nguyên giữa kỳ) |
| **LCD 16x2 I2C** | `GPIO5` (SDA), `GPIO6` (SCL) | Địa chỉ `0x27` / `0x3F` (giữ nguyên giữa kỳ) |
| **Quạt 24V PWM** | `GPIO7` | Nối vào `ENA` của **L298N** *(tháo jumper `ENA`, nối cứng `IN1→5V/3.3V`, `IN2→GND`)* hoặc chân tín hiệu **MOSFET PWM**. **BẮT BUỘC tháo jumper `5V_EN` trên L298N khi cấp 24V**, cấp 5V riêng vào cọc `+5V` và **nối chung GND**! |
| **Đèn chiếu sáng (LED)** | `GPIO10` | Qua trở 220Ω → GND |
| **Đèn cảnh báo (LED Đỏ)** | `GPIO8` | Qua trở 220Ω → GND (hoặc LED onboard GPIO8) |
| **Buzzer (Module 3 chân)** | `GPIO2` | Sửa `#define BUZZER_PASSIVE_PWM` trong `app_config.h` (`1` = Passive, `0` = Active) |
| **Nút `BTN_MENU`** | `GPIO0` | Nối giữa `GPIO0` và `GND` (pull-up nội) |
| **Nút `BTN_UP`** | `GPIO1` | Nối giữa `GPIO1` và `GND` (pull-up nội) |
| **Nút `BTN_DOWN`** | `GPIO9` | Nối giữa `GPIO9` và `GND` (cùng chân nút `BOOT`; không giữ khi reset) |

## 3. Hướng dẫn sử dụng 3 nút nhấn & Menu LCD 16x2

- **Màn hình chính (`Page 0`):**
  - Bấm ngắn **`BTN_MENU`**: Vào menu cài đặt (`Page 1 → 6 → tự lưu & về Page 0`).
  - Nhấn giữ **`BTN_MENU` (≥ 1.2s)**: Đổi nhanh chế độ **`AUTO ↔ MANUAL`** (nếu đang ở trong Menu thì **Lưu NVS ngay & thoát về màn hình chính**).
  - Bấm **`BTN_UP` / `BTN_DOWN`**:
    - Khi có cảnh báo đang kêu → **Mute còi 30 giây** ngay lập tức (`xQueueSendToFront`).
    - Khi ở chế độ `MANUAL` → **Tăng / Giảm tốc độ quạt PWM trực tiếp** (`0 / 25 / 50 / 75 / 100%`).
- **6 trang cài đặt trong Menu:**
  1. `>Mode`: Đổi `AUTO ↔ MANUAL`
  2. `>Fan Speed`: Chỉnh tốc độ quạt `0..100%` (bước 25%)
  3. `>Lamp Ctrl`: Bật/tắt Đèn ở chế độ Manual
  4. `>Temp Warn`: Ngưỡng cảnh báo nhiệt `20..45°C` (`Crit = Warn + 5°C`)
  5. `>Hum High`: Ngưỡng độ ẩm cao `40..95%` (bước 5%)
  6. `>Light Low`: Ngưỡng thiếu sáng bật đèn `10..80%` (bước 5%)
  - Không bấm gì trong **10 giây** → tự động lưu cấu hình xuống **NVS Flash** và quay về Màn hình chính.

## 4. Biên dịch & Nạp bằng ESP-IDF

```bash
cd CK_Nhung_Lucifer
idf.py set-target esp32c3
idf.py build
idf.py -p <PORT> flash monitor
```

Trong cửa sổ `idf.py monitor`, gõ `help` hoặc `status` để xem thông số cảm biến, Event Group bits, Queue depth và điều khiển từ máy tính.
