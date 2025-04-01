#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <time.h>

// DHT11 sensor configuration
#define DHTPIN 4      // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11 // DHT 11
DHT dht(DHTPIN, DHTTYPE);

// Ultrasonic sensor configuration
const int trigPin = 5;    // Trigger pin
const int echoPin = 18;   // Echo pin
#define SOUND_SPEED 0.034 // Speed of sound in cm/microsecond

// TDS sensor configuration
#define TDS_PIN 34        // ESP32 GPIO pin connected to TDS sensor
#define TDS_RELAY_PIN 14  // GPIO pin for TDS relay control

// Water level sensor configuration
#define WATER_SENSOR_PIN 32  // ESP32 analog pin connected to water sensor
#define WATER_RELAY_PIN 27   // GPIO pin for water level relay control
#define DRY_VALUE 0          // Value when sensor is completely dry
#define WET_VALUE 4095       // Value when sensor is completely wet (ESP32's ADC is 12-bit, 0-4095)

// WiFi credentials
const char* ssid = "vivo x100";
const char* password = "modify12";

// Postman Mock Server URL
const char* serverUrl = "https://e978879b-215c-4bf4-b33b-a43a2e2ae70e.mock.pstmn.io/data";

// Thresholds for alerts
const float TEMP_THRESHOLD = 30.0;    // Temperature threshold in °C
const float HUMIDITY_THRESHOLD = 70.0; // Humidity threshold in %
const float DISTANCE_THRESHOLD = 10.0; // Distance threshold in cm
const float TDS_THRESHOLD = 500.0;     // TDS threshold in ppm
const int WATER_LEVEL_THRESHOLD = 50;  // Water level threshold in %

// NTP Server to get time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;      // Adjust based on your timezone (in seconds)
const int   daylightOffset_sec = 0; // Adjust for daylight saving

// Current sensor values
float temperature = 0;
float humidity = 0;
float distanceCm = 0;
float tdsValue = 0;
int waterLevelPercent = 0;
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

// HTML for the web page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Sensor Dashboard</title>
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    body {margin: 0; padding: 20px; background-color: #f5f5f5;}
    h2 {font-size: 2.0rem; margin: 20px 0; color: #333;}
    p {font-size: 1.5rem; margin: 10px 0;}
    .alert {color: red; font-weight: bold;}
    .normal {color: green;}
    .card {
      background-color: white;
      box-shadow: 0 4px 8px 0 rgba(0,0,0,0.2);
      padding: 20px;
      margin: 10px;
      border-radius: 5px;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
      gap: 15px;
      max-width: 1200px;
      margin: 0 auto;
    }
    .value {
      font-size: 2.5rem;
      font-weight: bold;
      color: #0066cc;
    }
    .timestamp {
      font-size: 0.9rem;
      color: #666;
      margin-top: 20px;
    }
    .postman-status {
      margin-top: 20px;
      padding: 10px;
      background-color: #e6f7ff;
      border-radius: 5px;
      border-left: 4px solid #1890ff;
    }
    .relay {
      display: inline-block;
      padding: 8px 12px;
      border-radius: 20px;
      font-weight: bold;
      margin-top: 10px;
      font-size: 0.9rem;
    }
    .relay-on {
      background-color: #52c41a;
      color: white;
    }
    .relay-off {
      background-color: #f5222d;
      color: white;
    }
    .water-level-container {
      width: 80%;
      height: 20px;
      background-color: #f0f0f0;
      border-radius: 10px;
      margin: 10px auto;
      overflow: hidden;
    }
    .water-level-fill {
      height: 100%;
      background-color: #1890ff;
      border-radius: 10px;
      transition: width 0.5s ease-in-out;
    }
  </style>
</head>
<body>
  <h2>ESP32 Sensor Dashboard</h2>
  <div class="grid">
    <div class="card">
      <h3>Temperature</h3>
      <p class="value"><span id="temperature">%TEMPERATURE%</span> &deg;C</p>
      <p class="status" id="tempStatus">%TEMP_STATUS%</p>
    </div>
    <div class="card">
      <h3>Humidity</h3>
      <p class="value"><span id="humidity">%HUMIDITY%</span> &#37;</p>
      <p class="status" id="humStatus">%HUM_STATUS%</p>
    </div>
    <div class="card">
      <h3>Distance</h3>
      <p class="value"><span id="distance">%DISTANCE%</span> cm</p>
      <p class="status" id="distStatus">%DIST_STATUS%</p>
    </div>
    <div class="card">
      <h3>TDS Value</h3>
      <p class="value"><span id="tds">%TDS%</span> ppm</p>
      <p class="status" id="tdsStatus">%TDS_STATUS%</p>
      <div class="relay %TDS_RELAY_CLASS%" id="tdsRelayStatus">TDS Relay: %TDS_RELAY_STATUS%</div>
    </div>
    <div class="card">
      <h3>Water Level</h3>
      <p class="value"><span id="waterLevel">%WATER_LEVEL%</span> &#37;</p>
      <p class="status" id="waterLevelStatus">%WATER_LEVEL_STATUS%</p>
      <div class="relay %WATER_RELAY_CLASS%" id="waterRelayStatus">Water Relay: %WATER_RELAY_STATUS%</div>
    </div>
  </div>
  <div class="postman-status">
    <h3>Data Transfer Status</h3>
    <p>Data is being sent to Postman Mock Server every 30 seconds</p>
    <p>Last data sent: <span id="timestamp">%TIMESTAMP%</span></p>
  </div>
<script>
// Function to update dashboard with latest sensor values
function updateDashboard() {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      try {
        const data = JSON.parse(this.responseText);
        
        // Update sensor values
        document.getElementById("temperature").innerHTML = data.temperature.toFixed(1);
        document.getElementById("humidity").innerHTML = data.humidity.toFixed(1);
        document.getElementById("distance").innerHTML = data.distance.toFixed(1);
        document.getElementById("tds").innerHTML = data.tds.toFixed(0);
        document.getElementById("waterLevel").innerHTML = data.waterLevel;
        document.getElementById("waterLevelFill").style.width = data.waterLevel + '%';
        document.getElementById("timestamp").innerHTML = data.timestamp;
        
        // Update status messages
        document.getElementById("tempStatus").innerHTML = data.tempAlert ? 
          "<span class='alert'>ALERT! Temperature too high</span>" : 
          "<span class='normal'>Normal</span>";
        
        document.getElementById("humStatus").innerHTML = data.humidityAlert ? 
          "<span class='alert'>ALERT! Humidity too high</span>" : 
          "<span class='normal'>Normal</span>";
          
        document.getElementById("distStatus").innerHTML = data.distanceAlert ? 
          "<span class='alert'>ALERT! Object too close</span>" : 
          "<span class='normal'>Normal</span>";
          
        document.getElementById("tdsStatus").innerHTML = data.tdsAlert ? 
          "<span class='alert'>ALERT! TDS level below threshold</span>" : 
          "<span class='normal'>Normal</span>";
          
        document.getElementById("waterLevelStatus").innerHTML = data.waterLevelAlert ? 
          "<span class='alert'>ALERT! Water level below threshold</span>" : 
          "<span class='normal'>Normal</span>";
          
        // Update relay statuses
        if (data.tdsRelayStatus) {
          document.getElementById("tdsRelayStatus").innerHTML = "TDS Relay: ON";
          document.getElementById("tdsRelayStatus").className = "relay relay-on";
        } else {
          document.getElementById("tdsRelayStatus").innerHTML = "TDS Relay: OFF";
          document.getElementById("tdsRelayStatus").className = "relay relay-off";
        }
        
        if (data.waterRelayStatus) {
          document.getElementById("waterRelayStatus").innerHTML = "Water Relay: ON";
          document.getElementById("waterRelayStatus").className = "relay relay-on";
        } else {
          document.getElementById("waterRelayStatus").innerHTML = "Water Relay: OFF";
          document.getElementById("waterRelayStatus").className = "relay relay-off";
        }
        
        console.log("Dashboard updated successfully");
      } catch (e) {
        console.error("Error updating dashboard:", e);
      }
    }
  };
  xhttp.open("GET", "/data", true);
  xhttp.send();
}

// Initial update
updateDashboard();

// Set interval to update every 5 seconds
setInterval(updateDashboard, 5000);
</script>
</body>
</html>
)rawliteral";

// Replaces placeholder with sensor values
String processor(const String& var) {
  if(var == "TEMPERATURE") {
    return String(temperature, 1);
  }
  else if(var == "HUMIDITY") {
    return String(humidity, 1);
  }
  else if(var == "DISTANCE") {
    return String(distanceCm, 1);
  }
  else if(var == "TDS") {
    return String(tdsValue, 0);
  }
  else if(var == "WATER_LEVEL") {
    return String(waterLevelPercent);
  }
  else if(var == "TIMESTAMP") {
    return timestamp;
  }
  else if(var == "TEMP_STATUS") {
    if(tempAlert) {
      return "<span class='alert'>ALERT! Temperature too high</span>";
    } else {
      return "<span class='normal'>Normal</span>";
    }
  }
  else if(var == "HUM_STATUS") {
    if(humidityAlert) {
      return "<span class='alert'>ALERT! Humidity too high</span>";
    } else {
      return "<span class='normal'>Normal</span>";
    }
  }
  else if(var == "DIST_STATUS") {
    if(distanceAlert) {
      return "<span class='alert'>ALERT! Object too close</span>";
    } else {
      return "<span class='normal'>Normal</span>";
    }
  }
  else if(var == "TDS_STATUS") {
    if(tdsAlert) {
      return "<span class='alert'>ALERT! TDS level below threshold</span>";
    } else {
      return "<span class='normal'>Normal</span>";
    }
  }
  else if(var == "WATER_LEVEL_STATUS") {
    if(waterLevelAlert) {
      return "<span class='alert'>ALERT! Water level below threshold</span>";
    } else {
      return "<span class='normal'>Normal</span>";
    }
  }
  else if(var == "TDS_RELAY_STATUS") {
    return tdsRelayStatus ? "ON" : "OFF";
  }
  else if(var == "TDS_RELAY_CLASS") {
    return tdsRelayStatus ? "relay-on" : "relay-off";
  }
  else if(var == "WATER_RELAY_STATUS") {
    return waterRelayStatus ? "ON" : "OFF";
  }
  else if(var == "WATER_RELAY_CLASS") {
    return waterRelayStatus ? "relay-on" : "relay-off";
  }
  return String();
}

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

int readWaterLevelSensor() {
  // Read the analog value from the water sensor
  int waterLevel = analogRead(WATER_SENSOR_PIN);
  
  // Convert to a percentage
  int waterLevelPercent = map(waterLevel, DRY_VALUE, WET_VALUE, 0, 100);
  
  // Ensure the percentage is within valid range
  waterLevelPercent = constrain(waterLevelPercent, 0, 100);
  
  return waterLevelPercent;
}

void controlRelays() {
  // Control TDS relay - turn on when TDS is below threshold
  if (tdsValue < TDS_THRESHOLD) {
    digitalWrite(TDS_RELAY_PIN, HIGH);  // Activate relay
    tdsRelayStatus = true;
    Serial.println("TDS Relay activated: TDS below threshold");
  } else {
    digitalWrite(TDS_RELAY_PIN, LOW);   // Deactivate relay
    tdsRelayStatus = false;
    Serial.println("TDS Relay deactivated: TDS above threshold");
  }
  
  // Control Water Level relay - turn on when water level is below threshold
  if (waterLevelPercent < WATER_LEVEL_THRESHOLD) {
    digitalWrite(WATER_RELAY_PIN, HIGH);  // Activate relay
    waterRelayStatus = true;
    Serial.println("Water Relay activated: Water level below threshold");
  } else {
    digitalWrite(WATER_RELAY_PIN, LOW);   // Deactivate relay
    waterRelayStatus = false;
    Serial.println("Water Relay deactivated: Water level above threshold");
  }
}

void sendToPostman() {
  // Check WiFi connection status
  if(WiFi.status() == WL_CONNECTED) {
    // Create JSON document
    JsonDocument jsonDoc;
    
    jsonDoc["temperature"] = temperature;
    jsonDoc["humidity"] = humidity;
    jsonDoc["tempAlert"] = tempAlert;
    jsonDoc["humidityAlert"] = humidityAlert;
    jsonDoc["distance"] = distanceCm;
    jsonDoc["distanceAlert"] = distanceAlert;
    jsonDoc["tds"] = tdsValue;
    jsonDoc["tdsAlert"] = tdsAlert;
    jsonDoc["tdsRelayStatus"] = tdsRelayStatus;
    jsonDoc["waterLevel"] = waterLevelPercent;
    jsonDoc["waterLevelAlert"] = waterLevelAlert;
    jsonDoc["waterRelayStatus"] = waterRelayStatus;
    jsonDoc["timestamp"] = timestamp;
    
    // Serialize JSON to string
    String jsonPayload;
    serializeJson(jsonDoc, jsonPayload);
    
    HTTPClient http;
    
    // Create a secure client that skips certificate validation
    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure(); // Skip certificate validation
    
    // Specify the target URL with secure client
    http.begin(*client, serverUrl);
    
    // Specify content-type header
    http.addHeader("Content-Type", "application/json");
    
    // Send HTTP POST request
    int httpResponseCode = http.POST(jsonPayload);
    
    // Check the response - with improved debugging
    if(httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.print("Response length: ");
      Serial.println(response.length());
      Serial.println("Response content: ");
      
      // Print response byte by byte for better debugging
      if(response.length() == 0) {
        Serial.println("[Empty response]");
      } else {
        Serial.println("---Response Begin---");
        Serial.println(response);
        Serial.println("---Response End---");
      }
    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }
    
    // Free resources
    http.end();
    delete client; // Free the client memory
  } else {
    Serial.println("WiFi Disconnected");
    
    // Try to reconnect
    WiFi.begin(ssid, password);
    Serial.println("Trying to reconnect...");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.println("WiFi reconnected");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize DHT sensor
  dht.begin();
  Serial.println("DHT11 sensor initialized");
  
  // Set up ultrasonic sensor pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  Serial.println("Ultrasonic sensor initialized");
  
  // Set up relay pins
  pinMode(TDS_RELAY_PIN, OUTPUT);
  pinMode(WATER_RELAY_PIN, OUTPUT);
  digitalWrite(TDS_RELAY_PIN, LOW); // Initialize relay to OFF
  digitalWrite(WATER_RELAY_PIN, LOW); // Initialize relay to OFF
  Serial.println("Relays initialized");
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  // Route for data as JSON
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"temperature\":" + String(temperature) + ",";
    json += "\"humidity\":" + String(humidity) + ",";
    json += "\"distance\":" + String(distanceCm) + ",";
    json += "\"tds\":" + String(tdsValue) + ",";
    json += "\"waterLevel\":" + String(waterLevelPercent) + ",";
    json += "\"tempAlert\":" + String(tempAlert ? "true" : "false") + ",";
    json += "\"humidityAlert\":" + String(humidityAlert ? "true" : "false") + ",";
    json += "\"distanceAlert\":" + String(distanceAlert ? "true" : "false") + ",";
    json += "\"tdsAlert\":" + String(tdsAlert ? "true" : "false") + ",";
    json += "\"waterLevelAlert\":" + String(waterLevelAlert ? "true" : "false") + ",";
    json += "\"tdsRelayStatus\":" + String(tdsRelayStatus ? "true" : "false") + ",";
    json += "\"waterRelayStatus\":" + String(waterRelayStatus ? "true" : "false") + ",";
    json += "\"timestamp\":\"" + timestamp + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // Get current time
  timestamp = getFormattedTime();
  
  // Read temperature and humidity from DHT11
  float newTemp = dht.readTemperature();
  float newHumidity = dht.readHumidity();
  
  // Only update if reads are valid
  if (!isnan(newTemp) && !isnan(newHumidity)) {
    temperature = newTemp;
    humidity = newHumidity;
    
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.print(" °C, Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  } else {
    Serial.println("Failed to read from DHT sensor!");
  }
  
  // Read distance from ultrasonic sensor
  // Clear the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  // Set the trigPin HIGH for 10 microseconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Read the echoPin, returns the sound wave travel time in microseconds
  long duration = pulseIn(echoPin, HIGH);
  
  // Calculate the distance
  distanceCm = duration * SOUND_SPEED / 2;
  
  Serial.print("Distance (cm): ");
  Serial.println(distanceCm);
  
  // Read TDS sensor
  tdsValue = readTDSSensor();
  Serial.print("TDS Value: ");
  Serial.print(tdsValue, 0);
  Serial.println(" ppm");
  
  // Read Water level sensor
  waterLevelPercent = readWaterLevelSensor();
  Serial.print("Water Level: ");
  Serial.print(waterLevelPercent);
  Serial.println("%");
  
  // Determine alert conditions
  tempAlert = temperature > TEMP_THRESHOLD;
  humidityAlert = humidity > HUMIDITY_THRESHOLD;
  distanceAlert = distanceCm < DISTANCE_THRESHOLD;
  tdsAlert = tdsValue < TDS_THRESHOLD;
  waterLevelAlert = waterLevelPercent < WATER_LEVEL_THRESHOLD;
  
  // Control relays based on sensor values
  controlRelays();
  
  // Send data to Postman every 30 seconds
  static unsigned long lastPostmanSend = 0;
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastPostmanSend >= 30000) {
    Serial.println("Sending data to Postman...");
    sendToPostman();
    lastPostmanSend = currentMillis;
  }
  
  // Small delay between sensor readings
  delay(5000);
}