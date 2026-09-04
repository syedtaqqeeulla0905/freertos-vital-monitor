#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// Pin Definitions
#define PIN_DHT          15
#define PIN_PULSE_POT    34
#define PIN_SOS_BTN      4
#define PIN_BUZZER       25
#define PIN_LED_ALARM    26
#define DHTTYPE          DHT22

#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT    64

// Display & Sensor Objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(PIN_DHT, DHTTYPE);

// FreeRTOS Data Struct
struct VitalData {
  float temperature;
  float humidity;
  int heartRate;
  bool isAlert;
};

// FreeRTOS Synchronization Primitives
QueueHandle_t      xSensorQueue;
SemaphoreHandle_t  xSOSSemaphore;
SemaphoreHandle_t  xI2CMutex;

// Hardware ISR for Emergency SOS Button
void IRAM_ATTR sosButtonISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(xSOSSemaphore, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ----------------------------------------------------
// Task 1: High-Priority Emergency SOS Handler
// ----------------------------------------------------
void TaskEmergencyAlert(void *pvParameters) {
  for (;;) {
    if (xSemaphoreTake(xSOSSemaphore, portMAX_DELAY) == pdTRUE) {
      Serial.println("[ISR EMERGENCY] SOS Button Pressed!");
      
      // Trigger Alarm Pattern
      for (int i = 0; i < 5; i++) {
        digitalWrite(PIN_LED_ALARM, HIGH);
        digitalWrite(PIN_BUZZER, HIGH);
        vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_LED_ALARM, LOW);
        digitalWrite(PIN_BUZZER, LOW);
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
  }
}

// ----------------------------------------------------
// Task 2: Sensor Sampling Task (Reads DHT22 & ADC)
// ----------------------------------------------------
void TaskSensorRead(void *pvParameters) {
  for (;;) {
    VitalData data;
    
    // Read Temperature & Humidity
    data.temperature = dht.readTemperature();
    data.humidity = dht.readHumidity();
    
    // Map ADC Potentiometer to Heart Rate (40 to 160 BPM)
    int rawAdc = analogRead(PIN_PULSE_POT);
    data.heartRate = map(rawAdc, 0, 4095, 40, 160);
    
    // Check threshold alert (Fever > 38C, Abnormal Pulse < 50 or > 120 BPM)
    data.isAlert = (data.heartRate > 120 || data.heartRate < 50 || data.temperature > 38.0);

    // Send struct to Queue
    xQueueSend(xSensorQueue, &data, pdMS_TO_TICKS(100));

    vTaskDelay(pdMS_TO_TICKS(1000)); // Sample every 1 sec
  }
}

// ----------------------------------------------------
// Task 3: Data Processing & OLED UI Task
// ----------------------------------------------------
void TaskOLEDUpdate(void *pvParameters) {
  VitalData currentVitals;
  for (;;) {
    if (xQueueReceive(xSensorQueue, &currentVitals, portMAX_DELAY) == pdTRUE) {
      
      // Thread-safe access to I2C OLED display using Mutex
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
        
        xSemaphoreGive(xI2CMutex);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SOS_BTN, INPUT_PULLUP);
  pinMode(PIN_LED_ALARM, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  dht.begin();
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 Allocation Failed");
  }

  // Create FreeRTOS Primitives
  xSensorQueue  = xQueueCreate(5, sizeof(VitalData));
  xSOSSemaphore = xSemaphoreCreateBinary();
  xI2CMutex     = xSemaphoreCreateMutex();

  // Attach Hardware Interrupt
  attachInterrupt(digitalPinToInterrupt(PIN_SOS_BTN), sosButtonISR, FALLING);

  // Create FreeRTOS Tasks
  xTaskCreate(TaskEmergencyAlert, "EmergencyTask", 2048, NULL, 4, NULL);
  xTaskCreate(TaskSensorRead,     "SensorReadTask", 2048, NULL, 2, NULL);
  xTaskCreate(TaskOLEDUpdate,     "OLEDTask",       3072, NULL, 1, NULL);
}

void loop() {
  // Empty - FreeRTOS handles task execution
}
