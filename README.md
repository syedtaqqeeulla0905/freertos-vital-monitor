# Medical Vital Signs Monitor (FreeRTOS + ESP32)

A real-time medical patient monitoring system built on ESP32 running **FreeRTOS**. Implements multi-tasking, hardware interrupt handling, thread-safe queues, mutexes, binary semaphores, and live SSD1306 OLED visualization.

---

## 🛠️ FreeRTOS Architecture & Primitives

| Task Name | Priority | Primitive Used | Function Description |
|---|---|---|---|
| `TaskEmergencyAlert` | Priority 4 (Highest) | `Binary Semaphore` | Triggered via Hardware ISR on GPIO 4 when SOS Emergency button is pressed. Activates alarm sequence. |
| `TaskSensorRead` | Priority 2 (Medium) | `Queue (xSensorQueue)` | Samples DHT22 temperature & ADC potentiometer pulse every 1000ms. Pushes sensor struct to Queue. |
| `TaskOLEDUpdate` | Priority 1 (Normal) | `Mutex (xI2CMutex)` | Receives struct from Queue. Acquires I2C Mutex and updates SSD1306 OLED display with live vitals. |

---

## 🔌 Hardware Setup & Components

- **Microcontroller**: ESP32 DevKit v1
- **Temperature & Humidity**: DHT22 Sensor (GPIO 15)
- **Pulse Sensor Simulator**: Potentiometer (ADC GPIO 34)
- **Emergency Alert**: Push Button (GPIO 4 with Hardware ISR)
- **Display**: SSD1306 OLED 128x64 I2C (SDA: GPIO 21, SCL: GPIO 22)
- **Indicators**: Red Alarm LED (GPIO 26) & Piezo Buzzer (GPIO 25)

---

## 💻 Simulation & Step-by-Step Instructions

To run this simulation on **Wokwi**:

1. Open [Wokwi.com ESP32 Project](https://wokwi.com/projects/new/esp32)
2. Copy `diagram.json` into Wokwi's `diagram.json` tab.
3. Copy `src/main.cpp` into Wokwi's `sketch.ino` tab.
4. Click **Play ▶️** to run the FreeRTOS kernel in real-time!

---

## 📜 License

Distributed under the MIT License. See `LICENSE` for details.
