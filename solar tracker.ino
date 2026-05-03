#include <Wire.h>
#include <Adafruit_INA219.h>
#include <Servo.h>
#include <LiquidCrystal_I2C.h>

// ===== STRUCTURES =====
struct PowerMeasurement {
  float voltage;
  float current;
  float power;
};

struct EnergyData {
  float totalEnergy;    // Joules
  float avgPower;       // W
  unsigned long duration; // minutes
  unsigned long startTime;
};

// ===== CONSTANTS =====
const int PIN_SERVO_H = 8;
const int PIN_SERVO_V = 9;

const int SERVO_MIN = 0;
const int SERVO_MAX = 160;
const int RAMP_STEP_DEG = 2;
const int RAMP_DELAY_MS = 10;
const int STEP_DEG = 20; // Step for tracking scan to make it faster

// ===== OBJECTS =====
Adafruit_INA219 ina219(0x40);
Servo servoH, servoV;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== GLOBAL VARIABLES =====
int hPos = 0;  // Current horizontal position
int vPos = 0;  // Current vertical position
int bestH = 0;
int bestV = 0;

// Energy test variables
EnergyData energyTest;
bool energyTestRunning = false;
int energyTestDuration = 1; // Default duration in minutes

// Serial input
String serialInput = "";
bool commandReceived = false;

// ===== UTILITY FUNCTIONS =====
int clamp(int value, int minVal, int maxVal) {
  if (value < minVal) return minVal;
  if (value > maxVal) return maxVal;
  return value;
}

// Servo ramp function 
void rampTo(Servo &s, int &cur, int target) {
  target = clamp(target, SERVO_MIN, SERVO_MAX);
  int dir = (target > cur) ? +1 : -1;
  while(cur != target) {
    cur += dir * RAMP_STEP_DEG;
    if((dir > 0 && cur > target) || (dir < 0 && cur < target)) cur = target;
    s.write(cur);
    delay(RAMP_DELAY_MS);
  }
}

void setH(int deg) { rampTo(servoH, hPos, deg); }
void setV(int deg) { rampTo(servoV, vPos, deg); }

// ===== INA219 MEASUREMENTS =====
PowerMeasurement readPowerAvg(uint16_t n = 10, uint16_t dt = 30) {
  float su = 0, si = 0;
  for(uint16_t k = 0; k < n; k++) {
    // Using the same method as in the example code
    float shuntVoltage_mV = ina219.getShuntVoltage_mV();
    float busVoltage_V = ina219.getBusVoltage_V();
    float current_mA = ina219.getCurrent_mA();
    
    // Load voltage calculations
    float loadVoltage_V = busVoltage_V + (shuntVoltage_mV / 1000.0);
    float current_A = current_mA / 1000.0;
    
    su += loadVoltage_V;
    si += current_A;
    delay(dt);
  }
  
  PowerMeasurement pm;
  pm.voltage = su / n;
  pm.current = si / n;
  pm.power = pm.voltage * pm.current;
  
  return pm;
}

// ===== OPTIMIZED LCD DISPLAY =====
void updateLCD(const PowerMeasurement& pm) {
  lcd.clear();
  
  // Line 1: Voltage and Current
  lcd.setCursor(0, 0);
  lcd.print("U:");
  lcd.print(pm.voltage, 1);  // 1 decimal to save space
  lcd.print("V I:");
  lcd.print(abs(pm.current * 1000), 1); // Absolute value in mA, 0 decimals
  lcd.print("mA");
  
  // Line 2: Power and Angles
  lcd.setCursor(0, 1);
  lcd.print("P:");
  lcd.print(pm.power, 2);    // 2 decimals
  lcd.print("W ");
  lcd.print(hPos);
  lcd.print(",");
  lcd.print(vPos);
}

void displayEnergyOnLCD() {
  unsigned long elapsedSec = (millis() - energyTest.startTime) / 1000;
  unsigned long totalSec = energyTestDuration * 60;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ENERGY TEST");
  lcd.setCursor(0, 1);
  lcd.print(elapsedSec);
  lcd.print("/");
  lcd.print(totalSec);
  lcd.print("s ");
  lcd.print(energyTest.totalEnergy, 2);  // 2 decimals
  lcd.print("J");
}

// ===== MENU DISPLAY =====
void displayMainMenu() {
  Serial.println();
  Serial.println("===== SOLAR TRACKER TEST CENTER =====");
  Serial.println("1 - AUTO TRACKING TEST");
  Serial.println("2 - MANUAL SERVO CONTROL");
  Serial.println("3 - ENERGY ACCUMULATION TEST");
  Serial.println("4 - VIEW LAST RESULTS");
  Serial.println("5 - LIVE MONITORING");
  Serial.println();
  Serial.print("Choose test (1-5): ");
}

// ===== MODE 1: AUTO TRACKING (Using your algorithm) =====
void scanForMax() {
  Serial.println();
  Serial.println("===== AUTO TRACKING TEST =====");
  Serial.println("Starting grid scan for optimal position...");
  
  float maxP = -1;
  int bestA = 0;
  int bestE = 0;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AUTO TRACKING...");
  lcd.setCursor(0, 1);
  lcd.print("Scanning...");

  // Always start from H=0, V=0 (as in your code)
  setH(0);
  setV(0);
  delay(500);

  Serial.println("Scanning positions...");
  
  for(int a = 0; a <= 160; a += STEP_DEG) {
    setH(a);
    delay(200);
    
    // Update LCD with progress
    lcd.setCursor(0, 1);
    lcd.print("H:");
    lcd.print(a);
    lcd.print("       ");
    
    for(int e = 0; e <= 160; e += STEP_DEG) {
      setV(e);
      delay(200);
      
      PowerMeasurement pm = readPowerAvg(5, 30);  // Faster measurements during scan
      
      Serial.print("H:");
      Serial.print(a);
      Serial.print("° V:");
      Serial.print(e);
      Serial.print("° -> P:");
      Serial.print(pm.power, 3);
      Serial.println("W");
      
      if(pm.power > maxP) {
        maxP = pm.power;
        bestA = a;
        bestE = e;
      }
    }
  }

  // Final position = best found
  setH(bestA);
  setV(bestE);
  bestH = bestA;
  bestV = bestE;

  // Display result
  PowerMeasurement finalMeasurement = readPowerAvg();
  updateLCD(finalMeasurement);

  Serial.println();
  Serial.println("===== OPTIMAL POSITION FOUND =====");
  Serial.print("Best Position: H=");
  Serial.print(bestH);
  Serial.print("° V=");
  Serial.print(bestV);
  Serial.print("° P=");
  Serial.print(finalMeasurement.power, 3);
  Serial.println("W");
  Serial.println();
  Serial.println("Displaying live measurements... (type 'stop' to return to menu)");

  // Continue monitoring
  while (true) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      input.toLowerCase();
      if (input == "stop") {
        Serial.println("Returning to main menu...");
        break;
      }
    }
    
    PowerMeasurement pm = readPowerAvg();
    updateLCD(pm);
    
    Serial.print("Live: U:");
    Serial.print(pm.voltage, 2);
    Serial.print("V I:");
    Serial.print(abs(pm.current * 1000), 1);
    Serial.print("mA P:");
    Serial.print(pm.power, 3);
    Serial.println("W");
    
    delay(2000);
  }
}

// ===== MODE 2: MANUAL SERVO CONTROL =====
void manualServoTest() {
  Serial.println();
  Serial.println("===== MANUAL SERVO CONTROL =====");
  Serial.println("Commands:");
  Serial.println("  H:angle  - Set horizontal angle (0-160)");
  Serial.println("  V:angle  - Set vertical angle (0-160)");
  Serial.println("  H:90 V:45 - Set both angles");
  Serial.println("  stop     - Return to menu");
  Serial.println();
  
  while (true) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      input.toUpperCase();
      
      if (input == "STOP") {
        Serial.println("Returning to main menu...");
        break;
      }
      
      // Parse H: commands
      if (input.startsWith("H:")) {
        int colonIndex = input.indexOf(':');
        int spaceIndex = input.indexOf(' ');
        if (spaceIndex == -1) spaceIndex = input.length();
        
        int angle = input.substring(colonIndex + 1, spaceIndex).toInt();
        angle = clamp(angle, SERVO_MIN, SERVO_MAX);
        setH(angle);
        
        Serial.print("Horizontal set to: ");
        Serial.print(hPos);
        Serial.println("°");
      }
      
      // Parse V: commands
      if (input.startsWith("V:")) {
        int colonIndex = input.indexOf(':');
        int spaceIndex = input.indexOf(' ');
        if (spaceIndex == -1) spaceIndex = input.length();
        
        int angle = input.substring(colonIndex + 1, spaceIndex).toInt();
        angle = clamp(angle, SERVO_MIN, SERVO_MAX);
        setV(angle);
        
        Serial.print("Vertical set to: ");
        Serial.print(vPos);
        Serial.println("°");
      }
    }
    
    // Update display
    PowerMeasurement pm = readPowerAvg(3, 30);
    updateLCD(pm);
    
    Serial.print("Current: H:");
    Serial.print(hPos);
    Serial.print("° V:");
    Serial.print(vPos);
    Serial.print("° P:");
    Serial.print(pm.power, 3);
    Serial.println("W");
    
    delay(1000);
  }
}

// ===== MODE 3: ENERGY TEST WITH AUTO-TRACKING =====
void energyAccumulationTest() {
  Serial.println();
  Serial.println("===== ENERGY ACCUMULATION TEST =====");
  Serial.print("Current duration setting: ");
  Serial.print(energyTestDuration);
  Serial.println(" minutes");
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  duration:X  - Set test duration (1-10 minutes)");
  Serial.println("  start       - Start energy test (with auto-tracking first)");
  Serial.println("  stop        - Stop test and return to menu");
  Serial.println();
  
  while (true) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      input.toLowerCase();
      
      if (input == "stop") {
        if (energyTestRunning) {
          energyTestRunning = false;
          Serial.println("Energy test stopped!");
        }
        Serial.println("Returning to main menu...");
        break;
      }
      
      if (input.startsWith("duration:")) {
        int duration = input.substring(9).toInt();
        if (duration >= 1 && duration <= 10) {
          energyTestDuration = duration;
          Serial.print("Duration set to: ");
          Serial.print(energyTestDuration);
          Serial.println(" minutes");
        } else {
          Serial.println("Invalid duration! Use 1-10 minutes.");
        }
      }
      
      if (input == "start" && !energyTestRunning) {
        // STEP 1: Auto-tracking to find optimal position
        Serial.println("===== STEP 1: FINDING OPTIMAL POSITION =====");
        Serial.println("Running auto-tracking before energy test...");
        
        float maxP = -1;
        int bestA = 0;
        int bestE = 0;
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("FINDING OPTIMAL");
        lcd.setCursor(0, 1);
        lcd.print("POSITION...");

        // Start from H=0, V=0
        setH(0);
        setV(0);
        delay(500);

        Serial.println("Scanning for optimal position...");
        
        for(int a = 0; a <= 160; a += STEP_DEG) {
          setH(a);
          delay(200);
          
          for(int e = 0; e <= 160; e += STEP_DEG) {
            setV(e);
            delay(200);
            
            PowerMeasurement pm = readPowerAvg(3, 30);  // Fast scan
            
            Serial.print("H:");
            Serial.print(a);
            Serial.print("° V:");
            Serial.print(e);
            Serial.print("° -> P:");
            Serial.print(pm.power, 3);
            Serial.println("W");
            
            if(pm.power > maxP) {
              maxP = abs(pm.power);
              bestA = a;
              bestE = e;
            }
          }
        }

        // Final position = best found
        setH(bestA);
        setV(bestE);
        bestH = bestA;
        bestV = bestE;

        Serial.println();
        Serial.print("Optimal position found: H=");
        Serial.print(bestH);
        Serial.print("° V=");
        Serial.print(bestV);
        Serial.print("° P=");
        Serial.print(maxP, 3);
        Serial.println("W");
        
        delay(2000);
        
        // STEP 2: Start energy test
        energyTestRunning = true;
        energyTest.startTime = millis();
        energyTest.totalEnergy = 0;
        energyTest.avgPower = 0;
        energyTest.duration = energyTestDuration;
        
        Serial.println();
        Serial.println("===== STEP 2: ENERGY TEST STARTED =====");
        Serial.print("Duration: ");
        Serial.print(energyTestDuration);
        Serial.println(" minutes");
        Serial.print("Position fixed at: H=");
        Serial.print(bestH);
        Serial.print("° V=");
        Serial.print(bestV);
        Serial.println("°");
        Serial.println("Energy will be displayed in Joules (J)");
        Serial.println("Collecting data every 5 seconds...");
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("ENERGY TEST");
        lcd.setCursor(0, 1);
        lcd.print("STARTED!");
        delay(1000);
      }
    }
    
    if (energyTestRunning) {
      unsigned long elapsedMs = millis() - energyTest.startTime;
      unsigned long elapsedSec = elapsedMs / 1000;
      unsigned long totalSec = energyTestDuration * 60;
      
      if (elapsedSec >= totalSec) {
        // Test completed
        energyTestRunning = false;
        
        Serial.println();
        Serial.println("===== ENERGY TEST COMPLETED =====");
        Serial.print("Total Duration: ");
        Serial.print(energyTestDuration);
        Serial.println(" minutes");
        Serial.print("Position used: H=");
        Serial.print(bestH);
        Serial.print("° V=");
        Serial.print(bestV);
        Serial.println("°");
        Serial.print("Total Energy: ");
        Serial.print(energyTest.totalEnergy, 1);
        Serial.println(" Joules");
        Serial.print("Average Power: ");
        Serial.print(energyTest.avgPower, 3);
        Serial.println(" W");
        Serial.println("Type 'stop' to return to menu or 'start' for new test");
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("TEST COMPLETE!");
        lcd.setCursor(0, 1);
        lcd.print("E:");
        lcd.print(energyTest.totalEnergy, 2);  // 2 decimals
        lcd.print("J");
        
        continue;
      }
      
      // Take measurement
      PowerMeasurement pm = readPowerAvg(5, 30);
      
      // Calculate energy in Joules (Power × time in seconds)
      float timeIntervalSeconds = 5.0; // 5 seconds between measurements
      energyTest.totalEnergy += abs(pm.power) * timeIntervalSeconds; // Joules = Watts × seconds
      energyTest.avgPower = energyTest.totalEnergy / (elapsedMs / 1000.0); // Average power
      
      // Display progress
      displayEnergyOnLCD();
      
      Serial.print("Time: ");
      Serial.print(elapsedSec);
      Serial.print("/");
      Serial.print(totalSec);
      Serial.print("s | P: ");
      Serial.print(pm.power, 3);
      Serial.print("W | E: ");
      Serial.print(energyTest.totalEnergy, 1);
      Serial.print("J | Avg: ");
      Serial.print(energyTest.avgPower, 3);
      Serial.println("W");
      
      delay(5000); // Measure every 5 seconds
    } else {
      delay(500);
    }
  }
}

// ===== MODE 4: VIEW RESULTS =====
void viewResults() {
  Serial.println();
  Serial.println("===== LAST TEST RESULTS =====");
  Serial.print("Best Position - H:");
  Serial.print(bestH);
  Serial.print("° V:");
  Serial.print(bestV);
  Serial.println("°");
  
  if (energyTest.totalEnergy > 0) {
    Serial.print("Last Energy Test - Duration:");
    Serial.print(energyTest.duration);
    Serial.print("min Energy:");
    Serial.print(energyTest.totalEnergy, 1);
    Serial.print("J AvgPower:");
    Serial.print(energyTest.avgPower, 3);
    Serial.println("W");
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LAST RESULTS");
  lcd.setCursor(0, 1);
  lcd.print("H:");
  lcd.print(bestH);
  lcd.print(" V:");
  lcd.print(bestV);
  
  Serial.println("Press Enter to return to menu...");
  while (!Serial.available()) {
    delay(100);
  }
  Serial.readStringUntil('\n');
}

// ===== MODE 5: LIVE MONITORING =====
void liveMonitoring() {
  Serial.println();
  Serial.println("===== LIVE MONITORING =====");
  Serial.println("Displaying real-time measurements...");
  Serial.println("Type 'stop' to return to menu");
  Serial.println();
  
  while (true) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      input.toLowerCase();
      if (input == "stop") {
        Serial.println("Returning to main menu...");
        break;
      }
    }
    
    PowerMeasurement pm = readPowerAvg();
    updateLCD(pm);
    
    Serial.print("Live: ");
    Serial.print(pm.voltage, 2);
    Serial.print("V | ");
    Serial.print(abs(pm.current * 1000), 1);
    Serial.print("mA | ");
    Serial.print(pm.power, 3);
    Serial.print("W | H:");
    Serial.print(hPos);
    Serial.print("° V:");
    Serial.print(vPos);
    Serial.println("°");
    
    delay(2000);
  }
}

// ===== SERIAL COMMAND HANDLING =====
void processSerialCommand() {
  serialInput.trim();
  int command = serialInput.toInt();
  
  switch (command) {
    case 1:
      scanForMax(); 
      break;
    case 2:
      manualServoTest();
      break;
    case 3:
      energyAccumulationTest();
      break;
    case 4:
      viewResults();
      break;
    case 5:
      liveMonitoring();
      break;
    default:
      Serial.println("Invalid choice! Please select 1-5.");
      break;
  }
  
  displayMainMenu();
  serialInput = "";
  commandReceived = false;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // Initialize servos
  servoH.attach(PIN_SERVO_H);
  servoV.attach(PIN_SERVO_V);
  setH(0);
  setV(0);
  
  // Initialize INA219 with calibration
  if (!ina219.begin()) {
    Serial.println("ERROR: INA219 not found!");
  } else {
    Serial.println("INA219 OK");
    // Calibration as in the INA219Measurement.ino code //from 4mV and 0,0125mA
    ina219.setCalibration_16V_400mA();
  }
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Solar Test Ctr");
  lcd.setCursor(0, 1);
  lcd.print("Init...");
  
  delay(2000);
  
  Serial.println("System initialized successfully!");
  displayMainMenu();  // Display menu immediately on startup
}

// ===== MAIN LOOP =====
void loop() {
  // Handle serial input
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialInput.length() > 0) {
        commandReceived = true;
      }
    } else {
      serialInput += c;
    }
  }
  
  if (commandReceived) {
    processSerialCommand();
  }
  
  // Update LCD with current measurements when idle
  static unsigned long lastLCDUpdate = 0;
  if (!commandReceived && !energyTestRunning && (millis() - lastLCDUpdate > 2000)) {
    PowerMeasurement pm = readPowerAvg(3, 30);
    updateLCD(pm);  // This function uses the correct hPos and vPos variables
    lastLCDUpdate = millis();
  }
  
  delay(100);
}
