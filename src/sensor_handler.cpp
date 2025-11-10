/**
 * @file sensor_handler.cpp
 * @brief Implementation for all integrated sensor data handling (MPU, MLX, AHT, SGP).
 *
 * NOTE: This code assumes the libraries are correctly installed via PlatformIO.
 */

#include "sensor_handler.h"

// --- Sensor Objects ---
// These objects must be defined here in the .cpp file, not in the .h file.
Adafruit_AHTX0 aht;
Adafruit_SGP30 sgp;
MPU6050 mpu;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// --- Global Variable Definition ---
// This DEFINES the global variable declared as 'extern' in the header.
AllSensorData_t currentSensorData;


/**
 * @brief Initializes all I2C sensors and other required hardware.
 * @return True if all initializations were successful, false otherwise.
 */
bool initAllSensors() {
    bool success = true;
    Serial.println("[SENSOR INIT] Initializing all I2C sensors...");
    Wire.begin(); // Start I2C bus

    // --- AHT10/AHTX0 Init ---
    if (!aht.begin()) {
        Serial.println("[SENSOR INIT] ERROR: AHT10/AHTX0 not found or failed initialization.");
        success = false;
    } else {
        Serial.println("[SENSOR INIT] AHT10/AHTX0: OK.");
    }

    // --- SGP30 Init ---
    if (!sgp.begin()) {
        Serial.println("[SENSOR INIT] ERROR: SGP30 not found or failed initialization.");
        success = false;
    } else {
        Serial.println("[SENSOR INIT] SGP30: OK. Starting baselining...");
        // This is important for the SGP30 to get stable readings
        sgp.setHumidity(120); // Dummy humidity value for baseline start
    }
    
    // --- MPU-6050 Init ---
    // Fix: initialize() returns void, so we call it, then check connection separately.
    mpu.initialize(); 
    if (!mpu.testConnection()) { 
        Serial.println("[SENSOR INIT] ERROR: MPU-6050 failed initialization or connection test.");
        success = false;
    } else {
        Serial.println("[SENSOR INIT] MPU-6050: OK.");
    }

    // --- MLX90614 Init ---
    if (!mlx.begin()) {
        Serial.println("[SENSOR INIT] ERROR: MLX90614 not found or failed initialization.");
        success = false;
    } else {
        Serial.println("[SENSOR INIT] MLX90614: OK.");
    }
    
    Serial.printf("[SENSOR INIT] All sensors initialization complete. Status: %s\n", success ? "SUCCESS" : "FAIL");
    return success;
}

/**
 * @brief Reads data from all connected sensors and updates the currentSensorData structure.
 * @return True if all sensor readings were successful, false otherwise.
 */
bool readAndProcessAllSensors() {
    bool success = true;
    int16_t ax, ay, az, gx, gy, gz; // MPU6050 raw data types
    sensors_event_t humidity, temp; // AHTX0 data types

    // --- 1. MPU-6050 Reading ---
    // Since we check connection in initAllSensors, we assume it is connected here for reading.
    // If you need maximum robustness, you could add mpu.testConnection() here too.
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // Convert raw integer data to float and store (using conversion factors specific to MPU setup if necessary)
    // For simplicity, we just cast to float here.
    currentSensorData.accelX = (float)ax / 16384.0; // Example sensitivity conversion
    currentSensorData.accelY = (float)ay / 16384.0;
    currentSensorData.accelZ = (float)az / 16384.0;
    currentSensorData.gyroX = (float)gx / 131.0; 
    currentSensorData.gyroY = (float)gy / 131.0;
    currentSensorData.gyroZ = (float)gz / 131.0;
    
    // --- 2. MLX90614 Reading ---
    currentSensorData.objectTempC = mlx.readObjectTempC();
    currentSensorData.ambientTempC = mlx.readAmbientTempC();
    if (isnan(currentSensorData.objectTempC) || isnan(currentSensorData.ambientTempC)) {
        Serial.println("[SENSOR READ] MLX90614 read error (returned NaN).");
        success = false;
    }

    // --- 3. AHT10 Reading ---
    if (aht.getEvent(&humidity, &temp)) {
        currentSensorData.aht_temperature = temp.temperature;
        currentSensorData.aht_humidity = humidity.relative_humidity;
    } else {
        Serial.println("[SENSOR READ] AHT10 read error.");
        success = false;
    }

    // --- 4. SGP30 Reading ---
    if (sgp.IAQmeasure()) {
        currentSensorData.tvoc = sgp.TVOC;
        currentSensorData.eco2 = sgp.eCO2;
        // Optionally update humidity for better SGP30 performance
        // sgp.setHumidity(currentSensorData.aht_humidity); 
    } else {
        Serial.println("[SENSOR READ] SGP30 read error.");
        success = false;
    }

    // --- 5. Finalizing Data ---
    currentSensorData.readTimestamp = millis();
    currentSensorData.scanSuccess = success;

    return success;
}

/**
 * @brief Prints the entire contents of the AllSensorData_t structure to Serial Monitor.
 */
void printAllSensorData() {
    Serial.println("--- Current Sensor Readings ---");
    Serial.printf("Timestamp: %lu ms\n", currentSensorData.readTimestamp);
    
    Serial.println(":: MPU-6050 (IMU) ::");
    Serial.printf("  Accel (X/Y/Z): %.2f / %.2f / %.2f g\n", currentSensorData.accelX, currentSensorData.accelY, currentSensorData.accelZ);
    Serial.printf("  Gyro (X/Y/Z): %.2f / %.2f / %.2f deg/s\n", currentSensorData.gyroX, currentSensorData.gyroY, currentSensorData.gyroZ);
    
    Serial.println(":: MLX90614 (Contactless Temp) ::");
    Serial.printf("  Object Temp: %.2f °C\n", currentSensorData.objectTempC);
    Serial.printf("  Ambient Temp: %.2f °C\n", currentSensorData.ambientTempC);

    Serial.println(":: AHT10 (T/H) ::");
    Serial.printf("  T (AHT): %.2f °C\n", currentSensorData.aht_temperature);
    Serial.printf("  H (AHT): %.2f %%\n", currentSensorData.aht_humidity);

    Serial.println(":: SGP30 (Air Quality) ::");
    Serial.printf("  TVOC: %u ppb\n", currentSensorData.tvoc);
    Serial.printf("  eCO2: %u ppm\n", currentSensorData.eco2);

    Serial.println("--------------------------------\n");
}