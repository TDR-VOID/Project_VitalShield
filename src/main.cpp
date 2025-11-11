#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_MLX90614.h>

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
// ==========================================================+


FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

Adafruit_MLX90614 mlx = Adafruit_MLX90614();


float ambient;
float object; 

// --- Task Prototypes ---
void TaskSensorReadings(void * parameter);
void TaskFirebaseSender(void * parameter);
void initMLX90614();
void readMLX90614();

void setup(){
  Serial.begin(115200);

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

  Serial.println("\n--- Starting Dual-Core IoT Task Setup ---");

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
  
  // *** PLACE YOUR WIFI & FIREBASE INITIALIZATION CODE HERE ***
  // e.g., WiFi.begin(), Firebase.begin()
  // ...

  for (;;) {
    // 1. Check if the sensor read was successful before sending
    
        // ---- Create JSON payload ----
    FirebaseJson json;
    json.set("Ambient", ambient);
    json.set("Object", object);

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



/**
 * @brief Initialize the MLX90614 sensor
 */
void initMLX90614() {
  Wire.begin(); // Start I2C communication
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
