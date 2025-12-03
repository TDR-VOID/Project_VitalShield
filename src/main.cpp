#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SGP30.h>

#include <HardwareSerial.h>

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <time.h>


// ===================== CONFIGURE HERE =====================
#define WIFI_SSID      "2263081slt"
#define WIFI_PASSWORD  "199202FJ5"

#define API_KEY        "AIzaSyB6n9Dv6ANuWBl5qLZ6AqusQCsGBuIQpzU"
#define DATABASE_URL   "https://uoc-project-acf0b-default-rtdb.firebaseio.com/"

#define USER_EMAIL     "test@user.com"
#define USER_PASSWORD  "testpass"

#define USER_NAME      "User1"

#define TARGET_PHONE_NUMBER "+94769054603"

// LED Pin Configuration
#define LED_STATUS_PIN   14   // D14 - Status LED (always on)
#define LED_DATA_PIN     12   // D12 - Data activity LED (blinks on data transfer)

// ========================================================== //


// --- Firebase objects ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- Sensor objects ---
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_AHTX0 aht;
Adafruit_MPU6050 mpu;
Adafruit_SGP30 sgp;

// --- SIM800A objects ---
HardwareSerial simSerial(2); // Define the serial port for SIM800A, using UART2, RX2=16, TX2=17

// --- Global Variables ---
float ambient;
float object;
float relative_humidity;
float temperature;
float accelerationX, accelerationY, accelerationZ;
float gyroX, gyroY, gyroZ; 
float temperatureMPU;
String Action_1, Action_2, Action_3, Action_4, Action_5;

// SGP30 Gas Sensor Variables
uint16_t TVOC = 0;  // Total Volatile Organic Compounds (ppb)
uint16_t eCO2 = 0;  // Equivalent CO2 (ppm)

// Sensor Status Variables
String status_AHT10 = "Initializing";
String status_MLX90614 = "Initializing";
String status_MPU6050 = "Initializing";
String status_SGP30 = "Initializing";

// ML Training data tracking
int mlDataCount = 0;
const int MAX_ML_RECORDS = 100;
unsigned long firstRecordTime = 0;

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
void initSGP30();
void readSGP30();
void readFirebaseActions();
void saveFirebaseActions();
void saveToFirestore();
void getFormattedDateTime(char* buffer, size_t bufferSize);
void manageMLDataRotation();
void syncTimeWithNTP();
void updateSensorStatusToFirebase();
void initLEDs();
void ledDataBlink();
void sim800a_init();
void send_sms(String phoneNumber, String message);
bool checkResponse(String expected, unsigned int timeout);
void Alert_MSG();
// ------------------------------------------------------------------ //

void setup(){
  Serial.begin(115200);
  Wire.begin(); // Start I2C communication
  Serial.println("\n--- Starting Dual-Core IoT Task Setup ---");

  // Initialize LEDs
  initLEDs();

  // Start the serial communication with the SIM800A module
  simSerial.begin(9600, SERIAL_8N1, 16, 17); // RX, T

  sim800a_init(); // Initialize SIM800A module
  initWifi(); // Initialize WiFi
  initFirebase(); // Initialize Firebase
  initAHT10(); // Initialize AHT10 Sensor 
  initMLX90614(); // Call the initialization function
  initMPU6050(); // Initialize MPU6050 Sensor
  initSGP30(); // Initialize SGP30 Gas Sensor
  syncTimeWithNTP(); // Synchronize time with NTP server



  // ----------------------------------------
  // 1. Sensor Readings Task (Pinned to Core 1)
  // Handles fast, dedicated sensor acquisition.
  // ----------------------------------------
  xTaskCreatePinnedToCore(
    TaskSensorReadings,      // Function to implement the task
    "Sensor_Reader",         // Name of the task
    8192,                    // Increased Stack size (8KB) for multiple sensor libraries
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
    12288,                   // Increased Stack size (12KB - more stack for network ops)
    NULL,                    // Task input parameter
    1,                       // Priority
    NULL,                    // Task handle
    0                        // Core to pin the task to (0 = Core 0)
  );
  Serial.println("[SETUP] Firebase Task created on Core 0.");
}


void loop() {
  // Use the main loop for simple, low-priority status/health checks.
  Serial.print("[LOOP] System Status - Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" bytes | Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" seconds");
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
    readSGP30(); // Call the SGP30 reading function
    delay(1000);
    vTaskDelay(pdMS_TO_TICKS(2000)); 
  }
}

/**
 * @brief Task 2: Runs on Core 0, dedicated to Firebase/network communication.
 */
void TaskFirebaseSender(void * parameter) {
  Serial.println("[CORE 0 - FIREBASE] Task started.");
  
  // Reduce buffer size to prevent blocking
  fbdo.setBSSLBufferSize(512, 1024);
  
  // Flag to track if status was updated on first run
  static bool firstRun = true;

  for (;;) {
    // 0. On first run, upload sensor status
    if (firstRun) {
      updateSensorStatusToFirebase();
      firstRun = false;
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 1. Save sensor data to Firebase
    saveFirebaseActions();
    vTaskDelay(pdMS_TO_TICKS(100)); // Yield to watchdog
    
    // 2. Save data to ML Training collection with timestamp
    saveToFirestore();
    vTaskDelay(pdMS_TO_TICKS(100)); // Yield to watchdog

    // 3. Read action commands from Firebase
    readFirebaseActions();
    vTaskDelay(pdMS_TO_TICKS(100)); // Yield to watchdog

    // 4. Send SMS alerts based on actions
    Alert_MSG();
    vTaskDelay(pdMS_TO_TICKS(100)); // Yield to watchdog

    // 5. Task Delay - This task runs every ~5 seconds total
    vTaskDelay(pdMS_TO_TICKS(4600)); 
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

  if (auth.token.uid.length() > 0) {
    Serial.print("User UID: ");
    Serial.println(auth.token.uid.c_str());
  }
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
    status_MPU6050 = "Not Working";
    return; // Return but don't halt - sensor is optional
  }
  
  Serial.println("MPU6050 Found!");
  status_MPU6050 = "Working";

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
    status_AHT10 = "Working";
  } else {
    Serial.println("AHT10/AHTX0 Connection FAILED. Check wiring/address.");
    status_AHT10 = "Not Working";
    // Continue without halting - sensor is optional
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
 * @brief Synchronize time with NTP server (Sri Lanka Time: UTC+5:30)
 */
void syncTimeWithNTP() {
  Serial.println("Synchronizing time with NTP server (Sri Lanka Time UTC+5:30)...");
  
  // Configure time with NTP server
  // Sri Lanka Timezone: UTC+5:30 (5 hours 30 minutes)
  // Parameters: (gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3)
  configTime(5 * 3600 + 30 * 60, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  
  Serial.print("Waiting for NTP time sync: ");
  
  time_t now = time(nullptr);
  int timeout = 30; // 30 seconds timeout
  
  while (now < 24 * 3600 && timeout > 0) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    timeout--;
  }
  
  Serial.println();
  
  if (now > 24 * 3600) {
    struct tm* timeinfo = localtime(&now);
    Serial.print("Time synchronized! Sri Lanka Time: ");
    Serial.println(asctime(timeinfo));
  } else {
    Serial.println("WARNING: Failed to synchronize time with NTP. Using default time.");
  }
}

/**
 * @brief Initialize LED pins
 */
void initLEDs() {
  Serial.println("\n--- Initializing LEDs ---");
  
  // Configure LED pins as outputs
  pinMode(LED_STATUS_PIN, OUTPUT);
  pinMode(LED_DATA_PIN, OUTPUT);
  
  // Turn off both LEDs initially
  digitalWrite(LED_STATUS_PIN, LOW);
  digitalWrite(LED_DATA_PIN, LOW);
  
  delay(500);
  
  // Turn on status LED (stays on)
  digitalWrite(LED_STATUS_PIN, HIGH);
  Serial.print("Status LED (D");
  Serial.print(LED_STATUS_PIN);
  Serial.println(") turned ON");
  
  Serial.print("Data LED (D");
  Serial.print(LED_DATA_PIN);
  Serial.println(") ready for data activity");
}

/**
 * @brief Blink data LED to indicate Firebase data activity
 */
void ledDataBlink() {
  // Quick blink pattern: ON for 100ms, OFF for 100ms
  digitalWrite(LED_DATA_PIN, HIGH);
  delay(100);
  digitalWrite(LED_DATA_PIN, LOW);
  delay(100);
}

/**
 * @brief Initialize the MLX90614 sensor
 */
void initMLX90614() {
  Serial.println("\n--- MLX90614 Initialization ---");

  if (mlx.begin()) {
    Serial.println("✅ MLX90614 Connection Successful!");
    Serial.println("Ambient and Object temperatures will be displayed.\n");
    status_MLX90614 = "Working";
  } else {
    Serial.println("❌ MLX90614 Connection FAILED. Check wiring/address.");
    status_MLX90614 = "Not Working";
    // Continue without halting - sensor is optional
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


/**
 * @brief Initialize the SGP30 Air Quality Sensor
 */
void initSGP30() {
  Serial.println("\n--- SGP30 Initialization ---");

  if (!sgp.begin()) {
    Serial.println("❌ SGP30 Connection FAILED. Check wiring/address (0x58).");
    status_SGP30 = "Not Working";
    // Continue without halting - sensor is optional
  } else {
    Serial.println("✅ SGP30 Connection Successful!");
    Serial.print("Found SGP30 serial #");
    Serial.print(sgp.serialnumber[0], HEX);
    Serial.print(sgp.serialnumber[1], HEX);
    Serial.println(sgp.serialnumber[2], HEX);
    status_SGP30 = "Working";
    
    // Set humidity compensation (optional but recommended)
    // Uses AHT10 humidity data for better accuracy
    sgp.setIAQBaseline(0x8E68, 0x8F41); // Optional baseline values
  }
}


/**
 * @brief Read and display SGP30 Air Quality data
 */
void readSGP30() {
  // SGP30 should be read every 1 second
  if (!sgp.IAQmeasure()) {
    Serial.println("⚠️ Failed to read SGP30 data!");
    return;
  }
  
  TVOC = sgp.TVOC;
  eCO2 = sgp.eCO2;
  
  Serial.print("TVOC: ");
  Serial.print(TVOC);
  Serial.print(" ppb\teCO2: ");
  Serial.print(eCO2);
  Serial.println(" ppm");
  
  // Optional: Get baseline values for calibration
  uint16_t baselineECO2, baselineTVOC;
  static unsigned long lastBaselineTime = 0;
  
  if (millis() - lastBaselineTime > 30000) {
    if (sgp.getIAQBaseline(&baselineECO2, &baselineTVOC)) {
      Serial.print("SGP30 Baseline - eCO2: 0x");
      Serial.print(baselineECO2, HEX);
      Serial.print(" TVOC: 0x");
      Serial.println(baselineTVOC, HEX);
    }
    lastBaselineTime = millis();
  }
}



/**
 * @brief Read and display Firebase action data 
 */
void readFirebaseActions() {
  // Blink data LED to indicate Firebase activity
  ledDataBlink();
  
  char actionPaths[5][50] = {
    "",
    "",
    "",
    "",
    ""
  };
  
  // Build paths using sprintf
  sprintf(actionPaths[0], "%s/Actions/action_1", USER_NAME);
  sprintf(actionPaths[1], "%s/Actions/action_2", USER_NAME);
  sprintf(actionPaths[2], "%s/Actions/action_3", USER_NAME);
  sprintf(actionPaths[3], "%s/Actions/action_4", USER_NAME);
  sprintf(actionPaths[4], "%s/Actions/action_5", USER_NAME);

  for (int i = 0; i < 5; i++) {
    if (Firebase.RTDB.getString(&fbdo, actionPaths[i])) {
      String actionValue = fbdo.stringData();
      Serial.print(actionPaths[i]);
      Serial.print(" = ");
      Serial.println(actionValue);

      // Store the action values in corresponding global variables
      switch (i) {
        case 0: Action_1 = actionValue; break;
        case 1: Action_2 = actionValue; break;
        case 2: Action_3 = actionValue; break;
        case 3: Action_4 = actionValue; break;
        case 4: Action_5 = actionValue; break;
      }
    } else {
      Serial.print("Failed to read ");
      Serial.print(actionPaths[i]);
      Serial.print(" - ");
      Serial.println(fbdo.errorReason());
    }
    // Yield to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/**
 * @brief Save sensor data back to Firebase
 */

void saveFirebaseActions() {
  // Example function to save actions back to Firebase if needed

  // ---- Create JSON payload ----

    FirebaseJson AHT10_json, MLX90614_json, MPU6050_json, SGP30_json;

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

    SGP30_json.set("TVOC", TVOC);
    SGP30_json.set("eCO2", eCO2);

    // ---- Upload AHT10 data to Firebase ----
    char aht10Path[50];
    sprintf(aht10Path, "%s/Sensor_Data/AHT10", USER_NAME);
    if (Firebase.RTDB.setJSON(&fbdo, aht10Path, &AHT10_json)) {
      Serial.println("Uploaded AHT10 data to Firebase");
    } else {
      Serial.print("Upload failed: ");
      Serial.println(fbdo.errorReason());
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // ---- Upload MLX90614 data to Firebase ---- 
    char mlx90614Path[50];
    sprintf(mlx90614Path, "%s/Sensor_Data/MLX90614", USER_NAME);
    if (Firebase.RTDB.setJSON(&fbdo, mlx90614Path, &MLX90614_json)) {
      Serial.println("Uploaded MLX90614 data to Firebase");
    } else {
      Serial.print("Upload failed: ");
      Serial.println(fbdo.errorReason());
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // ---- Upload MPU6050 data to Firebase ---- 
    char mpu6050Path[50];
    sprintf(mpu6050Path, "%s/Sensor_Data/MPU6050", USER_NAME);
    if (Firebase.RTDB.setJSON(&fbdo, mpu6050Path, &MPU6050_json)) {
      Serial.println("Uploaded MPU6050 data to Firebase");
    } else {
      Serial.print("Upload failed: ");
      Serial.println(fbdo.errorReason());
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // ---- Upload SGP30 data to Firebase ---- 
    char sgp30Path[50];
    sprintf(sgp30Path, "%s/Sensor_Data/SGP30", USER_NAME);
    if (Firebase.RTDB.setJSON(&fbdo, sgp30Path, &SGP30_json)) {
      Serial.println("Uploaded SGP30 data to Firebase");
    } else {
      Serial.print("Upload failed: ");
      Serial.println(fbdo.errorReason());
    }
    vTaskDelay(pdMS_TO_TICKS(50));

}

/**
 * @brief Get formatted date and time string
 */
void getFormattedDateTime(char* buffer, size_t bufferSize) {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  
  strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", timeinfo);
}

/**
 * @brief Manage ML training data rotation - keep only latest 100 records
 * Uses a simple counter approach - stores metadata about record count
 */
void manageMLDataRotation() {
  char metaPath[80];
  sprintf(metaPath, "%s/ML_Training_Meta/record_count", USER_NAME);
  
  // Read current count
  if (Firebase.RTDB.getInt(&fbdo, metaPath)) {
    mlDataCount = fbdo.intData();
  } else {
    mlDataCount = 0;
  }
  
  // Increment count
  mlDataCount++;
  
  // If we exceed MAX_ML_RECORDS, reset to 1 (will overwrite oldest)
  if (mlDataCount > MAX_ML_RECORDS) {
    Serial.print("[ML Data] Rotating records - exceeded ");
    Serial.print(MAX_ML_RECORDS);
    Serial.println(" records. Starting new cycle.");
    mlDataCount = 1;
    
    // Clear old ML data folder to start fresh
    char clearPath[80];
    sprintf(clearPath, "%s/ML_Training_Data", USER_NAME);
    Firebase.RTDB.deleteNode(&fbdo, clearPath);
  }
  
  // Update count in database
  char countPath[80];
  sprintf(countPath, "%s/ML_Training_Meta/record_count", USER_NAME);
  Firebase.RTDB.setInt(&fbdo, countPath, mlDataCount);
}

/**
 * @brief Save sensor data to Firebase with timestamp and readable date/time for ML model training
 */
void saveToFirestore() {
  // Manage rotation first
  manageMLDataRotation();
  
  // Get current timestamp in milliseconds
  unsigned long timestamp = millis();
  
  // Create formatted date/time string
  char dateTimeStr[25];
  getFormattedDateTime(dateTimeStr, sizeof(dateTimeStr));
  
  // Create a new document with record number as ID (ensures ordering)
  char docId[20];
  sprintf(docId, "record_%03d", mlDataCount);
  
  char rtdbPath[150];
  sprintf(rtdbPath, "%s/ML_Training_Data/%s", USER_NAME, docId);

  // Create JSON payload with all sensor data and timestamp
  FirebaseJson firestoreData;
  
  // Add timestamp (numeric)
  firestoreData.set("timestamp_ms", (double)timestamp);
  
  // Add readable date/time
  firestoreData.set("datetime", dateTimeStr);
  
  // Add AHT10 data
  FirebaseJson aht10_obj;
  aht10_obj.set("humidity", relative_humidity);
  aht10_obj.set("temperature", temperature);
  firestoreData.set("AHT10", aht10_obj);
  
  // Add MLX90614 data
  FirebaseJson mlx90614_obj;
  mlx90614_obj.set("ambient", ambient);
  mlx90614_obj.set("object", object);
  firestoreData.set("MLX90614", mlx90614_obj);
  
  // Add MPU6050 data
  FirebaseJson mpu6050_obj;
  mpu6050_obj.set("accel_x", accelerationX);
  mpu6050_obj.set("accel_y", accelerationY);
  mpu6050_obj.set("accel_z", accelerationZ);
  mpu6050_obj.set("gyro_x", gyroX);
  mpu6050_obj.set("gyro_y", gyroY);
  mpu6050_obj.set("gyro_z", gyroZ);
  mpu6050_obj.set("temperature", temperatureMPU);
  firestoreData.set("MPU6050", mpu6050_obj);

  // Add SGP30 Air Quality data
  FirebaseJson sgp30_obj;
  sgp30_obj.set("tvoc", TVOC);
  sgp30_obj.set("eco2", eCO2);
  firestoreData.set("SGP30", sgp30_obj);

  // Add action states for context
  FirebaseJson actions_obj;
  actions_obj.set("action_1", Action_1);
  actions_obj.set("action_2", Action_2);
  actions_obj.set("action_3", Action_3);
  actions_obj.set("action_4", Action_4);
  actions_obj.set("action_5", Action_5);
  firestoreData.set("Actions", actions_obj);

  // Save to Realtime Database
  if (Firebase.RTDB.setJSON(&fbdo, rtdbPath, &firestoreData)) {
    Serial.print("[ML Data] Saved at ");
    Serial.print(dateTimeStr);
    Serial.print(" (Record ");
    Serial.print(mlDataCount);
    Serial.println("/100)");
  } else {
    Serial.print("[ML Data] Failed to save: ");
    Serial.println(fbdo.errorReason());
  }
}

// ----------------------------------------------------------------
// FUNCTION: Update Sensor Status to Firebase
// ----------------------------------------------------------------
void updateSensorStatusToFirebase() {
  // Create JSON payload with sensor statuses
  FirebaseJson statusJson;
  
  statusJson.set("AHT10", status_AHT10);
  statusJson.set("MLX90614", status_MLX90614);
  statusJson.set("MPU6050", status_MPU6050);
  statusJson.set("SGP30", status_SGP30);
  
  // Get current time for last update
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timeStr[25];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
  statusJson.set("last_update", timeStr);

  // Upload to Firebase
  char statusPath[60];
  sprintf(statusPath, "%s/Sensor_Status", USER_NAME);
  
  if (Firebase.RTDB.setJSON(&fbdo, statusPath, &statusJson)) {
    Serial.println("[Sensor Status] Updated to Firebase");
    Serial.print("  AHT10: ");
    Serial.print(status_AHT10);
    Serial.print(" | MLX90614: ");
    Serial.print(status_MLX90614);
    Serial.print(" | MPU6050: ");
    Serial.print(status_MPU6050);
    Serial.print(" | SGP30: ");
    Serial.println(status_SGP30);
  } else {
    Serial.print("[Sensor Status] Failed to update: ");
    Serial.println(fbdo.errorReason());
  }
  
  vTaskDelay(pdMS_TO_TICKS(50)); // Yield
}

// ----------------------------------------------------------------
// FUNCTION: Initialize the SIM800A
// ----------------------------------------------------------------
void sim800a_init() {
  Serial.println("Initializing SIM800A...");
  
  // Give the module time to boot
  delay(3000); 

  // Send "AT" command to check connection and sync baud rate
  simSerial.println("AT");
  if (!checkResponse("OK", 2000)) {
    Serial.println("Error: No response from SIM800A. Check wiring and power.");
    // You might want to halt here or retry
    //while(1);
  }
  Serial.println("Module is responding.");

  // Set SMS mode to Text Mode
  simSerial.println("AT+CMGF=1");
  if (!checkResponse("OK", 2000)) {
    Serial.println("Error: Failed to set SMS to text mode.");
  } else {
    Serial.println("SIM800A initialized successfully in text mode.");
  }

  // Optional: Set character set to GSM (default)
  // simSerial.println("AT+CSCS=\"GSM\"");
  // checkResponse("OK", 1000);
}

// ----------------------------------------------------------------
// FUNCTION: Send an SMS
// ----------------------------------------------------------------
void send_sms(String phoneNumber, String message) {
  Serial.print("Attempting to send SMS to: ");
  Serial.println(phoneNumber);

  // 1. Set the destination phone number
  simSerial.print("AT+CMGS=\"");
  simSerial.print(phoneNumber);
  simSerial.println("\"");

  // 2. Wait for the ">" prompt from the module
  if (checkResponse(">", 2000)) {
    Serial.println("Module is ready to accept message text.");
    
    // 3. Send the message text
    simSerial.print(message);
    
    // 4. Send the "Ctrl+Z" character (ASCII 26) to send the message
    simSerial.write(26);

    // 5. Wait for the "+CMGS:" confirmation
    // This can take a while (up to 10 seconds)
    if (checkResponse("+CMGS:", 10000)) {
      Serial.println("SMS sent successfully!");
    } else {
      Serial.println("Error: Failed to send SMS. No +CMGS confirmation.");
    }
  } else {
    Serial.println("Error: Did not receive '>' prompt. Module not ready.");
  }
}

// ----------------------------------------------------------------
// HELPER FUNCTION: Check module response
// Waits for a specific string in the module's serial reply.
// ----------------------------------------------------------------
bool checkResponse(String expected, unsigned int timeout) {
  String response = "";
  long int startTime = millis();

  while ((millis() - startTime) < timeout) {
    while (simSerial.available()) {
      char c = simSerial.read();
      response += c;
    }
    // Check if the expected string is in the response
    if (response.indexOf(expected) != -1) {
      Serial.print("Module Response: ");
      Serial.println(response); // Print the full response for debugging
      return true;
    }
  }
  
  // If we timed out
  Serial.print("Timeout/Error. Module Response: ");
  Serial.println(response); // Print whatever we got
  return false;
}


void Alert_MSG() {
  if (Action_1 == "ON") {
    send_sms(TARGET_PHONE_NUMBER, "Alert: Action 1 Triggered!");
  }
  if (Action_2 == "ON") {
    send_sms(TARGET_PHONE_NUMBER, "Alert: Action 2 Triggered!");
  }
  if (Action_3 == "ON") {
    send_sms(TARGET_PHONE_NUMBER, "Alert: Action 3 Triggered!");
  }
  if (Action_4 == "ON") {
    send_sms(TARGET_PHONE_NUMBER, "Alert: Action 4 Triggered!");
  }
  if (Action_5 == "ON") {
    send_sms(TARGET_PHONE_NUMBER, "Alert: Action 5 Triggered!");
  }

}