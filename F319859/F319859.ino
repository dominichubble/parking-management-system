/**
 * Parking Management System
 * This program manages a parking system for vehicles, tracking their registration,
 * location, type, and payment status. It features an LCD interface, EEPROM storage,
 * and real-time data processing.
 */

#include <Arduino.h>                // Essential Arduino library
#include <Wire.h>                   // Wire library for I2C communication
#include <Adafruit_RGBLCDShield.h>  // Library for the Adafruit RGB LCD Shield
#include <TimeLib.h>                // Time library for timestamp handling
#include <EEPROM.h>                 // EEPROM library for data storage
#include <MemoryFree.h>             // Library to check free memory on Arduino

// Color definitions for the LCD backlight
#define RED 1
#define GREEN 2
#define YELLOW 3
#define BLUE 4
#define PURPLE 5
#define LIGHTBLUE 6
#define WHITE 7
#define MAX_VEHICLES 15  // Maximum number of vehicles that can be tracked

Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();  // Initializing the LCD shield

// Structures to define vehicle and EEPROM storage formats
struct Vehicle {
  String regNumber;
  String location;
  char type;
  String paymentStatus;
  String entryTimestamp;
  String exitTimestamp;
};
struct EepromVehicle {
  char regNumber[8];
  char location[12];
  char type;
  char paymentStatus[4];
  char entryTimestamp[5];
  char exitTimestamp[5];
};
struct EepromTime {
  unsigned long timestamp;
};

// Enum for filtering vehicles based on payment status
enum FilterMode { NONE,
                  PD,
                  NPD };
FilterMode currentFilter = NONE;

// Indices for managing filtered lists and current vehicle display
int pdVehicleIndex = 0;
int npdVehicleIndex = 0;
Vehicle vehicles[MAX_VEHICLES];
int vehicleCount = 0;
int currentVehicleIndex = 0;

// Control flags for scrolling and ID display
bool shouldScroll = true;
bool displayID = false;
unsigned long lastScrollTime = 0;
const int scrollDelay = 500;
int scrollPosition = 0;
String currentLocation = "";


// Setup routine for Arduino
void setup() {
  Serial.begin(9600);        // Initialize serial communication
  lcd.begin(16, 2);          // Initialize LCD display
  lcd.setBacklight(PURPLE);  // Set initial LCD backlight color

  // Synchronization with external system
  lcd.print("Sync...");
  while (true) {
    Serial.print('Q');  // Send synchronization character
    delay(1000);        // Delay for 1 second

    // Check for synchronization acknowledgement
    if (Serial.available()) {
      char receivedChar = Serial.read();
      if (receivedChar == 'X') break;  // Exit loop if sync is confirmed
      // Error handling for invalid sync characters
      if (receivedChar == '\n' || receivedChar == '\r') {
        lcd.clear();
        handleError(F("Newline/CR during sync"));
        while (true)
          ;  // Halt program on error
      }
    }
  }

  lcd.clear();  // Clear LCD after sync
  Serial.println(F("UDCHARS,FREERAM,HCI,EEPROM,SCROLL"));
  lcd.setBacklight(WHITE);  // Set LCD backlight to white

  // Load vehicle data and time from EEPROM
  for (int i = 0; i < MAX_VEHICLES; i++) {
    loadVehicleFromEEPROM(i, vehicles[i]);
    if (vehicles[i].regNumber[0] != '\0') vehicleCount++;
  }
  loadCurrentTimeFromEEPROM();

  // Set default time if not set
  if (now() == 0) setTime(0, 0, 0, 1, 1, 2023);

  // Initialize display filter and display first vehicle
  currentFilter = NPD;
  pdVehicleIndex = npdVehicleIndex = 0;
  displayVehicle();
}

// Main loop for handling tasks
void loop() {
  // Save current time to EEPROM every 60 seconds
  static unsigned long lastTimeSave = 0;
  if (millis() - lastTimeSave > 60000) {
    saveCurrentTimeToEEPROM();
    lastTimeSave = millis();
  }

  processSerialMessage();  // Handle serial communication
  handleButtonPresses();   // Handle user interactions

  // Scroll display if needed and not in ID display mode
  if (!displayID) scrollLocation();
}


// Function to display vehicle information based on the current filter
void displayVehicle() {
  if (currentFilter != NONE) {
    bool found = false;
    int index = (currentFilter == PD) ? pdVehicleIndex : npdVehicleIndex;
    int tempIndex = 0;

    // Iterate over vehicles to find the one matching the filter criteria
    for (int i = 0; i < vehicleCount; ++i) {
      if ((currentFilter == PD && vehicles[i].paymentStatus == "PD ") || (currentFilter == NPD && vehicles[i].paymentStatus == "NPD")) {
        if (tempIndex == index) {
          // Update location and scroll position for the matched vehicle
          currentLocation = vehicles[i].location;
          scrollPosition = 0;
          shouldScroll = currentLocation.length() > 7;  // Check for scrolling necessity
          displayVehicleData(vehicles[i], true, tempIndex);
          found = true;
          break;
        }
        tempIndex++;
      }
    }

    // Handle case when no vehicle matches the filter criteria
    if (!found) {
      lcd.clear();
      lcd.print(currentFilter == PD ? "NO PD VEHICLES" : "NO NPD VEHICLES");
      lcd.setBacklight(WHITE);  // Set appropriate backlight color
      shouldScroll = false;     // Disable scrolling in this case
    }
  } else {
    // Display vehicle information for unfiltered list
    if (currentVehicleIndex < vehicleCount) {
      currentLocation = vehicles[currentVehicleIndex].location;
      scrollPosition = 0;
      shouldScroll = currentLocation.length() > 7;
      displayVehicleData(vehicles[currentVehicleIndex], false, currentVehicleIndex);
    } else {
      // Handle case when there are no vehicles
      lcd.clear();
      lcd.print("NO VEHICLES");
      lcd.setBacklight(WHITE);
      shouldScroll = false;
    }
  }
}

// Function to handle button presses on the LCD shield
void handleButtonPresses() {
  static unsigned long buttonPressStartTime = 0;
  static bool isButtonHeld = false;

  // Handle 'UP' button press
  if (lcd.readButtons() & BUTTON_UP) {
    // Navigate to the previous vehicle based on the current filter
    if (currentFilter == NONE && currentVehicleIndex > 0) currentVehicleIndex--;
    else decrementFilteredIndex();
    displayVehicle();
  }

  // Handle 'DOWN' button press
  if (lcd.readButtons() & BUTTON_DOWN) {
    // Navigate to the next vehicle based on the current filter
    if (currentFilter == NONE && currentVehicleIndex < vehicleCount - 1) currentVehicleIndex++;
    else incrementFilteredIndex();
    displayVehicle();
  }

  // Handle 'RIGHT' button press to filter PD vehicles
  if (lcd.readButtons() & BUTTON_RIGHT && currentFilter != PD) {
    currentFilter = PD;
    pdVehicleIndex = 0;  // Reset PD vehicle index
    displayVehicle();
  }

  // Handle 'LEFT' button press to filter NPD vehicles
  if (lcd.readButtons() & BUTTON_LEFT && currentFilter != NPD) {
    currentFilter = NPD;
    npdVehicleIndex = 0;  // Reset NPD vehicle index
    displayVehicle();
  }

  // Handle 'SELECT' button press for displaying ID and free RAM
  if (lcd.readButtons() & BUTTON_SELECT) {
    if (!isButtonHeld) {
      buttonPressStartTime = millis();
      isButtonHeld = true;
    } else if (millis() - buttonPressStartTime > 1000 && !displayID) {
      lcd.clear();
      lcd.setBacklight(PURPLE);
      lcd.print("F319859");  // Display student ID
      lcd.setCursor(0, 1);
      lcd.print("Free RAM: ");
      lcd.print(freeMemory());
      displayID = true;
    }
  } else if (isButtonHeld) {
    // Return to vehicle display when button is released
    displayID = false;
    lcd.clear();
    lcd.setBacklight(WHITE);
    displayVehicle();
    isButtonHeld = false;
  }
}

// Process incoming serial messages
void processSerialMessage() {
  if (Serial.available()) {
    String fullMessage = Serial.readString();  // Read message from serial
    fullMessage.trim();                        // Remove whitespace

    // Check message length before processing
    if (fullMessage.length() > 0) {
      char operation = fullMessage.charAt(0);  // Get operation code

      // Handle different operations
      switch (operation) {
        case 'A': processAmessage(fullMessage); break;
        case 'S': processSmessage(fullMessage); break;
        case 'T': processTmessage(fullMessage); break;
        case 'L': processLmessage(fullMessage); break;
        case 'R': processRmessage(fullMessage); break;
        default: handleError(F("Invalid operation"));
      }
    }
  }
}


// Function to save the current system time to EEPROM.
void saveCurrentTimeToEEPROM() {
  EepromTime eepromTime;
  eepromTime.timestamp = now();  // Get current system time.

  // Calculate EEPROM address offset based on the size of stored vehicles.
  int timeAddress = MAX_VEHICLES * sizeof(EepromVehicle);
  EEPROM.put(timeAddress, eepromTime);  // Save timestamp to EEPROM.
}

// Function to load the current system time from EEPROM.
void loadCurrentTimeFromEEPROM() {
  EepromTime eepromTime;

  // Calculate EEPROM address offset where the time is stored.
  int timeAddress = MAX_VEHICLES * sizeof(EepromVehicle);
  EEPROM.get(timeAddress, eepromTime);  // Retrieve the timestamp from EEPROM.

  setTime(eepromTime.timestamp);  // Set the system time to the retrieved timestamp.
}

// Function to save a vehicle's data to EEPROM.
void saveVehicleToEEPROM(int index, const Vehicle& vehicle) {
  // Validate index to ensure it's within the range of allowed vehicles.
  if (index < 0 || index >= MAX_VEHICLES) return;

  // Calculate the EEPROM address for the given vehicle index.
  int eepromAddress = index * sizeof(EepromVehicle);

  EepromVehicle eepromVehicle;

  // Copy data from the Vehicle structure to the EepromVehicle structure.
  // The strncpy function ensures that the copied strings do not exceed the allocated space.
  strncpy(eepromVehicle.regNumber, vehicle.regNumber.c_str(), sizeof(eepromVehicle.regNumber));
  strncpy(eepromVehicle.location, vehicle.location.c_str(), sizeof(eepromVehicle.location));

  // Assign simple types directly.
  eepromVehicle.type = vehicle.type;

  // Continue copying string data.
  strncpy(eepromVehicle.paymentStatus, vehicle.paymentStatus.c_str(), sizeof(eepromVehicle.paymentStatus));
  strncpy(eepromVehicle.entryTimestamp, vehicle.entryTimestamp.c_str(), sizeof(eepromVehicle.entryTimestamp));
  strncpy(eepromVehicle.exitTimestamp, vehicle.exitTimestamp.c_str(), sizeof(eepromVehicle.exitTimestamp));

  // Write the structured vehicle data to the EEPROM at the calculated address.
  EEPROM.put(eepromAddress, eepromVehicle);
}

// Function to load a vehicle's data from EEPROM.
void loadVehicleFromEEPROM(int index, Vehicle& vehicle) {
  // Check if the index is within the valid range for stored vehicles.
  if (index < 0 || index >= MAX_VEHICLES) return;

  // Calculate the address in EEPROM where the vehicle's data is stored.
  int eepromAddress = index * sizeof(EepromVehicle);

  EepromVehicle eepromVehicle;

  // Read the vehicle data from EEPROM into an EepromVehicle structure.
  EEPROM.get(eepromAddress, eepromVehicle);

  // Convert and assign the data from EepromVehicle structure to the Vehicle structure.
  // This includes converting character arrays to Arduino String objects for easier handling in the program.
  vehicle.regNumber = String(eepromVehicle.regNumber);
  vehicle.location = String(eepromVehicle.location);
  vehicle.type = eepromVehicle.type;  // Direct assignment for non-array fields.
  vehicle.paymentStatus = String(eepromVehicle.paymentStatus);
  vehicle.entryTimestamp = String(eepromVehicle.entryTimestamp);
  vehicle.exitTimestamp = String(eepromVehicle.exitTimestamp);
}

void clearEEPROM() {
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0);
  }
}


void handleError(const __FlashStringHelper* errorMessage) {
  Serial.print(F("ERROR: "));
  Serial.println(errorMessage);
}

void debugMessage(const __FlashStringHelper* message) {
  Serial.print(F("DEBUG: "));
  Serial.println(message);
  sendDoneMessage();
}

void sendDoneMessage() {
  Serial.println(F("DONE!"));
}

// Function to display a single vehicle's data on the LCD screen.
void displayVehicleData(const Vehicle& vehicle, bool isFiltered, int index) {
  // Define custom characters for the up and down arrows on the LCD.
  byte upArrow[8] = {
    0b00100,
    0b01110,
    0b10101,
    0b00100,
    0b00100,
    0b00100,
    0b00000,
    0b00000
  };
  byte downArrow[8] = {
    0b00000,
    0b00000,
    0b00100,
    0b00100,
    0b10101,
    0b01110,
    0b00100,
    0b00000
  };

  // Create and assign the custom characters to the LCD.
  lcd.createChar(0, upArrow);
  lcd.createChar(1, downArrow);

  lcd.clear();          // Clear the LCD screen before displaying new data.
  lcd.setCursor(0, 0);  // Set the cursor to the beginning of the first line.

  // Calculate the number of vehicles to be displayed based on the filter applied.
  int listCount = isFiltered ? getFilteredVehicleCount() : vehicleCount;

  // Display the up arrow if there are more vehicles above the current one.
  if (index > 0) {
    lcd.write(byte(0));  // Up arrow
  } else {
    lcd.print(" ");  // Print a space if the up arrow is not needed.
  }

  // Print the vehicle registration number and the first part of the location.
  lcd.print(vehicle.regNumber);
  lcd.print(" ");                               // Space separator.
  lcd.print(vehicle.location.substring(0, 7));  // Display only the first 7 characters of the location.

  lcd.setCursor(0, 1);  // Move the cursor to the beginning of the second line.

  // Display the down arrow if there are more vehicles below the current one.
  if (index < listCount - 1) {
    lcd.write(byte(1));  // Down arrow
  } else {
    lcd.print(" ");  // Print a space if the down arrow is not needed.
  }

  // Determine if the location string needs scrolling.
  shouldScroll = vehicle.location.length() > 7;

  // Print the remaining vehicle data: type, payment status, entry, and exit timestamps.
  lcd.print(vehicle.type);
  lcd.print(" ");  // Space separator.
  lcd.print(vehicle.paymentStatus);
  lcd.print(" ");  // Space separator.
  lcd.print(vehicle.entryTimestamp);
  lcd.print(" ");  // Space separator.
  lcd.print(vehicle.exitTimestamp);

  // Set the backlight color based on the vehicle's payment status.
  if (vehicle.paymentStatus == "NPD") {
    lcd.setBacklight(YELLOW);
  } else if (vehicle.paymentStatus == "PD ") {
    lcd.setBacklight(GREEN);
  }
}

int getFilteredVehicleCount() {
  int count = 0;
  for (int i = 0; i < vehicleCount; ++i) {
    if ((currentFilter == PD && vehicles[i].paymentStatus == "PD ") || (currentFilter == NPD && vehicles[i].paymentStatus == "NPD")) {
      count++;
    }
  }
  return count;
}

void incrementFilteredIndex() {
  if (currentFilter == PD) {
    int pdCount = countFilteredVehicles("PD ");
    if (pdVehicleIndex < pdCount - 1) {
      pdVehicleIndex++;
    }
  } else if (currentFilter == NPD) {
    int npdCount = countFilteredVehicles("NPD");
    if (npdVehicleIndex < npdCount - 1) {
      npdVehicleIndex++;
    }
  }
}

void decrementFilteredIndex() {
  if (currentFilter == PD) {
    if (pdVehicleIndex > 0) {
      pdVehicleIndex--;
    }
  } else if (currentFilter == NPD) {
    if (npdVehicleIndex > 0) {
      npdVehicleIndex--;
    }
  }
}

int countFilteredVehicles(String status) {
  int count = 0;
  for (int i = 0; i < vehicleCount; i++) {
    if (vehicles[i].paymentStatus == status) {
      count++;
    }
  }
  return count;
}

// Function to validate a vehicle registration number.
bool isValidRegNumber(const String& regNum) {
  // Check if the registration number is exactly 7 characters long.
  if (regNum.length() != 7) {
    return false;  // Return false if it's not 7 characters.
  }

  // Loop through each character of the registration number.
  for (int i = 0; i < regNum.length(); i++) {
    // Check for letters at positions 0, 1, 4, 5, 6.
    if (i < 2 || (i > 3 && i < 7)) {
      // Check if the character is a letter and is uppercase.
      if (!isAlpha(regNum[i]) || !isUpperCase(regNum[i])) {
        return false;  // Return false if it's not an uppercase letter.
      }
    }
    // Check for digits at positions 2, 3.
    else if (i == 2 || i == 3) {
      // Check if the character is a digit.
      if (!isDigit(regNum[i])) {
        return false;  // Return false if it's not a digit.
      }
    }
  }
  return true;  // Return true if all checks pass, indicating a valid registration number.
}

void updatePaymentTimestamp(int index) {
  if (index < 0 || index >= vehicleCount) {
    return;  // Index out of bounds check
  }
  unsigned long currentTime = now();  // Loaded from EEPROM
  int currentHour = hour(currentTime);
  int currentMinute = minute(currentTime);
  String paymentTime = (currentHour < 10 ? "0" : "") + String(currentHour) + (currentMinute < 10 ? "0" : "") + String(currentMinute);

  vehicles[index].exitTimestamp = paymentTime;
}

void updateEntryTimestamp(int index) {
  if (index < 0 || index >= vehicleCount) {
    return;  // Index out of bounds check
  }
  unsigned long currentTime = now();  // Loaded from EEPROM
  int currentHour = hour(currentTime);
  int currentMinute = minute(currentTime);
  String entryTime = (currentHour < 10 ? "0" : "") + String(currentHour) + (currentMinute < 10 ? "0" : "") + String(currentMinute);

  vehicles[index].entryTimestamp = entryTime;
}

// Function to add a new vehicle or update an existing one in the parking system.
bool addVehicle(const String& regNumber, const String& location, char type) {
  // Check if the parking system is full.
  if (vehicleCount >= MAX_VEHICLES) {
    return false;  // Return false if no space is available to add more vehicles.
  }

  // Obtain the current time from the TimeLib library.
  int currentHour = hour();
  int currentMinute = minute();
  // Format the time into HHMM format.
  String entryTime = (currentHour < 10 ? "0" : "") + String(currentHour) + (currentMinute < 10 ? "0" : "") + String(currentMinute);

  // Loop through the existing vehicles to check if the vehicle already exists.
  for (int i = 0; i < vehicleCount; i++) {
    // If the vehicle is found, update its details.
    if (vehicles[i].regNumber == regNumber) {
      vehicles[i].location = location;  // Update location.
      vehicles[i].type = type;          // Update vehicle type.

      // Update the entry timestamp only if it's not already set.
      if (vehicles[i].entryTimestamp == "") {
        vehicles[i].entryTimestamp = entryTime;
      }
      return true;  // Return true to indicate successful update.
    }
  }

  // Add a new vehicle with the provided details if it does not exist already.
  vehicles[vehicleCount] = { regNumber, location, type, "NPD", entryTime, "" };
  // Save the new vehicle to EEPROM for persistent storage.
  saveVehicleToEEPROM(vehicleCount, vehicles[vehicleCount]);
  vehicleCount++;  // Increment the vehicle count.

  return true;  // Return true to indicate successful addition of a new vehicle.
}


// Function to process 'A' type messages for adding or updating vehicles.
void processAmessage(const String& message) {
  // Extract message components
  int firstDash = message.indexOf('-');
  int secondDash = message.indexOf('-', firstDash + 1);
  int thirdDash = message.indexOf('-', secondDash + 1);

  if (firstDash == -1 || secondDash == -1 || thirdDash == -1 || thirdDash >= message.length() - 1) {
    handleError(F("Invalid format for A message"));
    return;
  }

  String regNum = message.substring(firstDash + 1, secondDash);
  String typeString = message.substring(secondDash + 1, thirdDash);  // Extract type as a string
  String location = message.substring(thirdDash + 1);

  // Check for missing or invalid vehicle type
  if (typeString.length() != 1 || (typeString[0] != 'C' && typeString[0] != 'M' && typeString[0] != 'V' && typeString[0] != 'L' && typeString[0] != 'B')) {
    handleError(F("Invalid or missing vehicle type"));
    return;
  }
  char type = typeString[0];  // Convert the type string to a char

  if (!isValidRegNumber(regNum) || location.length() < 1 || location.length() > 11) {
    handleError(F("Invalid vehicle data"));
    return;
  }

  // Check if the vehicle already exists with the same details
  for (int i = 0; i < vehicleCount; i++) {
    if (vehicles[i].regNumber == regNum) {
      if (vehicles[i].type == type && vehicles[i].location == location) {
        // Exact duplicate found
        handleError(F("Duplicate vehicle entry"));
        return;
      }
      // If vehicle exists but details differ, and payment status is not paid, return error
      if (vehicles[i].paymentStatus == "NPD") {
        handleError(F("Cannot modify due to non-payment status"));
        return;
      }
      // Update vehicle details
      vehicles[i].type = type;
      vehicles[i].location = location;
      // No need to update entry timestamp as vehicle already exists
      saveVehicleToEEPROM(i, vehicles[i]);
      displayVehicle();
      debugMessage(F("Vehicle updated successfully"));
      return;
    }
  }

  // If vehicle does not exist, add a new vehicle
  if (addVehicle(regNum, location, type)) {
    displayVehicle();
    debugMessage(F("Vehicle added successfully"));
  } else {
    handleError(F("Vehicle array is full or error occurred"));
  }
}

// Function to process 'S' type messages for updating vehicle payment status.
void processSmessage(const String& message) {
  // Identify the positions of the dashes to split the message.
  int firstDash = message.indexOf('-');
  int secondDash = message.lastIndexOf('-');

  // Validate the message format.
  if (firstDash == -1 || secondDash == -1 || secondDash >= message.length() - 1) {
    handleError(F("Invalid format for S message"));
    return;
  }

  // Extract registration number and payment status from the message.
  String regNum = message.substring(firstDash + 1, secondDash);
  String paymentStatus = message.substring(secondDash + 1);

  // Validate the extracted data.
  if (!isValidRegNumber(regNum) || (paymentStatus != "PD" && paymentStatus != "NPD")) {
    handleError(F("Invalid data in S message"));
    return;
  }

  // Search for the vehicle and update its payment status.
  bool vehicleFound = false;
  for (int i = 0; i < vehicleCount; i++) {
    if (vehicles[i].regNumber == regNum) {
      vehicleFound = true;
      // Check if the payment status is different from the current status.
      if (vehicles[i].paymentStatus != paymentStatus) {
        vehicles[i].paymentStatus = paymentStatus + " ";  // Update payment status.

        // Update timestamps based on the new payment status.
        if (paymentStatus == "PD") {
          updatePaymentTimestamp(i);
        } else {
          vehicles[i].exitTimestamp = "";
          updateEntryTimestamp(i);
        }

        // Save changes to EEPROM.
        saveVehicleToEEPROM(i, vehicles[i]);

        // Reset filter indices and refresh display.
        pdVehicleIndex = npdVehicleIndex = 0;
        displayVehicle();

        debugMessage(F("Payment status updated successfully"));
        return;
      } else {
        handleError(F("Payment status not changed"));
        return;
      }
    }
  }

  // Handle the case where the vehicle was not found.
  if (!vehicleFound) {
    handleError(F("Vehicle not found"));
  }
}

// Function to process 'T' type messages for updating vehicle type.
void processTmessage(const String& message) {
  // Identify the positions of the dashes to split the message.
  int firstDash = message.indexOf('-');
  int secondDash = message.lastIndexOf('-');

  // Validate the message format.
  if (firstDash == -1 || secondDash == -1 || secondDash >= message.length() - 1) {
    handleError(F("Invalid format for T message"));
    return;
  }

  // Extract registration number and new type from the message.
  String regNum = message.substring(firstDash + 1, secondDash);
  char newType = message.charAt(secondDash + 1);

  // Validate the extracted data.
  if (!isValidRegNumber(regNum) || (newType != 'C' && newType != 'M' && newType != 'V' && newType != 'L' && newType != 'B')) {
    handleError(F("Invalid data in T message"));
    return;
  }

  // Search for the vehicle and update its type.
  for (int i = 0; i < vehicleCount; i++) {
    if (vehicles[i].regNumber == regNum) {
      // Restrict type modification for unpaid vehicles.
      if (vehicles[i].paymentStatus == "NPD") {
        handleError(F("Cannot modify type due to non-payment status"));
        return;
      }

      // Check if the new type is different from the current type.
      if (vehicles[i].type == newType) {
        handleError(F("Type is the same as existing"));
        return;
      }

      // Update the vehicle type and save changes to EEPROM.
      vehicles[i].type = newType;
      saveVehicleToEEPROM(i, vehicles[i]);
      displayVehicle();  // Refresh the display.

      debugMessage(F("Vehicle type updated successfully"));
      return;
    }
  }

  // Handle the case where the vehicle was not found.
  handleError(F("Vehicle not found"));
}

// Function to process 'L' type messages for updating vehicle location.
void processLmessage(const String& message) {
  // Find the positions of dashes to split the message.
  int firstDash = message.indexOf('-');
  int secondDash = message.lastIndexOf('-');

  // Validate message format.
  if (firstDash == -1 || secondDash == -1 || secondDash >= message.length() - 1) {
    handleError(F("Invalid format for L message"));
    return;
  }

  // Extract registration number and new location from the message.
  String regNum = message.substring(firstDash + 1, secondDash);
  String newLocation = message.substring(secondDash + 1);

  // Validate extracted data.
  if (!isValidRegNumber(regNum) || newLocation.length() < 1 || newLocation.length() > 11) {
    handleError(F("Invalid data in L message"));
    return;
  }

  // Search for the vehicle and update its location.
  for (int i = 0; i < vehicleCount; i++) {
    if (vehicles[i].regNumber == regNum) {
      // Restrict location modification for unpaid vehicles.
      if (vehicles[i].paymentStatus == "NPD") {
        handleError(F("Cannot modify location due to non-payment status"));
        return;
      }

      // Check if the new location is different from the current one.
      if (vehicles[i].location == newLocation) {
        handleError(F("Location is the same as existing"));
        return;
      }

      // Update the vehicle location and save changes to EEPROM.
      vehicles[i].location = newLocation;
      saveVehicleToEEPROM(i, vehicles[i]);
      displayVehicle();  // Refresh the display.

      debugMessage(F("Vehicle location updated successfully"));
      return;
    }
  }

  // Handle the case where the vehicle was not found.
  handleError(F("Vehicle not found"));
}

// Function to process 'R' type messages for removing a vehicle.
void processRmessage(const String& message) {
  // Find the position of dash to split the message.
  int dashIndex = message.indexOf('-');

  // Validate message format.
  if (dashIndex == -1 || dashIndex >= message.length() - 1) {
    handleError(F("Invalid format for R message"));
    return;
  }

  // Extract registration number from the message.
  String regNum = message.substring(dashIndex + 1);

  // Validate extracted registration number.
  if (!isValidRegNumber(regNum)) {
    handleError(F("Invalid registration number in R message"));
    return;
  }

  // Initialize flag to track if vehicle is found.
  bool vehicleFound = false;

  // Iterate through vehicles to find a match.
  for (int i = 0; i < vehicleCount; i++) {
    if (vehicles[i].regNumber == regNum) {
      vehicleFound = true;

      // Check if vehicle can be removed (paid status).
      if (vehicles[i].paymentStatus != "PD ") {
        handleError(F("Vehicle cannot be deleted due to non-payment status"));
        return;
      }

      // Remove the vehicle by shifting others to fill the gap.
      for (int j = i; j < vehicleCount - 1; j++) {
        vehicles[j] = vehicles[j + 1];
        saveVehicleToEEPROM(j, vehicles[j]);  // Update EEPROM after shift.
      }

      // Decrement the vehicle count and clear the last EEPROM entry.
      vehicleCount--;
      EEPROM.put(vehicleCount * sizeof(EepromVehicle), EepromVehicle());

      // Reset indices to account for the removal.
      pdVehicleIndex = 0;
      npdVehicleIndex = 0;
      displayVehicle();  // Update display after removal.

      debugMessage(F("Vehicle removed successfully"));
      return;
    }
  }

  // Handle case when the vehicle is not found.
  if (!vehicleFound) {
    handleError(F("Vehicle not found"));
  }
}


// Function to handle the scrolling of vehicle location text on the LCD.
void scrollLocation() {
  // Exit if scrolling is not required.
  if (!shouldScroll) return;

  // Check if it's time to update the scroll position.
  unsigned long currentTime = millis();
  if (currentTime - lastScrollTime >= scrollDelay) {
    // Update the last scroll time and increment scroll position.
    lastScrollTime = currentTime;
    scrollPosition++;

    // Reset scroll position if it exceeds the displayable part of the location string.
    if (scrollPosition > currentLocation.length() - 7) {
      scrollPosition = 0;  // Reset to the beginning of the string.
    }

    // Extract and display the current part of the location string on the LCD.
    String displayText = currentLocation.substring(scrollPosition, scrollPosition + 7);
    lcd.setCursor(9, 0);  // Set cursor to start at the location field on the first row.
    lcd.print(displayText);
  }
}