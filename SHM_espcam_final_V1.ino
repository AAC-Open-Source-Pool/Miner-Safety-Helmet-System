// ESP Cam code
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// WiFi credentials
const char* ssid = "**";
const char* password = "**";

// Flask server URL
const char* serverUrl = "http://192.168.110.160:5000/upload";

// Time interval - SET TO 10 SECONDS
const unsigned long CAPTURE_INTERVAL = 10000;  // 10 seconds for continuous monitoring

// Camera pin definitions
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define LED_PIN 33

unsigned long lastCaptureTime = 0;
bool autoMode = true;

// Store latest sensor data
String latestSensorData = "SENSOR_DATA:NONE|TEMP:0|HUM:0|GAS:0|HR:0|ACCEL:0";

void setup() {
  Serial.begin(115200);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  delay(2000);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  
  digitalWrite(LED_PIN, LOW);
  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Camera configuration
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.println("CAM_ERROR");
    while(1) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("CAM_READY");
  Serial.print("Auto-capture mode: Every ");
  Serial.print(CAPTURE_INTERVAL / 1000);
  Serial.println(" seconds");
  Serial.println("Waiting for sensor data from WROOM...");
  
  // Take first capture immediately on startup
  Serial.println("\n=== Initial Capture on Startup ===");
  delay(3000); // Wait for WROOM to send initial data
  
  // Check if we received sensor data
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');
    if (data.startsWith("SENSOR_DATA:")) {
      latestSensorData = data;
      Serial.println("Received initial data: " + latestSensorData);
    }
  }
  
  captureAndUpload();
  
  // Initialize timer for subsequent captures
  lastCaptureTime = millis();
}

void captureAndUpload() {
  digitalWrite(LED_PIN, HIGH);
  
  unsigned long captureTime = millis();
  
  Serial.print("[");
  Serial.print(captureTime / 1000);
  Serial.println("s] Capturing image...");
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("UPLOAD_FAILED:Camera capture failed");
    digitalWrite(LED_PIN, LOW);
    return;
  }
  
  unsigned long captureComplete = millis();
  Serial.print("[");
  Serial.print(captureComplete / 1000);
  Serial.print("s] Image captured: ");
  Serial.print(fb->len);
  Serial.print(" bytes (took ");
  Serial.print(captureComplete - captureTime);
  Serial.println("ms)");
  
  // Upload to Flask server with sensor data
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    Serial.print("Server URL: ");
    Serial.println(serverUrl);
    Serial.print("WiFi Signal Strength (RSSI): ");
    Serial.println(WiFi.RSSI());
    
    // Test server connectivity first
    Serial.println("Testing server connection...");
    http.begin(serverUrl);
    http.setTimeout(15000);  // 15 second timeout
    
    int pingResponse = http.GET();
    if (pingResponse == 405) {  // Method Not Allowed is OK (means server is there)
      Serial.println("Server is reachable!");
    } else if (pingResponse > 0) {
      Serial.print("Server ping response: ");
      Serial.println(pingResponse);
    } else {
      Serial.print("Server unreachable! Error: ");
      Serial.println(http.errorToString(pingResponse));
      http.end();
      esp_camera_fb_return(fb);
      digitalWrite(LED_PIN, LOW);
      return;
    }
    http.end();
    
    // Now do actual upload
    delay(100);
    http.begin(serverUrl);
    http.setTimeout(15000);
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("Content-Length", String(fb->len));
    
    // Add sensor data as custom header
    http.addHeader("X-Sensor-Data", latestSensorData);
    
    Serial.print("[");
    Serial.print(millis() / 1000);
    Serial.println("s] Uploading to server...");
    Serial.println("Sensor Data: " + latestSensorData);
    
    unsigned long uploadStart = millis();
    int httpResponseCode = http.POST(fb->buf, fb->len);
    unsigned long uploadComplete = millis();
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("[");
      Serial.print(uploadComplete / 1000);
      Serial.print("s] UPLOAD_SUCCESS:");
      Serial.print(httpResponseCode);
      Serial.print(" (upload took ");
      Serial.print(uploadComplete - uploadStart);
      Serial.println("ms)");
      Serial.println(response);
      
      // Blink LED twice for success
      digitalWrite(LED_PIN, LOW);
      delay(200);
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
    } else {
      Serial.print("UPLOAD_FAILED:");
      Serial.println(httpResponseCode);
      Serial.print("Error details: ");
      Serial.println(http.errorToString(httpResponseCode));
      
      // Rapid blink for error
      for(int i = 0; i < 5; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
      }
    }
    
    http.end();
  } else {
    Serial.println("UPLOAD_FAILED:WiFi not connected");
    Serial.print("WiFi Status: ");
    Serial.println(WiFi.status());
    Serial.println("Reconnecting to WiFi...");
    WiFi.reconnect();
  }
  
  esp_camera_fb_return(fb);
  digitalWrite(LED_PIN, LOW);
  
  Serial.print("Total process time: ");
  Serial.print(millis() - captureTime);
  Serial.println("ms");
}

void loop() {
  // Auto-capture mode
  if (autoMode && millis() - lastCaptureTime >= CAPTURE_INTERVAL) {
    Serial.println("\n=== Auto Capture Triggered ===");
    captureAndUpload();
    lastCaptureTime = millis();
    
    Serial.print("Next capture in ");
    Serial.print(CAPTURE_INTERVAL / 1000);
    Serial.println(" seconds");
  }
  
  // Listen for commands from WROOM
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.startsWith("SENSOR_DATA:")) {
      // Store sensor data
      latestSensorData = cmd;
      Serial.println("Received sensor data: " + latestSensorData);
    }
    else if (cmd == "CAPTURE") {
      Serial.println("\n=== Manual/Alert Capture Triggered ===");
      captureAndUpload();
      lastCaptureTime = millis();
      Serial.println("Auto-capture timer reset!");
    }
    else if (cmd == "AUTO_ON") {
      autoMode = true;
      Serial.println("Auto-capture mode ENABLED");
      lastCaptureTime = millis();
    }
    else if (cmd == "AUTO_OFF") {
      autoMode = false;
      Serial.println("Auto-capture mode DISABLED");
    }
    else if (cmd == "STATUS") {
      Serial.println("\n=== STATUS ===");
      Serial.print("Auto mode: ");
      Serial.println(autoMode ? "ON" : "OFF");
      Serial.print("Interval: ");
      Serial.print(CAPTURE_INTERVAL / 1000);
      Serial.println(" seconds");
      Serial.print("WiFi: ");
      Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("RSSI: ");
        Serial.println(WiFi.RSSI());
      }
      Serial.print("Server: ");
      Serial.println(serverUrl);
      Serial.println("Latest Sensor Data: " + latestSensorData);
      
      if (autoMode) {
        unsigned long remaining = CAPTURE_INTERVAL - (millis() - lastCaptureTime);
        if (remaining > CAPTURE_INTERVAL) remaining = 0;
        Serial.print("Next capture in: ");
        Serial.print(remaining / 1000);
        Serial.println(" seconds");
      }
      Serial.println("=============");
    }
    else if (cmd == "TEST_SERVER") {
      Serial.println("\n=== Testing Server Connection ===");
      if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        Serial.print("Trying to reach: ");
        Serial.println(serverUrl);
        
        http.begin(serverUrl);
        http.setTimeout(5000);
        int httpCode = http.GET();
        
        Serial.print("Response code: ");
        Serial.println(httpCode);
        
        if (httpCode > 0) {
          Serial.println("✓ Server is reachable!");
          String payload = http.getString();
          Serial.println("Response preview (first 200 chars):");
          Serial.println(payload.substring(0, 200));
        } else {
          Serial.println("✗ Cannot reach server!");
          Serial.println(http.errorToString(httpCode));
        }
        http.end();
      } else {
        Serial.println("✗ WiFi not connected!");
      }
      Serial.println("======================");
    }
  }
  
  delay(100);
}