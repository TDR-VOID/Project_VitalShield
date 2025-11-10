#include <Arduino.h>

#include <Wire.h>
#include "i2c_scanner.h" // Include the header file for the I2C scanner
#include "sensor_handler.h"

// --- Task Prototypes ---
void TaskSensorReadings(void * parameter);
void TaskFirebaseSender(void * parameter);

void setup(){
  Serial.begin(115200);
  Wire.begin(); // Initialize I2C as master

  scanI2CDevices(); // Call the function to scan for I2C devices

  Serial.println("\n--- Starting Dual-Core IoT Task Setup ---");

  // Call the centralized sensor initialization function
  if (initAllSensors()) {
    Serial.println("[SETUP] Sensor modules successfully initialized.");
  } else {
    Serial.println("[SETUP] WARNING: Sensor initialization failed! Check I2C connections.");
  }

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
    // 1. Read Data from ALL sensors
    if (readAndProcessAllSensors()) {
      Serial.println("[CORE 1 - SENSOR] All sensors successfully read.");
      
      // 2. Print all current data (for debug/verification)
      printAllSensorData();
    } else {
      Serial.println("[CORE 1 - SENSOR] ERROR: Failed to read one or more sensors.");
    }

    // 3. Task Delay
    // This task runs every 2 seconds.
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
    if (currentSensorData.scanSuccess) {
        
      // 2. Format and Send Data to Firebase
      // Use currentSensorData.aht_temperature, currentSensorData.tvoc, etc.
      Serial.print("[CORE 0 - FIREBASE] Preparing to send data. Temp: ");
      Serial.print(currentSensorData.aht_temperature);
      Serial.print(", eCO2: ");
      Serial.print(currentSensorData.eco2);
      Serial.println("...");

      // *** PLACE YOUR ACTUAL FIREBASE SENDING CODE HERE ***
      // Example: Firebase.pushJSON(firebaseData, "/sensor_data", jsonDocument);
      
      // Simulate network latency 
      vTaskDelay(pdMS_TO_TICKS(400)); 
      
      Serial.println("[CORE 0 - FIREBASE] Send successful.");
      
    } else {
        Serial.println("[CORE 0 - FIREBASE] Data not sent: Previous sensor read failed.");
    }
    
    // 3. Task Delay
    // This task runs every 5 seconds.
    vTaskDelay(pdMS_TO_TICKS(5000)); 
  }
}
