// ESP32 Code
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems

// UART Communication with WROVER
#define RX2_PIN 16
#define TX2_PIN 17

// I2C Pins for each sensor - using different physical I2C pins
#define I2C1_SDA 21  // For LCD
#define I2C1_SCL 22
#define I2C2_SDA 25  // For MPU6050
#define I2C2_SCL 26
#define I2C3_SDA 32  // For MAX30102
#define I2C3_SCL 33

// Other sensor pins
#define DHT_PIN 4
#define DHT_TYPE DHT22
#define MQ2_PIN 34          // Analog pin for MQ2
#define BUZZER_PIN 27
#define BUTTON_PIN 0        // Boot button

// Thresholds
#define FALL_THRESHOLD 2.5  // G-force threshold for fall detection
#define TEMP_HIGH 35.0      // High temperature threshold (°C)
#define TEMP_LOW 10.0       // Low temperature threshold (°C)
#define GAS_THRESHOLD 2000  // MQ2 gas threshold
#define HEART_RATE_LOW 50   // Low heart rate (BPM)
#define HEART_RATE_HIGH 120 // High heart rate (BPM)

// Create three separate I2C buses
TwoWire I2C_1 = TwoWire(0);  // LCD    ?? 2. COMMENT THIS 
TwoWire I2C_2 = TwoWire(1);  // MPU6050
TwoWire I2C_3 = TwoWire(2);  // MAX30102

// Create sensor objects
LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD address 0x27
Adafruit_MPU6050 mpu;
DHT dht(DHT_PIN, DHT_TYPE);
MAX30105 particleSensor;

// Sensor availability flags
bool mpuAvailable = false;
bool maxAvailable = false;
bool dhtAvailable = false;
bool lcdAvailable = false;  // Add LCD flag

// Sensor data variables
float temperature = 0;
float humidity = 0;
int gasLevel = 0;
float heartRate = 0;
float accelMagnitude = 0;
bool fallDetected = false;

// Alert flags
bool tempAlert = false;
bool gasAlert = false;
bool heartRateAlert = false;
bool fallAlert = false;

// Heart rate detection
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute;
int beatAvg;

// Timing
unsigned long lastSensorRead = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastHeartRateCheck = 0;
unsigned long lastDataSend = 0;
const unsigned long SENSOR_INTERVAL = 1000;     // Read sensors every 1 second
const unsigned long DISPLAY_INTERVAL = 1000;    // Update display every 1 second  
const unsigned long HEARTRATE_INTERVAL = 100;   // Check heart rate every 100ms
const unsigned long DATA_SEND_INTERVAL = 2000;  // Send data every 2 seconds (before each capture)

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MQ2_PIN, INPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  Serial.println("\n=== Miner Safety Helmet System ===");
  
  // Initialize LCD on I2C bus 1
  Serial.print("Initializing LCD... ");
  // I2C_1.begin(I2C1_SDA, I2C1_SCL, 100000);
  Wire.begin(I2C1_SDA, I2C1_SCL);
  delay(100);
  
  // Check if LCD exists
  Wire.beginTransmission(0x27);
  byte lcdError = Wire.endTransmission();
  
  if (lcdError != 0) {
    // Try alternate address 0x3F
    Wire.beginTransmission(0x3F);
    lcdError = Wire.endTransmission();
  }
  
  if (lcdError == 0) {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Helmet Init...");
    lcd.setCursor(0, 1);
    lcd.print("Please wait...");
    lcdAvailable = true;
    Serial.println("OK");
  } else {
    Serial.println("NOT FOUND");
    Serial.println("  Check: VCC->5V, GND->GND, SDA->21, SCL->22");
    lcdAvailable = false;
  }
  delay(1000);
  
  // Initialize MPU6050 on I2C bus 2
  Serial.print("Initializing MPU6050... ");
  I2C_2.begin(I2C2_SDA, I2C2_SCL, 400000);
  delay(200);  // Give power supply time to stabilize
  
  if (!mpu.begin(0x68, &I2C_2)) {
    Serial.println("NOT FOUND - Fall detection disabled");
    mpuAvailable = false;
  } else {
    Serial.println("OK");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    mpuAvailable = true;
  }
  
  // Initialize MAX30102 on I2C bus 3
  Serial.print("Initializing MAX30102... ");
  I2C_3.begin(I2C3_SDA, I2C3_SCL, 100000);  // Standard speed
  delay(300);  // Give power supply time to stabilize
  
  // SparkFun library syntax
  if (!particleSensor.begin(I2C_3, I2C_SPEED_STANDARD, 0x57)) {
    Serial.println("NOT FOUND - Heart rate disabled");
    Serial.println("  Check: 3.3V power, wiring");
    maxAvailable = false;
  } else {
    Serial.println("OK");
    
    // Configure sensor (SparkFun library) - REDUCED POWER
    byte ledBrightness = 0x0A; // Reduced from 0x1F (Options: 0=Off to 255=50mA)
    byte sampleAverage = 4;     // Options: 1, 2, 4, 8, 16, 32
    byte ledMode = 2;           // Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
    int sampleRate = 100;       // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
    int pulseWidth = 411;       // Options: 69, 118, 215, 411
    int adcRange = 4096;        // Options: 2048, 4096, 8192, 16384
    
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    particleSensor.setPulseAmplitudeRed(0x0A); // Low power for testing
    
    maxAvailable = true;
  }
  
  // Initialize DHT22
  Serial.print("Initializing DHT22... ");
  dht.begin();
  delay(2000);  // DHT needs time to stabilize
  temperature = dht.readTemperature();
  if (!isnan(temperature)) {
    Serial.println("OK");
    dhtAvailable = true;
  } else {
    Serial.println("NOT FOUND - Temperature disabled");
    dhtAvailable = false;
  }
  
  // Show sensor status on LCD
  if (lcdAvailable) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sensors:");
    lcd.setCursor(0, 1);
    if (mpuAvailable) lcd.print("M");
    if (maxAvailable) lcd.print("H");
    if (dhtAvailable) lcd.print("T");
    lcd.print(" OK");
    delay(1500);
  }
  
  // Wait for camera
  if (lcdAvailable) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Wait Camera...");
  }
  Serial.println("Waiting for camera...");
  
  unsigned long startTime = millis();
  while (!Serial2.available() && millis() - startTime < 15000) {
    delay(100);
  }
  
  if (Serial2.available()) {
    String ready = Serial2.readStringUntil('\n');
    if (ready.indexOf("CAM_READY") >= 0) {
      Serial.println("✓ Camera ready!");
      if (lcdAvailable) {
        lcd.setCursor(0, 1);
        lcd.print("Cam Ready!");
      }
      delay(1000);
    }
  }
  
  if (lcdAvailable) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("System Ready!");
  }
  delay(1000);
  
  Serial.println("=== All Systems Ready ===");
  Serial.println("Available sensors:");
  if (mpuAvailable) Serial.println("  - MPU6050 (Fall detection)");
  if (maxAvailable) Serial.println("  - MAX30102 (Heart rate)");
  if (dhtAvailable) Serial.println("  - DHT22 (Temperature/Humidity)");
  Serial.println("  - MQ2 (Gas sensor)");
  Serial.println();
  
  // Send initial sensor reading to WROVER
  delay(2000);
  Serial.println("Reading sensors for initial data...");
  readSensors();
  
  // Send initial data to WROVER
  String sensorData = "SENSOR_DATA:STARTUP|";
  sensorData += "TEMP:" + String(temperature, 1) + "|";
  sensorData += "HUM:" + String(humidity, 1) + "|";
  sensorData += "GAS:" + String(gasLevel) + "|";
  sensorData += "HR:" + String((int)heartRate) + "|";
  sensorData += "ACCEL:" + String(accelMagnitude, 2);
  
  Serial.println("Sending initial data to WROVER:");
  Serial.println(sensorData);
  Serial2.println(sensorData);
  delay(500);
}

void readSensors() {
  // Read temperature and humidity if available
  if (dhtAvailable) {
    float newTemp = dht.readTemperature();
    float newHum = dht.readHumidity();
    if (!isnan(newTemp)) temperature = newTemp;
    if (!isnan(newHum)) humidity = newHum;
    
    // Check for temperature alerts
    tempAlert = (temperature > TEMP_HIGH || temperature < TEMP_LOW);
  }
  
  // Read gas sensor (always available - analog)
  gasLevel = analogRead(MQ2_PIN);
  gasAlert = (gasLevel > GAS_THRESHOLD);
  
  // Read accelerometer for fall detection if available
  if (mpuAvailable) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    accelMagnitude = sqrt(a.acceleration.x * a.acceleration.x + 
                          a.acceleration.y * a.acceleration.y + 
                          a.acceleration.z * a.acceleration.z) / 9.81;
    
    // Check for fall
    if (accelMagnitude > FALL_THRESHOLD && !fallDetected) {
      fallDetected = true;
      fallAlert = true;
    }
  }
}

void readHeartRate() {
  if (!maxAvailable) return;
  
  long irValue = particleSensor.getIR();
  
  if (irValue > 50000) {  // Finger detected
    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      
      beatsPerMinute = 60 / (delta / 1000.0);
      
      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        
        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
        
        heartRate = beatAvg;
        heartRateAlert = (heartRate < HEART_RATE_LOW || heartRate > HEART_RATE_HIGH);
      }
    }
  } else {
    heartRate = 0;  // No finger detected
    // Turn off LEDs when no finger to save power
    static unsigned long lastNoFinger = 0;
    if (millis() - lastNoFinger > 5000) {  // After 5 seconds with no finger
      particleSensor.setPulseAmplitudeRed(0x00);  // Turn off
      lastNoFinger = millis();
    }
  }
  
  // Re-enable LEDs if finger detected
  if (irValue > 50000) {
    particleSensor.setPulseAmplitudeRed(0x0A);
  }
}

void updateDisplay() {
  if (!lcdAvailable) return;
  
  lcd.clear();
  
  // Priority: Show alerts first
  if (fallAlert) {
    lcd.setCursor(0, 0);
    lcd.print("! FALL ALERT !");
    lcd.setCursor(0, 1);
    lcd.print("Emergency!");
  } else if (gasAlert) {
    lcd.setCursor(0, 0);
    lcd.print("! GAS ALERT !");
    lcd.setCursor(0, 1);
    lcd.print("Gas:");
    lcd.print(gasLevel);
    lcd.print(" EVAC!");
  } else if (tempAlert) {
    lcd.setCursor(0, 0);
    lcd.print("! TEMP ALERT !");
    lcd.setCursor(0, 1);
    lcd.print("T:");
    lcd.print(temperature, 1);
    lcd.print("C ");
  } else if (heartRateAlert && heartRate > 0) {
    lcd.setCursor(0, 0);
    lcd.print("! HR ALERT !");
    lcd.setCursor(0, 1);
    lcd.print("BPM:");
    lcd.print((int)heartRate);
  } else {
    // Normal display - Line 1: Temp and Heart Rate
    lcd.setCursor(0, 0);
    if (dhtAvailable && !isnan(temperature)) {
      lcd.print("T:");
      lcd.print(temperature, 1);
      lcd.print("C");
    } else {
      lcd.print("T:--");
    }
    
    lcd.print(" ");
    
    if (maxAvailable && heartRate > 0) {
      lcd.print("HR:");
      lcd.print((int)heartRate);
    } else {
      lcd.print("HR:--");
    }
    
    // Line 2: Gas level and status
    lcd.setCursor(0, 1);
    lcd.print("Gas:");
    lcd.print(gasLevel);
    lcd.print(" ");
    
    // Status indicator
    if (mpuAvailable && maxAvailable && dhtAvailable) {
      lcd.print("OK");
    } else {
      lcd.print("CHK");
    }
  }
}

void triggerBuzzer(int beeps) {
  for (int i = 0; i < beeps; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
  }
}

void sendAlertToServer(String alertType) {
  // Build sensor data string
  String sensorData = "SENSOR_DATA:";
  sensorData += alertType + "|";
  sensorData += "TEMP:" + String(temperature, 1) + "|";
  sensorData += "HUM:" + String(humidity, 1) + "|";
  sensorData += "GAS:" + String(gasLevel) + "|";
  sensorData += "HR:" + String((int)heartRate) + "|";
  sensorData += "ACCEL:" + String(accelMagnitude, 2);
  
  Serial.println("Sending alert: " + alertType);
  Serial.println(sensorData);
  
  // Send to WROVER via UART
  Serial2.println(sensorData);
  delay(100);
  Serial2.println("CAPTURE");
}

void handleAlerts() {
  bool alertTriggered = false;
  String alertType = "";
  int buzzerBeeps = 0;
  
  if (fallAlert) {
    alertType = "FALL_DETECTED";
    buzzerBeeps = 5;
    alertTriggered = true;
    Serial.println("\n!!! FALL DETECTED !!!");
  } else if (gasAlert) {
    alertType = "GAS_ALERT";
    buzzerBeeps = 4;
    alertTriggered = true;
    Serial.println("\n!!! GAS ALERT !!!");
  } else if (tempAlert) {
    alertType = "TEMP_ALERT";
    buzzerBeeps = 3;
    alertTriggered = true;
    Serial.println("\n!!! TEMPERATURE ALERT !!!");
  } else if (heartRateAlert && heartRate > 0) {
    alertType = "HEART_RATE_ALERT";
    buzzerBeeps = 2;
    alertTriggered = true;
    Serial.println("\n!!! HEART RATE ALERT !!!");
  }
  
  if (alertTriggered) {
    triggerBuzzer(buzzerBeeps);
    sendAlertToServer(alertType);
    
    // Clear fall alert after sending
    if (fallAlert) {
      fallAlert = false;
      delay(5000);
      fallDetected = false;
    }
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Read sensors periodically
  if (currentMillis - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = currentMillis;
    readSensors();
    handleAlerts();
  }
  
  // Read heart rate more frequently
  if (currentMillis - lastHeartRateCheck >= HEARTRATE_INTERVAL) {
    lastHeartRateCheck = currentMillis;
    readHeartRate();
  }
  
  // Update display
  if (currentMillis - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = currentMillis;
    updateDisplay();
  }
  
  // Send sensor data periodically to WROVER (every 2 seconds)
  if (currentMillis - lastDataSend >= DATA_SEND_INTERVAL) {
    lastDataSend = currentMillis;
    
    // Build comprehensive sensor data string
    String sensorData = "SENSOR_DATA:PERIODIC|";
    sensorData += "TEMP:" + String(temperature, 1) + "|";
    sensorData += "HUM:" + String(humidity, 1) + "|";
    sensorData += "GAS:" + String(gasLevel) + "|";
    sensorData += "HR:" + String((int)heartRate) + "|";
    sensorData += "ACCEL:" + String(accelMagnitude, 2);
    
    Serial2.println(sensorData);  // Send to WROVER
    
    // Debug output
    Serial.print("→ WROVER: ");
    Serial.println(sensorData);
  }
  
  // Manual capture with boot button
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      Serial.println("\n=== MANUAL CAPTURE ===");
      
      // Send current sensor data
      String sensorData = "SENSOR_DATA:MANUAL|";
      sensorData += "TEMP:" + String(temperature, 1) + "|";
      sensorData += "HUM:" + String(humidity, 1) + "|";
      sensorData += "GAS:" + String(gasLevel) + "|";
      sensorData += "HR:" + String((int)heartRate) + "|";
      sensorData += "ACCEL:" + String(accelMagnitude, 2);
      
      Serial2.println(sensorData);
      delay(100);
      Serial2.println("CAPTURE");
      
      if (lcdAvailable) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Capturing...");
      }
      
      while (digitalRead(BUTTON_PIN) == LOW) {
        delay(10);
      }
    }
  }
  
  // Listen for responses from WROVER
  if (Serial2.available()) {
    String response = Serial2.readStringUntil('\n');
    response.trim();
    
    if (response.startsWith("UPLOAD_SUCCESS")) {
      Serial.println("✓ Image uploaded successfully!");
    } else if (response.startsWith("UPLOAD_FAILED")) {
      Serial.println("✗ Upload failed!");
    } else if (response.length() > 0) {
      Serial.println(response);
    }
  }
  
  delay(10);
}