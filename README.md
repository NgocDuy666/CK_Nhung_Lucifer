# Smart Environmental Monitoring and Control System (ESP32-C3 + FreeRTOS)

Đồ án Cuối kỳ Hệ thống Nhúng — **Nhóm Lucifer** (nâng cấp từ đồ án Giữa kỳ *Environmental Monitor System* trên **ESP32-C3 + ESP-IDF ≥ 5.2 + FreeRTOS**).

---

## 1. Cấu trúc mã nguồn đa file

```text
CK_Nhung_Lucifer/
├── CMakeLists.txt
├── sdkconfig.defaults          # Cấu hình ESP32-C3, Tick 1000 Hz, Queue Sets, Timer Stack 3072B
├── README.md
└── main/
    ├── CMakeLists.txt          # Đăng ký đầy đủ 14 file nguồn .c và dependencies ESP-IDF
    ├── main.c                  # Khởi tạo RTOS objects, NVS, phần cứng & launch 7 tasks
    ├── core/
    │   ├── app_config.h        # Pinout GPIO, PWM 500 Hz, ngưỡng mặc định, Priority, Bitmasks
    │   ├── app_types.h         # Struct & Enum truyền dữ liệu giữa các task
    │   ├── app_context.h       # Quản lý tập trung toàn bộ FreeRTOS handles
    │   └── app_state.h/.c      # Mutex bảo vệ config, NVS Flash, Mailbox (Overwrite/Peek), Toast
    ├── drivers/
    │   ├── sensors.h/.c        # DHT22 (One-wire + Critical Section) & LDR (12-bit ADC)
    │   ├── lcd_i2c.h/.c        # LCD 16x2 PCF8574 I2C + I2C 9-clock bus recovery & scan
    │   ├── actuators.h/.c      # L298N PWM Fan (500 Hz), Lamp, Alarm LED, Buzzer 2 chân
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

---

## 2. Sơ đồ đấu nối phần cứng hoàn chỉnh (ESP32-C3 + L298N 24V)

| Thiết bị | Chân ESP32-C3 | Lưu ý đấu nối thực tế |
| :--- | :---: | :--- |
| **LDR (AO)** | `GPIO3` | `ADC1_CH3` (12-bit ADC, giữ nguyên giữa kỳ) |
| **DHT22 (DATA)** | `GPIO4` | One-wire Open-Drain + Pull-up nội (giữ nguyên giữa kỳ) |
| **LCD 16x2 I2C** | `GPIO5` (SDA), `GPIO6` (SCL) | Địa chỉ `0x27` / `0x3F`, cấp nguồn **5V VCC** & chung GND (tự phục hồi bus I2C nếu kẹt sau reset) |
| **Quạt 24V (L298N Kênh A)** | `GPIO7` | Tháo jumper `ENA` → cắm `GPIO7` vào **`ENA`** (PWM 500 Hz, 0–100%). Nối cứng **`IN1 → +5V`**, **`IN2 → GND`**. Quạt 24V nối **`OUT1 / OUT2`** |
| **Đèn chiếu sáng (L298N Kênh B / LED)** | `GPIO10` | • **Nếu đèn DC 24V qua L298N:** Tháo jumper `ENB` → cắm `GPIO10` vào **`ENB`**, nối cứng **`IN3 → +5V`**, **`IN4 → GND`**, đèn 24V vào **`OUT3 (+) / OUT4 (-)`**<br>• **Nếu LED 5mm (3V):** Cắm trực tiếp `GPIO10 → trở 220Ω → LED (+) → LED (-) → GND` *(KHÔNG cắm LED 5mm vào L298N 24V)* |
| **LED Đỏ cảnh báo (Alarm)** | `GPIO8` | Qua trở `220Ω` → GND *(hoặc dùng LED onboard GPIO8)* |
| **Buzzer 2 chân (`+` / `-`)** | `GPIO0` | Chân dài (`+`) → `GPIO0` *(nên qua trở 100Ω)*, chân ngắn (`-`) → `GND`. Mặc định `BUZZER_PASSIVE_PWM 0` (Active); nếu là Passive đổi thành `1` trong `app_config.h` |
| **Nút `BTN_MENU`** | `GPIO2` | Nối giữa `GPIO2` và `GND` (pull-up nội, trạng thái nghỉ = HIGH nên an toàn cho strapping pin lúc boot) |
| **Nút `BTN_UP`** | `GPIO1` | Nối giữa `GPIO1` và `GND` (pull-up nội) |
| **Nút `BTN_DOWN`** | `GPIO9` | Nối giữa `GPIO9` và `GND` (trùng chân nút `BOOT`; không giữ lúc reset) |

> **Cảnh báo an toàn L298N với nguồn tổ ong 24V:**
> 1. **BẮT BUỘC tháo jumper `5V_EN`** trên L298N trước khi cấp 24V vào cọc `+12V (VMS)` để tránh cháy IC ổn áp 78M05.
> 2. Cấp nguồn `5V` từ ESP32-C3 vào cọc `+5V` của L298N để nuôi mạch logic.
> 3. **Nối chung `GND`** giữa nguồn tổ ong 24V, L298N và ESP32-C3.

---

## 3. Giao diện LCD 16x2, Nút nhấn & Quy tắc Cảnh báo

### Ký hiệu trên màn hình chính (`Page 0`)
- Hàng 1: `T:25.3 H:86% [A]` — Nhiệt độ (`T`), Độ ẩm (`H`), Chế độ (`[A]` = AUTO, `[M]` = MANUAL).
- Hàng 2: `L:62% F: 75% P:1` — Ánh sáng LDR (`L`), Tốc độ quạt PWM (`F`), Đèn chiếu sáng (`P`: `1` = ON, `0` = OFF; hiển thị `MUT` khi đang tắt còi tạm thời, hoặc `!! CRITICAL !!` khi báo động mức nguy hiểm).

### Thao tác 3 nút nhấn (`MENU`, `UP`, `DOWN`)
- **Tại màn hình chính (`Page 0`):**
  - Nhấn nhả nhanh **`BTN_MENU (GPIO2)`**: Vào Menu cài đặt (`Page 1 → 6`).
  - Nhấn giữ **`BTN_MENU` (≥ 1.5s)**: Đổi nhanh chế độ **`AUTO ↔ MANUAL`**.
  - Nhấn **`BTN_UP (GPIO1)` / `BTN_DOWN (GPIO9)`**:
    - Khi còi đang kêu → **Mute còi 30 giây** ngay lập tức (`xQueueSendToFront`).
    - Khi ở chế độ `MANUAL` → **Tăng / Giảm tốc độ quạt PWM trực tiếp** (`0 / 25 / 50 / 75 / 100%`).
- **6 trang cài đặt trong Menu:**
  1. `>Mode`: Đổi `AUTO ↔ MANUAL`
  2. `>Fan Speed`: Chỉnh tốc độ quạt `0..100%` (bước 25%)
  3. `>Lamp Ctrl`: Bật/tắt Đèn ở chế độ Manual
  4. `>Temp Warn`: Ngưỡng nhiệt độ cảnh báo `20..45°C` (mặc định `30°C`, `Crit = Warn + 5°C`)
  5. `>Hum High`: Ngưỡng độ ẩm cao `40..99%` (mặc định `92%`)
  6. `>Light Low`: Ngưỡng thiếu sáng bật đèn `10..80%` (mặc định `40%`)
- Nhấn giữ **`BTN_MENU` (≥ 1.5s)** trong Menu (hoặc đi hết trang 6 / chờ timeout) sẽ lưu cấu hình vào **NVS Flash** và quay về màn hình chính.

### Quy tắc Còi Buzzer & Đèn cảnh báo
- **WARNING** *(LED đỏ nháy 1s, còi bíp ngắn 120 ms mỗi giây)*:
  - `T ≥ Temp Warn` (mặc định `30.0°C`, hysteresis `1.5°C`), **HOẶC**
  - `H ≥ Hum High` (mặc định `92.0%`, hysteresis `5%`), **HOẶC**
  - Lỗi đọc DHT22 `≥ 3` lần liên tiếp (`EVT_SENSOR_ERR`).
- **CRITICAL** *(LED đỏ + còi hú nhanh chu kỳ `250 ms`, quạt tự lên `100%` ở AUTO)*:
  - `T ≥ Temp Crit` (mặc định `35.0°C`), **HOẶC**
  - Sốc nhiệt (*Heat-Stress*): đồng thời `T ≥ Temp Warn` **VÀ** `H ≥ Hum High`.

---

## 4. Ánh xạ đầy đủ kiến thức FreeRTOS (Ch. 4 → Ch. 10)

| Cơ chế FreeRTOS | API chính sử dụng | Vị trí trong mã nguồn |
| :--- | :--- | :--- |
| **Tasks** (Ch. 4) | `xTaskCreate`, `vTaskDelayUntil`, `vTaskDelay`, `uxTaskPriorityGet` | `main.c`, `main/tasks/task_*.c` (7 tasks, Priority 2→6) |
| **Queues, Queue Set & Mailbox** (Ch. 5) | `xQueueCreate`, `xQueueSendToBack`, `xQueueSendToFront`, `xQueueReceive`, `uxQueueMessagesWaiting`, `xQueueCreateSet`, `xQueueAddToSet`, `xQueueSelectFromSet`, `xQueueOverwrite`, `xQueuePeek` | `main.c`, `core/app_state.c`, `tasks/task_control.c`, `tasks/task_button.c` |
| **Software Timers** (Ch. 6) | `xTimerCreate`, `xTimerReset`, `xTimerStop`, `xTimerChangePeriod`, `vTimerSetTimerID`, `pvTimerGetTimerID` | `tasks/app_timers.c` (5 timers: debounce, ui_timeout, toast, mute, alarm_blink) |
| **Interrupts & Semaphores** (Ch. 7) | `xSemaphoreCreateBinary`, `xSemaphoreCreateCounting`, `xSemaphoreGiveFromISR`, `xQueueSendToBackFromISR`, `xTimerPendFunctionCallFromISR`, `portYIELD_FROM_ISR` | `drivers/buttons.c`, `tasks/task_button.c` |
| **Resource Management** (Ch. 8) | `portENTER_CRITICAL`/`portEXIT_CRITICAL` (DHT22), `xSemaphoreCreateMutex` (`config_mutex`), **Gatekeeper Task** (`lcd_gatekeeper_task`) | `drivers/sensors.c`, `core/app_state.c`, `tasks/task_lcd.c` |
| **Event Groups** (Ch. 9) | `xEventGroupCreate`, `xEventGroupSetBits`, `xEventGroupClearBits`, `xEventGroupWaitBits` (OR + AND check), `xEventGroupSync` (Startup Barrier) | `tasks/task_control.c`, `tasks/task_alarm.c`, `tasks/task_dht.c`, `tasks/task_light.c`, `tasks/task_lcd.c` |
| **Task Notifications** (Ch. 10) | `xTaskNotify`, `xTaskNotifyWait`, `xTaskNotifyGive`, `ulTaskNotifyTake` | `tasks/task_lcd.c`, `tasks/task_alarm.c`, `tasks/app_timers.c`, `tasks/task_control.c` |

---

## 5. Biên dịch, Nạp & Lệnh Serial Monitor (ESP-IDF)

```bash
git clone https://github.com/NgocDuy666/CK_Nhung_Lucifer.git
cd CK_Nhung_Lucifer
idf.py set-target esp32c3
idf.py build
idf.py -p <PORT> flash monitor
```

Các lệnh hỗ trợ trực tiếp trong `idf.py monitor`:
- `status` — xem cảm biến, tốc độ quạt PWM, đèn, ngưỡng, Event Group bits, Queue depth & số lần ISR.
- `mode auto | manual` — chuyển chế độ hoạt động.
- `fan <0..100>` — chỉnh tốc độ quạt PWM (chuyển sang Manual).
- `lamp on | off` — bật/tắt đèn (chuyển sang Manual).
- `set temp <warn> [crit]` / `set hum <high>` / `set light <low>` — chỉnh ngưỡng động.
- `mute` / `save` / `reset` / `help`.
