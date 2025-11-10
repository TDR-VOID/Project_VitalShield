/**
 * @file i2c_scanner.h
 * @brief Header file for I2C scanning functionality.
 * * Declares the function used to scan the I2C bus for connected devices.
 */

#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

// Required libraries are often included in the header when dealing with Arduino core types
#include <Arduino.h>
#include <Wire.h>

/**
 * @brief Scans the I2C bus (addresses 1 to 126) and prints the addresses of any found devices.
 */
void scanI2CDevices();

#endif // I2C_SCANNER_H