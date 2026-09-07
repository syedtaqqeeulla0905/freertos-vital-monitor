#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ====================================================
// Hardware Pin Definitions
// ====================================================
#define PIN_DHT          15   // DHT22 Temperature & Humidity Sensor
#define PIN_PULSE_POT    34   // Potentiometer simulating Heart Rate (ADC1_CH6)
#define PIN_SOS_BTN      4    // Emergency Push Button (Hardware ISR Trigger)
#define PIN_BUZZER       25   // Piezo Alarm Buzzer
#define PIN_LED_ALARM    26   // Red Emergency Warning LED
#define DHTTYPE          DHT22

#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT    64

// Display & Sensor Instantiations
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(PIN_DHT, DHTTYPE);

// ====================================================
// FreeRTOS Data Structs & Synchronization Primitives
// ====================================================
struct VitalData {
  float temperature;
  float humidity;
  int heartRate;
  bool isAlert;
};

// FreeRTOS Handles
QueueHandle_t      xSensorQueue;   // Thread-safe FIFO Queue for VitalData structs
SemaphoreHandle_t  xSOSSemaphore;  // Binary Semaphore given by Hardware ISR
SemaphoreHandle_t  xI2CMutex;      // Mutex to protect shared I2C bus (SSD1306 display)

// ====================================================
// Hardware Interrupt Service Routine (ISR)
// ====================================================
// Triggered on GPIO 4 Falling Edge when SOS Button is pressed
void IRAM_ATTR sosButtonISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  // Unblock Emergency Task from ISR safely
  xSemaphoreGiveFromISR(xSOSSemaphore, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ====================================================
// Task 1: Emergency SOS Handler (Priority 4 - Highest)
// ====================================================
void TaskEmergencyAlert(void *pvParameters) {
  for (;;) {
    // Block indefinitely until Binary Semaphore is given by Hardware ISR
    if (xSemaphoreTake(xSOSSemaphore, portMAX_DELAY) == pdTRUE) {
      Serial.println("[ISR EMERGENCY] SOS Alarm Triggered!");
      
      // Execute 5x Emergency Flashing & Beeping Alarm Sequence
      for (int i = 0; i < 5; i++) {
        digitalWrite(PIN_LED_ALARM, HIGH);
        digitalWrite(PIN_BUZZER, HIGH);
        vTaskDelay(pdMS_TO_TICKS(100)); // Sleep 100ms without blocking CPU
        digitalWrite(PIN_LED_ALARM, LOW);
        digitalWrite(PIN_BUZZER, LOW);
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
  }
}

// ====================================================
// Task 2: Sensor Sampling Task (Priority 2 - Medium)
// ====================================================
void TaskSensorRead(void *pvParameters) {
  for (;;) {
    VitalData data;
    
    // 1. Read DHT22 Sensor
    data.temperature = dht.readTemperature();
    data.humidity = dht.readHumidity();
    
    // 2. Sample ADC & Map Potentiometer value (0-4095) to Heart Rate (40-160 BPM)
    int rawAdc = analogRead(PIN_PULSE_POT);
    data.heartRate = map(rawAdc, 0, 4095, 40, 160);
    
    // 3. Threshold Check (Fever > 38C, Abnormal Heart Rate < 50 or > 120 BPM)
    data.isAlert = (data.heartRate > 120 || data.heartRate < 50 || data.temperature > 38.0);

    // 4. Push data struct to FreeRTOS Queue (Non-blocking write)
    xQueueSend(xSensorQueue, &data, pdMS_TO_TICKS(100));

    // 5. Yield execution for 1000ms
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ====================================================
// Task 3: Data Processing & OLED Display Task (Priority 1)
// ====================================================
void TaskOLEDUpdate(void *pvParameters) {
  VitalData currentVitals;
  for (;;) {
    // Wait for incoming sensor packet from Queue
    if (xQueueReceive(xSensorQueue, &currentVitals, portMAX_DELAY) == pdTRUE) {
      
      // Acquire I2C Mutex before accessing shared SSD1306 Display
      if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("--- VITAL MONITOR ---");
        
        display.setCursor(0, 18);
        display.print("Heart Rate : ");
        display.print(currentVitals.heartRate);
        display.println(" BPM");

        display.setCursor(0, 32);
        display.print("Temp       : ");
        display.print(currentVitals.temperature, 1);
        display.println(" C");

        display.setCursor(0, 50);
        if (currentVitals.isAlert) {
          display.println("STATUS     : WARNING!");
          digitalWrite(PIN_LED_ALARM, HIGH);
        } else {
          display.println("STATUS     : NORMAL");
          digitalWrite(PIN_LED_ALARM, LOW);
        }
        display.display();
        
        // Release I2C Mutex for other tasks
        xSemaphoreGive(xI2CMutex);
      }
    }
  }
}

// ====================================================
// System Setup & FreeRTOS Kernel Initialization
// ====================================================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_SOS_BTN, INPUT_PULLUP);
  pinMode(PIN_LED_ALARM, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  dht.begin();
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 OLED Initialization Failed!");
  }

  // 1. Initialize FreeRTOS Queues, Mutexes, and Binary Semaphores
  xSensorQueue  = xQueueCreate(5, sizeof(VitalData));
  xSOSSemaphore = xSemaphoreCreateBinary();
  xI2CMutex     = xSemaphoreCreateMutex();

  // 2. Attach Hardware Interrupt on GPIO 4 Falling Edge
  attachInterrupt(digitalPinToInterrupt(PIN_SOS_BTN), sosButtonISR, FALLING);

  // 3. Register & Launch FreeRTOS Tasks with Specific Priorities
  xTaskCreate(TaskEmergencyAlert, "EmergencyTask", 2048, NULL, 4, NULL); // Priority 4 (Highest)
  xTaskCreate(TaskSensorRead,     "SensorReadTask", 2048, NULL, 2, NULL); // Priority 2 (Medium)
  xTaskCreate(TaskOLEDUpdate,     "OLEDTask",       3072, NULL, 1, NULL); // Priority 1 (Normal)
}

void loop() {
  // Empty: FreeRTOS Scheduler handles all task execution
}
