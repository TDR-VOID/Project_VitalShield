#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_AHTX0.h>


#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"


// ===================== CONFIGURE HERE =====================
#define WIFI_SSID      "2263081slt"
#define WIFI_PASSWORD  "199202FJ5"

#define API_KEY        "AIzaSyB6n9Dv6ANuWBl5qLZ6AqusQCsGBuIQpzU"
#define DATABASE_URL   "https://uoc-project-acf0b-default-rtdb.firebaseio.com/"

#define USER_EMAIL     "test@user.com"
#define USER_PASSWORD  "testpass"

// ==========================================================


FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_AHTX0 aht;


float ambient;
float object;
float relative_humidity;
float temperature; 

// --- Task Prototypes ---
void TaskSensorReadings(void * parameter);
void TaskFirebaseSender(void * parameter);
void initMLX90614();
void readMLX90614();
void initFirebase();
void initWifi();
void initAHT10();
void readAHT10();

void setup(){
  Serial.begin(115200);
  Wire.begin(); // Start I2C communication
  Serial.println("\n--- Starting Dual-Core IoT Task Setup ---");

  initWifi(); // Initialize WiFi
  initFirebase(); // Initialize Firebase
  initAHT10(); // Initialize AHT10 Sensor 
  initMLX90614(); // Call the initialization function



  // ----------------------------------------
  // 1. Sensor Readings Task (Pinned to Core 1)
  // Handles fast, dedicated sensor acquisition.
  // ----------------------------------------
  xTaskCreatePinnedToCore(
    TaskSensorReadings,      // Function to implement the task
    "Sensor_Reader",         // Name of the task
    6144,                    // Increased Stack size (6KB) for multiple sensor libraries
    NULL,                    // Task input parameter
    2,                       // Priority (Higher priority than the Sender)
    NULL,                    // Task handle
    1                        // Core to pin the task to (1 = Core 1)
  );
  Serial.println("[SETUP] Sensor Task created on Core 1.");


  // ----------------------------------------
  // 2. Firebase Sender Task (Pinned to Core 0)
  // Handles slower, network-blocking I/O (Wi-Fi, Firebase, OTA).
  // ----------------------------------------
  xTaskCreatePinnedToCore(
    TaskFirebaseSender,      // Function to implement the task
    "Firebase_Sender",       // Name of the task
    10000,                   // Stack size (10KB - more stack for network ops)
    NULL,                    // Task input parameter
    1,                       // Priority
    NULL,                    // Task handle
    0                        // Core to pin the task to (0 = Core 0)
  );
  Serial.println("[SETUP] Firebase Task created on Core 0.");
}


void loop() {
  // Use the main loop for simple, low-priority status/health checks.
  Serial.print("[LOOP] System Status Check - Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
}




// ------------------------------------------------------------------
// TASK IMPLEMENTATIONS
// ------------------------------------------------------------------

/**
 * @brief Task 1: Runs on Core 1, dedicated to sensor acquisition.
 */
void TaskSensorReadings(void * parameter) {
  Serial.println("[CORE 1 - SENSOR] Task started.");
  
  for (;;) {

    readAHT10(); // Call the AHT10 reading function
    readMLX90614(); // Call the reading function
    delay(1000);
    vTaskDelay(pdMS_TO_TICKS(2000)); 
  }
}

/**
 * @brief Task 2: Runs on Core 0, dedicated to Firebase/network communication.
 */
void TaskFirebaseSender(void * parameter) {
  Serial.println("[CORE 0 - FIREBASE] Task started.");
  

  for (;;) {
    // 1. Check if the sensor read was successful before sending
    
    // ---- Create JSON payload ----
    FirebaseJson json;
    json.set("Ambient", ambient);
    json.set("Object", object);
    json.set("Humidity", relative_humidity);
    json.set("Temperature", temperature);

    // Each user has their own folder
    String path = "/Sensor_Data/";

    // ---- Upload to Firebase ----
    if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
      Serial.println("Uploaded sensor data to Firebase");
    } else {
      Serial.print("Upload failed: ");
      Serial.println(fbdo.errorReason());
    }
    // 3. Task Delay
    // This task runs every 5 seconds.
    vTaskDelay(pdMS_TO_TICKS(5000)); 
  }
}





void initFirebase() {
// ---------------- Firebase ----------------
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("Signing in");
  while (!Firebase.ready()) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nFirebase ready!");

  if (auth.token.uid.length() > 0)
    Serial.println("User UID: " + String(auth.token.uid.c_str()));
  else
    Serial.println("UID not available yet.");
}



void initWifi(){
  // ---------------- WiFi ----------------
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to Wi-Fi: %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(250);
  }
  Serial.println("\nWi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

}



/**
 * @brief Initialize the AHT10 sensor
 */
void initAHT10() {
  Serial.println("\n--- AHT10/AHTX0 Test ---");
  
  if (aht.begin()) {
    Serial.println("AHT10/AHTX0 Connection Successful!");
  } else {
    Serial.println("AHT10/AHTX0 Connection FAILED. Check wiring/address.");
    while (true) delay(1000); 
  }
}


/** 
 * @brief Read and print AHT10 data
 */


void readAHT10() {
  sensors_event_t humidity, temp;
  
  if (aht.getEvent(&humidity, &temp)) {
    Serial.print("Temperature: ");
    Serial.print(temp.temperature);
    temperature = temp.temperature;
    Serial.print(" *C\tHumidity: ");
    Serial.print(humidity.relative_humidity);
    relative_humidity = humidity.relative_humidity;
    Serial.println(" %");
  } else {
    Serial.println("AHT10/AHTX0 Failed to read data!");
  }
}


/**
 * @brief Initialize the MLX90614 sensor
 */
void initMLX90614() {
  Serial.println("\n--- MLX90614 Initialization ---");

  if (mlx.begin()) {
    Serial.println("✅ MLX90614 Connection Successful!");
    Serial.println("Ambient and Object temperatures will be displayed.\n");
  } else {
    Serial.println("❌ MLX90614 Connection FAILED. Check wiring/address.");
    while (true) delay(1000); // Halt program if connection fails
  }
}



/** 
 * @brief Read and print MLX90614 temperature data
 */
void readMLX90614() {
  ambient = mlx.readAmbientTempC();
  object = mlx.readObjectTempC();

  if (isnan(ambient) || isnan(object)) {
    Serial.println("⚠️ Failed to read MLX90614 data!");
  } else {
    Serial.print("Ambient: ");
    Serial.print(ambient);
    Serial.print(" °C\tObject: ");
    Serial.print(object);
    Serial.println(" °C");
  }
}
