/**
 * @file sensor_handler.h
 * @brief Declarations for all integrated sensor data structure and reading function.
 */

#ifndef SENSOR_HANDLER_H
#define SENSOR_HANDLER_H

#include <Arduino.h>
#include <Wire.h> // Required for all I2C sensors (MPU, MLX, AHT, SGP)

// We'll assume you are using popular libraries, which you'd need to install 
// via PlatformIO's Library Manager for the full project:
#include <Adafruit_AHTX0.h>    // For AHT10/AHT20 - Corrected library name
#include <Adafruit_SGP30.h>    // For SGP30
#include <MPU6050.h>           // For MPU-6050
#include <Adafruit_MLX90614.h> // For MLX90614 - Corrected library name

// --- 1. Consolidated Data Structure ---
/**
 * @brief Structure to hold the latest readings from ALL integrated sensors.
 */
typedef struct {
    // MPU-6050 (Accelerometer/Gyroscope)
    float accelX, accelY, accelZ;
    float gyroX, gyroY, gyroZ;
    
    // MLX90614 (Contactless Temperature)
    float objectTempC;  // Object temperature
    float ambientTempC; // Ambient temperature

    // AHT10 (High Precision Digital Temperature and Humidity)
    float aht_temperature;
    float aht_humidity;

    // SGP30 (Gas Sensor)
    uint16_t tvoc; // Total Volatile Organic Compounds
    uint16_t eco2; // Equivalent CO2
    
    // General
    long readTimestamp; // Time of last successful reading
    bool scanSuccess;   // Flag to indicate if the reading was successful
} AllSensorData_t;


// --- 2. External Declaration for the Data Storage ---
// The actual instance of this structure will be defined in a .cpp file 
// (or main.cpp) and accessed here using 'extern'.
extern AllSensorData_t currentSensorData;


// --- 3. Function Declaration ---
/**
 * @brief Initializes all I2C sensors and other required hardware.
 * @return True if all initializations were successful, false otherwise.
 */
bool initAllSensors();

/**
 * @brief Reads data from all connected sensors and updates the currentSensorData structure.
 * @return True if all sensor readings were successful, false otherwise.
 */
bool readAndProcessAllSensors();

/**
 * @brief Prints the entire contents of the AllSensorData_t structure to Serial Monitor.
 */
void printAllSensorData();

#endif // SENSOR_HANDLER_H