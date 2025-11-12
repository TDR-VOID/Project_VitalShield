#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>


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

// ========================================================== //


// --- Firebase objects ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- Sensor objects ---
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_AHTX0 aht;
Adafruit_MPU6050 mpu;


float ambient;
float object;
float relative_humidity;
float temperature;
float accelerationX, accelerationY, accelerationZ;
float gyroX, gyroY, gyroZ; 
float temperatureMPU;

// --- Task Prototypes ---
void TaskSensorReadings(void * parameter);
void TaskFirebaseSender(void * parameter);

// --- Function Prototypes ---
void initMLX90614();
void readMLX90614();
void initFirebase();
void initWifi();
void initAHT10();
void readAHT10();
void initMPU6050();
void readMPU6050();

void setup(){
  Serial.begin(115200);
  Wire.begin(); // Start I2C communication
  Serial.println("\n--- Starting Dual-Core IoT Task Setup ---");

  initWifi(); // Initialize WiFi
  initFirebase(); // Initialize Firebase
  initAHT10(); // Initialize AHT10 Sensor 
  initMLX90614(); // Call the initialization function
  initMPU6050(); // Initialize MPU6050 Sensor



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
    readMPU6050(); // Call the MPU6050 reading function
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

    FirebaseJson AHT10_json, MLX90614_json, MPU6050_json;

    AHT10_json.set("Humidity", relative_humidity);
    AHT10_json.set("Temperature", temperature);

    MLX90614_json.set("Ambient", ambient);
    MLX90614_json.set("Object", object);

    MPU6050_json.set("Accel_X", accelerationX);
    MPU6050_json.set("Accel_Y", accelerationY);
    MPU6050_json.set("Accel_Z", accelerationZ);
    MPU6050_json.set("Gyro_X", gyroX);
    MPU6050_json.set("Gyro_Y", gyroY);
    MPU6050_json.set("Gyro_Z", gyroZ);
    MPU6050_json.set("Temp_MPU", temperatureMPU);


    // ---- Upload AHT10 data to Firebase ----
    if (Firebase.RTDB.setJSON(&fbdo, "User1/Sensor_Data/AHT10", &AHT10_json)) {
      Serial.println("Uploaded AHT10 data to Firebase");
    } else {
      Serial.print("Upload failed: ");
      Serial.println(fbdo.errorReason());
    }

    // ---- Upload MLX90614 data to Firebase ---- 
    if (Firebase.RTDB.setJSON(&fbdo,"User1/Sensor_Data/MLX90614", &MLX90614_json)) {
      Serial.println("Uploaded MLX90614 data to Firebase");
    } else {
      Serial.print("Upload failed: ");
      Serial.println(fbdo.errorReason());
    }

    // ---- Upload MPU6050 data to Firebase ---- 
    if (Firebase.RTDB.setJSON(&fbdo,"User1/Sensor_Data/MPU6050", &MPU6050_json)) {
      Serial.println("Uploaded MPU6050 data to Firebase");
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
 * @brief Initialize Firebase connection
 */
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


/**
 * @brief Initialize WiFi connection
 */
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
 * @brief Initialize the MPU6050 sensor
 */
void initMPU6050() {
  while (!Serial)
    delay(10); // will pause Zero, Leonardo, etc until serial console opens

  Serial.println("Adafruit MPU6050 test!");

  // Try to initialize!
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 Found!");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  Serial.print("Accelerometer range set to: ");
  switch (mpu.getAccelerometerRange()) {
  case MPU6050_RANGE_2_G:
    Serial.println("+-2G");
    break;
  case MPU6050_RANGE_4_G:
    Serial.println("+-4G");
    break;
  case MPU6050_RANGE_8_G:
    Serial.println("+-8G");
    break;
  case MPU6050_RANGE_16_G:
    Serial.println("+-16G");
    break;
  }
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  Serial.print("Gyro range set to: ");
  switch (mpu.getGyroRange()) {
  case MPU6050_RANGE_250_DEG:
    Serial.println("+- 250 deg/s");
    break;
  case MPU6050_RANGE_500_DEG:
    Serial.println("+- 500 deg/s");
    break;
  case MPU6050_RANGE_1000_DEG:
    Serial.println("+- 1000 deg/s");
    break;
  case MPU6050_RANGE_2000_DEG:
    Serial.println("+- 2000 deg/s");
    break;
  }

  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);
  Serial.print("Filter bandwidth set to: ");
  switch (mpu.getFilterBandwidth()) {
  case MPU6050_BAND_260_HZ:
    Serial.println("260 Hz");
    break;
  case MPU6050_BAND_184_HZ:
    Serial.println("184 Hz");
    break;
  case MPU6050_BAND_94_HZ:
    Serial.println("94 Hz");
    break;
  case MPU6050_BAND_44_HZ:
    Serial.println("44 Hz");
    break;
  case MPU6050_BAND_21_HZ:
    Serial.println("21 Hz");
    break;
  case MPU6050_BAND_10_HZ:
    Serial.println("10 Hz");
    break;
  case MPU6050_BAND_5_HZ:
    Serial.println("5 Hz");
    break;
  }
  Serial.println("");
  delay(100);
}


/** 
 * @brief Read and print MPU6050 data
 */
void readMPU6050() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  /* Print out the values */
  Serial.print("Acceleration X: ");
  Serial.print(a.acceleration.x);
  accelerationX = a.acceleration.x;
  Serial.print(", Y: ");
  Serial.print(a.acceleration.y);
  accelerationY = a.acceleration.y;
  Serial.print(", Z: ");
  Serial.print(a.acceleration.z);
  accelerationZ = a.acceleration.z;
  Serial.println(" m/s^2");

  Serial.print("Rotation X: ");
  Serial.print(g.gyro.x);
  gyroX = g.gyro.x;
  Serial.print(", Y: ");
  Serial.print(g.gyro.y);
  gyroY = g.gyro.y;
  Serial.print(", Z: ");
  Serial.print(g.gyro.z);
  gyroZ = g.gyro.z;
  Serial.println(" rad/s");

  Serial.print("Temperature: ");
  Serial.print(temp.temperature);
  temperatureMPU = temp.temperature;
  Serial.println(" degC");

  Serial.println("");
  delay(500);
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

