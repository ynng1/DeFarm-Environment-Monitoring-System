#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <time.h>

// TDS sensor configuration
#define TDS_PIN 34        // ESP32 GPIO pin connected to TDS sensor

// Water level sensor configuration
#define WATER_SENSOR_PIN 32  // ESP32 analog pin connected to water sensor
#define DRY_VALUE 0          // Value when sensor is completely dry
#define WET_VALUE 4095        // Value when sensor is completely wet (ESP32's ADC is 12-bit, 0-4095)

// Water Circuit configuration
#define drain_pump 21
#define main_pump 19
#define Power_Switch 17
#define Maintenance_Switch 16
bool isPumping = false;


// Water Level sensor
#define TRIG_PIN 32
#define ECHO_PIN 33
const int tankheight = 10;
#define SOUND_SPEED 0.034

// Fertiliser Control configuration
#define fert_pump 5
#define Mix_pump 18

// Average
#define TDS_BUFFER_SIZE 20
float tdsBuffer[TDS_BUFFER_SIZE];
int tdsIndex = 0;
bool bufferFilled = false;

// Airflow Control configuration
#define fan 23

// WiFi credentials
const char* ssid = "Keith";
const char* password = "100320";

// Postman Mock Server URL
const char* serverUrl = "https://18.142.255.27:82/data";

// Thresholds for alerts
const float TEMP_THRESHOLD = 30.0;    // Temperature threshold in °C
const float HUMIDITY_THRESHOLD = 70.0; // Humidity threshold in %
const float DISTANCE_THRESHOLD = 10.0; // Distance threshold in cm
const float TDS_THRESHOLD = 240;     // TDS threshold in ppm
const int WATER_LEVEL_THRESHOLD_LOWER = 40;  // Water level threshold in %
const int WATER_LEVEL_THRESHOLD_UPPER = 70; // Upper water level threshold in %

// NTP Server to get time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;      // Adjust based on your timezone (in seconds)
const int   daylightOffset_sec = 0; // Adjust for daylight saving

// Current sensor values
float temperature = 0;
float humidity = 0;
float tdsValue = 0;
// int waterLevelPercent = 0;
bool tempAlert = false;
bool humidityAlert = false;
bool distanceAlert = false;
bool tdsAlert = false;
bool waterLevelAlert = false;
bool tdsRelayStatus = false;
bool waterRelayStatus = false;
String timestamp = "";

// TDS calibration values
float vRef = 3.3;      // Reference voltage (3.3V for ESP32)
float kValue = 1.0;    // K value of the probe (default is 1.0, adjust based on calibration)

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

String getFormattedTime() {
  struct tm timeinfo;
  char timeStringBuff[30];
  
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "2025-03-28T12:00:00Z"; // Fallback timestamp
  }
  
  // Format: YYYY-MM-DDThh:mm:ssZ
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timeStringBuff);
}

//functions start here

// Get average TDS value from buffer
// This function adds a new reading to the buffer and calculates the average
float getAverageTDS(float newReading) {
  tdsBuffer[tdsIndex] = newReading;
  tdsIndex = (tdsIndex + 1) % TDS_BUFFER_SIZE;

  if (tdsIndex == 0) {
    bufferFilled = true;
  }

  int count = bufferFilled ? TDS_BUFFER_SIZE : tdsIndex;

  float sum = 0;
  for (int i = 0; i < count; i++) {
    sum += tdsBuffer[i];
  }

  return sum / count;
}


float readTDSSensor() {
  // Read the analog value from the TDS sensor
  int rawValue = analogRead(TDS_PIN);
  
  // Convert the analog value to voltage
  float voltage = rawValue * (vRef / 4095.0); // ESP32 has 12-bit ADC (0-4095)
  
  // Temperature compensation (assuming water temp is 25°C)
  float temperature = 25.0;
  float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
  
  // Temperature compensation
  float compensationVoltage = voltage / compensationCoefficient;
  
  // Convert voltage value to TDS value
  float tds = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage - 
              255.86 * compensationVoltage * compensationVoltage + 
              857.39 * compensationVoltage) * kValue;
  
  return tds;
}

float readWaterLevelPercent() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout

  // Convert duration to distance in cm
  float distance = duration * 0.0343 / 2.0;

  // Calculate percentage (assuming sensor is at the top of the tank)
  float percent = 100.0 * (tankheight - distance) / tankheight;
  percent = constrain(percent, 0.0, 100.0);

  return percent;
}

//WaterPump Control function
void WaterPumpControl() {
  bool maintenanceState = digitalRead(Maintenance_Switch) == LOW;
  int waterLevel = readWaterLevelPercent();

  if (maintenanceState) {
    Serial.println("Maintenance Mode: Draining Water");
    digitalWrite(main_pump, HIGH);   // Turn off main pump
    digitalWrite(drain_pump, LOW);   // Turn on drain pump
    isPumping = false;               // Reset state
  } else {
    // Normal operation logic with hysteresis
    if (waterLevel < WATER_LEVEL_THRESHOLD_LOWER) {
      isPumping = true;
    } else if (waterLevel > WATER_LEVEL_THRESHOLD_UPPER) {
      isPumping = false;
    }

    if (digitalRead(Power_Switch) == LOW && isPumping) {
      Serial.println("Normal Operation: Main Pump On");
      digitalWrite(main_pump, LOW);    // Turn on main pump
      digitalWrite(drain_pump, HIGH);  // Turn off drain pump
    } else {
      Serial.println("System Off");
      digitalWrite(main_pump, HIGH);   // Turn off main pump
      digitalWrite(drain_pump, HIGH);  // Turn off drain pump
    }
  }
}

//Fertiliser Control function
enum FertiliserState {
  IDLE,
  DISPENSING,
  MIXING
};

FertiliserState fertState = IDLE;
unsigned long fertStateStartTime = 0;

void FertiliserControl() {
  unsigned long currentMillis = millis();
  float currentTDS = readTDSSensor();
  tdsValue = getAverageTDS(currentTDS);

  Serial.print("Raw TDS: ");
  Serial.print(currentTDS);
  Serial.print(" | Smoothed TDS: ");
  Serial.print(tdsValue, 0);
  Serial.println(" ppm");

  switch (fertState) {
    case IDLE:
      if (tdsValue < TDS_THRESHOLD && tdsValue != 0) {
        digitalWrite(fert_pump, LOW);
        Serial.println("Fertiliser Pump ON: TDS below threshold");
        fertStateStartTime = currentMillis;
        fertState = DISPENSING;
        Serial.println("Dispensing fertiliser...");
      }
      break;

    case DISPENSING:
      if (currentMillis - fertStateStartTime >= 2000) {
        digitalWrite(fert_pump, HIGH);
        Serial.println("Fertiliser Pump OFF: 2s dispensing done");
        fertStateStartTime = currentMillis;
        fertState = MIXING;
        Serial.println("Mixing fertiliser...");
      }
      break;

    case MIXING:
      if (currentMillis - fertStateStartTime >= 10000) {
        Serial.println("Mixing complete. Ready to check TDS again.");
        digitalWrite(Mix_pump, HIGH);
        fertStateStartTime = currentMillis;
        fertState = IDLE;
        Serial.println("Idle...");
      }
      break;
  }
}

//Airflow Control function
void AirflowControl() {
  // Control the fan based on temperature and humidity
  if (humidity > HUMIDITY_THRESHOLD) {
    digitalWrite(fan, LOW); // Turn on fan
    Serial.println("Fan ON: High temperature or humidity detected");
  } else {
    digitalWrite(fan, HIGH); // Turn off fan
    Serial.println("Fan OFF: Temperature and humidity normal");
  }
}

//  to measure raw distance from the ultrasonic sensor
long readDistance() {
  // Trigger the ultrasonic pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Measure the pulse duration
  long duration = pulseIn(ECHO_PIN, HIGH);
  
  // Calculate the distance in cm
  long distance = duration * 0.0343 / 2;
  return distance;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  // Set up Water level pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Set up Water circuit pins
  pinMode(main_pump, OUTPUT);
  pinMode(drain_pump, OUTPUT);
  pinMode(Power_Switch, INPUT_PULLUP);
  pinMode(Maintenance_Switch, INPUT_PULLUP);

  digitalWrite(main_pump, HIGH);
  digitalWrite(drain_pump, HIGH);

  // Set Up Fertiliser circuit pins
  pinMode(fert_pump, OUTPUT);
  digitalWrite(fert_pump, HIGH); // Initialize relay to OFF

  // Set Up Airflow Circuit pins
  pinMode(fan, OUTPUT);
  digitalWrite(fan, LOW);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect. Continuing offline...");
  }
}

void loop() {
  float waterlevel = readWaterLevelPercent();
  Serial.print("water level percent: ");
  Serial.println(waterlevel);

  // Determine alert conditions
  tdsAlert = tdsValue < TDS_THRESHOLD;
  waterLevelAlert = waterlevel < WATER_LEVEL_THRESHOLD_UPPER || waterlevel > WATER_LEVEL_THRESHOLD_LOWER;

  // Control water pump based on switches
  WaterPumpControl();

  // Control fertiliser pump based on TDS value
  FertiliserControl();
  // FertiliserTest(); // Test mode for fertiliser pump

  // Control Fan based on temperature and humidity
  AirflowControl();

  // Small delay between sensor readings
  delay(500);
}