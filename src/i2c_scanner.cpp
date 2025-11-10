/**
 * @file i2c_scanner.cpp
 * @brief Implementation file for I2C scanning functionality.
 * * Contains the definition (actual code) for the I2C scanning function.
 */

// Include the corresponding header file. This provides the declaration
// and ensures we have access to <Wire.h> and <Arduino.h>
#include "i2c_scanner.h" 

/**
 * @brief Scans the I2C bus (addresses 1 to 126) and prints the addresses of any found devices.
 */
void scanI2CDevices() {
  byte error, address;
  int nDevices = 0;

  Serial.println("\n--- I2C Scanner starting ---");

  // Iterate through all possible I2C addresses (1 to 126)
  for (address = 1; address < 127; address++) {
    // Start transmission to the current address
    Wire.beginTransmission(address);
    // End transmission and check for error status
    error = Wire.endTransmission();

    if (error == 0) {
      // Device acknowledged (found)
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0"); // Pad with leading zero if needed
      Serial.print(address, HEX);
      Serial.println();
      nDevices++;
    } else if (error == 4) {
      // Unknown error (sometimes indicates a lock-up)
      Serial.print("Unknown error at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  if (nDevices == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.print("Scan complete. Total devices found: ");
    Serial.println(nDevices);
  }
  Serial.println("----------------------------\n");
}