/*
 * RFID In/Out System with Shared I2C for LCD and RTC
 * Shows personalized messages and tracks time spent
 * Display shows only hours and minutes between scans
 * Updates time display when minutes change
 * Added buzzer for audio feedback
 * Added double entry detection and alarm
 * Added RGB LED feedback
 */
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>

// Pin definitions for RFID
#define RST_PIN_IN      8  // Reset pin for IN RFID reader
#define RST_PIN_OUT     7  // Reset pin for OUT RFID reader
#define SS_PIN_IN       10 // SDA pin for IN RFID reader
#define SS_PIN_OUT      9  // SDA pin for OUT RFID reader
#define BUZZER_PIN      4  // Pin for buzzer

// Pin definitions for RGB LED
#define RED_PIN    5  // Pin for RGB LED red component
#define GREEN_PIN  6  // Pin for RGB LED green component
#define BLUE_PIN   3  // Pin for RGB LED blue component

// RFID readers setup
MFRC522 rfidIn(SS_PIN_IN, RST_PIN_IN);
MFRC522 rfidOut(SS_PIN_OUT, RST_PIN_OUT);

// LCD setup - standard I2C address
LiquidCrystal_I2C lcd(0x27, 16, 2);

// RTC setup
RTC_DS3231 rtc;

// Debounce timing
#define DEBOUNCE_TIME 1500 
unsigned long lastScanTime = 0;
int lastMinute = -1; // Store the last minute displayed

// Known cards database
const int NUM_KNOWN_CARDS = 3;
String knownUIDs[NUM_KNOWN_CARDS] = {
  "5ADA9C80",  // Andrei
  "5AAC5180",  // Bogdan
  "93D0140E"   // Denis
};
String knownNames[NUM_KNOWN_CARDS] = {
  "Andrei",
  "Bogdan",
  "Denis"
};

// Entry time tracking (stored as Unix timestamp)
unsigned long entryTimes[NUM_KNOWN_CARDS] = {0, 0, 0};
bool isInside[NUM_KNOWN_CARDS] = {false, false, false};

// Flag to indicate if the system is in ready mode
bool inReadyMode = false;

// Error logging
#define MAX_ERROR_LOG 10
String errorLog[MAX_ERROR_LOG];
int errorCount = 0;

void setup() {
  Serial.begin(9600);
  Serial.println("Starting RFID Attendance System...");
  
  // Initialize SPI bus for RFID readers
  SPI.begin();
  
  // Initialize RFID readers
  rfidIn.PCD_Init();
  delay(100);
  rfidOut.PCD_Init();
  
  // Set antenna gain to maximum
  rfidIn.PCD_SetAntennaGain(MFRC522::RxGain_max);
  rfidOut.PCD_SetAntennaGain(MFRC522::RxGain_max);
  
  // Initialize buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Make sure buzzer is off
  
  // Initialize RGB LED pins
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  // Turn off RGB LED initially
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);
  
  // Initialize I2C for both LCD and RTC
  Wire.begin();  // Use default A4/A5
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting up...");
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    lcd.setCursor(0, 1);
    lcd.print("RTC Error!");
    // Error beep
    errorBeep();
    blinkRed();  // Add red blinking for error
    logError("RTC not found");
    delay(2000);
  } else {
    Serial.println("RTC connected");
    
    // Set time if RTC lost power
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, setting to compile time");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      // Warning beep
      warningBeep();
      blinkRed();  // Add red blinking for error
      logError("RTC lost power");
    }
    
    // Display time
    DateTime now = rtc.now();
    lcd.setCursor(0, 1);
    lcd.print(formatTimeHM(now));
    lastMinute = now.minute(); // Initialize last minute
  }
  
  // Startup beep
  startupBeep();
  
  // Reset all entry states
  for (int i = 0; i < NUM_KNOWN_CARDS; i++) {
    isInside[i] = false;
    entryTimes[i] = 0;
  }
  
  // Show ready screen
  delay(2000);
  showReadyScreen();
  
  Serial.println("System ready!");
}

void loop() {
  // Get current time
  DateTime now = rtc.now();
  unsigned long currentTime = millis();
  
  // Update the time display if minute has changed and we're in ready mode
  if (inReadyMode && now.minute() != lastMinute) {
    lcd.setCursor(0, 1);
    lcd.print(formatTimeHM(now));
    lastMinute = now.minute();
  }
  
  // Check if enough time has passed since last scan
  if (currentTime - lastScanTime >= DEBOUNCE_TIME) {
    
    // Check entrance RFID reader
    if (rfidIn.PICC_IsNewCardPresent() && rfidIn.PICC_ReadCardSerial()) {
      // We're no longer in ready mode
      inReadyMode = false;
      
      // Get card UID
      String uid = getUID(rfidIn.uid.uidByte, rfidIn.uid.size);
      
      // Check if it's a known card
      int personIndex = findPersonByUID(uid);
      
      // Log to serial
      Serial.print("ENTRANCE - Card: ");
      Serial.print(uid);
      
      if (personIndex >= 0) {
        Serial.print(" (" + knownNames[personIndex] + ")");
        
        // Check if person is already inside (double entry attempt)
        if (isInside[personIndex]) {
          // Double entry attempt detected!
          doubleEntryAlarm(personIndex);
        } else {
          // Normal entry
          successBeep();
          blinkGreen();  // Add green blinking for entry
          
          // Record entry
          entryTimes[personIndex] = now.unixtime();
          isInside[personIndex] = true;
          
          // Display on LCD
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Hello ");
          lcd.print(knownNames[personIndex]);
          lcd.setCursor(0, 1);
          lcd.print(formatTime(now));
        }
      } else {
        // Unknown person
        unknownBeep();
        blinkRed();  // Add red blinking for unknown person
        logError("Unknown card: " + uid);
        
        // Display on LCD
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("ENTER: ");
        lcd.print(uid.substring(0, 8));
        lcd.setCursor(0, 1);
        lcd.print(formatTime(now));
      }
      
      Serial.print(" at ");
      Serial.println(formatDateTime(now));
      
      // Halt RFID communication
      rfidIn.PICC_HaltA();
      rfidIn.PCD_StopCrypto1();
      
      // Update last scan time
      lastScanTime = currentTime;
    }
    
    // Small delay between reader checks
    delay(20);
    
    // Check exit RFID reader
    if (rfidOut.PICC_IsNewCardPresent() && rfidOut.PICC_ReadCardSerial()) {
      // We're no longer in ready mode
      inReadyMode = false;
      
      // Get card UID
      String uid = getUID(rfidOut.uid.uidByte, rfidOut.uid.size);
      
      // Check if it's a known card
      int personIndex = findPersonByUID(uid);
      
      // Log to serial
      Serial.print("EXIT - Card: ");
      Serial.print(uid);
      
      if (personIndex >= 0) {
        Serial.print(" (" + knownNames[personIndex] + ")");
        
        // Check if person is not inside (exiting without entry)
        if (!isInside[personIndex]) {
          // Person trying to exit without entering
          invalidExitAlarm(personIndex);
        } else {
          // Normal exit
          exitBeep();
          blinkGreen();  // Add green blinking for exit
          
          // Display on LCD
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Bye ");
          lcd.print(knownNames[personIndex]);
          
          // Calculate and display time spent
          unsigned long timeSpentSeconds = now.unixtime() - entryTimes[personIndex];
          lcd.setCursor(0, 1);
          lcd.print(formatTimeSpent(timeSpentSeconds));
          
          // Log to serial
          Serial.print("Time spent: ");
          Serial.println(formatTimeSpent(timeSpentSeconds));
          
          // Reset entry state
          isInside[personIndex] = false;
        }
      } else {
        // Unknown person
        unknownBeep();
        blinkRed();  // Add red blinking for unknown person
        logError("Unknown card at exit: " + uid);
        
        // Display on LCD
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("EXIT: ");
        lcd.print(uid.substring(0, 8));
        lcd.setCursor(0, 1);
        lcd.print(formatTime(now));
      }
      
      Serial.print(" at ");
      Serial.println(formatDateTime(now));
      
      // Halt RFID communication
      rfidOut.PICC_HaltA();
      rfidOut.PCD_StopCrypto1();
      
      // Update last scan time
      lastScanTime = currentTime;
    }
  }
  
  // Reset display after 3 seconds of scanning
  if (!inReadyMode && (currentTime - lastScanTime >= 3000)) {
    showReadyScreen();
  }
  
  // Small delay to prevent excessive CPU usage
  delay(10);
}

// Show the ready screen with current time
void showReadyScreen() {
  DateTime now = rtc.now();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready to Scan");
  
  // Show current time on second line (hours and minutes only)
  lcd.setCursor(0, 1);
  lcd.print(formatTimeHM(now));
  
  // Update the last minute
  lastMinute = now.minute();
  
  // Set flag that we're in ready mode
  inReadyMode = true;
}

// Find person by UID, returns index or -1 if not found
int findPersonByUID(String uid) {
  // We only need to compare the first 8 characters of the UID
  String shortUID = uid.substring(0, 8);
  
  for (int i = 0; i < NUM_KNOWN_CARDS; i++) {
    if (shortUID.equals(knownUIDs[i])) {
      return i;
    }
  }
  return -1;  // Not found
}

// Convert UID bytes to string
String getUID(byte *buffer, byte bufferSize) {
  String result = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) {
      result += "0";
    }
    result += String(buffer[i], HEX);
  }
  result.toUpperCase();
  return result;
}

// Format time as HH:MM (hours and minutes only)
String formatTimeHM(DateTime dt) {
  String result = "";
  
  // Hours
  if (dt.hour() < 10) result += "0";
  result += String(dt.hour()) + ":";
  
  // Minutes
  if (dt.minute() < 10) result += "0";
  result += String(dt.minute());
  
  return result;
}

// Format time as HH:MM:SS for LCD display
String formatTime(DateTime dt) {
  String result = "";
  
  // Hours
  if (dt.hour() < 10) result += "0";
  result += String(dt.hour()) + ":";
  
  // Minutes
  if (dt.minute() < 10) result += "0";
  result += String(dt.minute()) + ":";
  
  // Seconds
  if (dt.second() < 10) result += "0";
  result += String(dt.second());
  
  return result;
}

// Format time spent in hours, minutes, seconds
String formatTimeSpent(unsigned long seconds) {
  // Calculate hours, minutes, seconds
  unsigned long hours = seconds / 3600;
  unsigned long minutes = (seconds % 3600) / 60;
  unsigned long secs = seconds % 60;
  
  // Format as "Time: HH:MM:SS"
  String result = "Time: ";
  
  if (hours < 10) result += "0";
  result += String(hours) + ":";
  
  if (minutes < 10) result += "0";
  result += String(minutes) + ":";
  
  if (secs < 10) result += "0";
  result += String(secs);
  
  return result;
}

// Format full date and time for serial output
String formatDateTime(DateTime dt) {
  String result = "";
  
  // Date
  result += String(dt.year()) + "/";
  if (dt.month() < 10) result += "0";
  result += String(dt.month()) + "/";
  if (dt.day() < 10) result += "0";
  result += String(dt.day()) + " ";
  
  // Time
  if (dt.hour() < 10) result += "0";
  result += String(dt.hour()) + ":";
  if (dt.minute() < 10) result += "0";
  result += String(dt.minute()) + ":";
  if (dt.second() < 10) result += "0";
  result += String(dt.second());
  
  return result;
}

// Handle double entry attempt (someone tries to enter twice)
void doubleEntryAlarm(int personIndex) {
  // Log the error
  String errorMsg = "Double entry: " + knownNames[personIndex];
  logError(errorMsg);
  
  // Show error on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ERROR: Already");
  lcd.setCursor(0, 1);
  lcd.print("inside - " + knownNames[personIndex]);
  
  // Sound the alarm
  alarmBeep();
  blinkRed();  // Add red blinking for error
  
  Serial.println("ALARM: " + errorMsg);
}

// Handle invalid exit attempt (someone tries to exit without entering)
void invalidExitAlarm(int personIndex) {
  // Log the error
  String errorMsg = "Invalid exit: " + knownNames[personIndex];
  logError(errorMsg);
  
  // Show error on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ERROR: Not");
  lcd.setCursor(0, 1);
  lcd.print("inside - " + knownNames[personIndex]);
  
  // Sound the alarm
  alarmBeep();
  blinkRed();  // Add red blinking for error
  
  Serial.println("ALARM: " + errorMsg);
}

// Log an error message
void logError(String errorMessage) {
  // Add timestamp to error
  DateTime now = rtc.now();
  String timestamp = formatDateTime(now);
  
  // Store in error log (circular buffer)
  errorLog[errorCount % MAX_ERROR_LOG] = timestamp + " - " + errorMessage;
  errorCount++;
  
  // Print to serial
  Serial.print("ERROR: ");
  Serial.println(errorMessage);
}

// Print the error log
void printErrorLog() {
  Serial.println("=== ERROR LOG ===");
  int startIndex = (errorCount > MAX_ERROR_LOG) ? (errorCount % MAX_ERROR_LOG) : 0;
  int entries = min(errorCount, MAX_ERROR_LOG);
  
  for (int i = 0; i < entries; i++) {
    int index = (startIndex + i) % MAX_ERROR_LOG;
    Serial.println(errorLog[index]);
  }
  Serial.println("=================");
}

// Buzzer functions

// Beep for system startup
void startupBeep() {
  for (int i = 0; i < 2; i++) {
    tone(BUZZER_PIN, 1000, 100);
    delay(150);
  }
  tone(BUZZER_PIN, 1500, 200);
  delay(200);
}

// Beep for success (known person entering)
void successBeep() {
  tone(BUZZER_PIN, 1500, 100);
  delay(100);
  tone(BUZZER_PIN, 2000, 150);
  delay(150);
}

// Beep for exit (known person leaving)
void exitBeep() {
  tone(BUZZER_PIN, 2000, 100);
  delay(100);
  tone(BUZZER_PIN, 1500, 150);
  delay(150);
}

// Beep for unknown person
void unknownBeep() {
  tone(BUZZER_PIN, 800, 200);
  delay(200);
}

// Beep for error
void errorBeep() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 300, 200);
    delay(250);
  }
}

// Beep for warning
void warningBeep() {
  for (int i = 0; i < 2; i++) {
    tone(BUZZER_PIN, 800, 100);
    delay(150);
  }
}

// Alarm for fraud attempts or serious errors
void alarmBeep() {
  // Stronger and longer sound for alarm
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, 2500, 100);
    delay(120);
    tone(BUZZER_PIN, 2000, 100);
    delay(120);
    tone(BUZZER_PIN, 1500, 100);
    delay(120);
  }
}

// RGB LED functions
void setRGB(bool red, bool green, bool blue) {
  digitalWrite(RED_PIN, red ? HIGH : LOW);
  digitalWrite(GREEN_PIN, green ? HIGH : LOW);
  digitalWrite(BLUE_PIN, blue ? HIGH : LOW);
}

void turnOffRGB() {
  setRGB(false, false, false);
}

// Blink green LED 3 times for success
void blinkGreen() {
  for (int i = 0; i < 3; i++) {
    setRGB(false, true, false);  // Green on
    delay(200);
    turnOffRGB();                // LED off
    delay(200);
  }
}

// Blink red LED 5 times for error
void blinkRed() {
  for (int i = 0; i < 5; i++) {
    setRGB(true, false, false);  // Red on
    delay(200);
    turnOffRGB();                // LED off
    delay(200);
  }
}