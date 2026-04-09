#include <LiquidCrystal_I2C.h>       //LCD screens
#include <OneWire.h>  
#include <DallasTemperature.h>      
#include <Adafruit_Si7021.h>        
#include <Adafruit_DS3502.h>
#include <SPI.h>
#include <SD.h> 

File myFile;

const int powerPin = 30;
const int recipePin = 31;   
const int tempUpPin = 32;   
const int tempDownPin = 33;      
const int startPin = 34;
const int manualPin = 35;
const int heatersOnPin = 36; // Fixed naming consistency

const int greenRGBPin = 8;
const int blueRGBPin = 9;
const int redRGBPin = 10;
const int manualLEDPin = 12;          
const int heaterLEDPin = 11;

const int fanMOSFETPin = 22;
const int heaterTRIACPin = 23;

// Sensors [cite: 8]
const int humiditySensor = 44;
const int tempSensors = 45;          

// SD card Chip Select [cite: 9]
const int sdCSPin = 53;

int tempSetpoint = 35;
const int fishTemp = 50;
const int seaweedTemp = 60;
const int vegTemp = 70;
const int minimumTemp = 35;
const int maximumTemp = 80;
int recipeCounter = 0;

int wiperVal = 0;

struct Button {
  uint8_t pin;
  bool lastReading = LOW;
  bool state = LOW;
  unsigned long lastDebounceTime = 0;

  Button(int p) {
    pin = p;
    lastReading = LOW;
    state = LOW;
    lastDebounceTime = 0;
  }
 
  // Returns true ONLY on the exact moment the button is pressed down
  bool pressed() {
    bool isPressed = false;
    bool reading = digitalRead(pin);
    if (reading != lastReading) {
      lastDebounceTime = millis();
    }
    if ((millis() - lastDebounceTime) > 50) { // 50ms debounce [cite: 33]
      if (reading != state) {
        state = reading;
        if (state == HIGH) {
          isPressed = true;
        }
      }
    }
    lastReading = reading;
    return isPressed;
  }
};

Button btnPower(powerPin);
Button btnRecipe(recipePin);
Button btnTempUp(tempUpPin);
Button btnTempDown(tempDownPin);
Button btnStart(startPin);
Button btnManual(manualPin);
Button btnHeatersOn(heatersOnPin);

enum SystemState {
  STATE_OFF,
  STATE_IDLE,
  STATE_AUTO_RUNNING,
  STATE_MANUAL_RUNNING
};
SystemState currentState = STATE_OFF;

float hum = 0;
float tempCsensor1 = 0;
float tempCsensor2 = 0;                      
float tempAvg = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);
LiquidCrystal_I2C lcd2(0x23, 20, 4);
OneWire oneWire(tempSensors);
DallasTemperature sensors(&oneWire);
DeviceAddress sensor1, sensor2;
Adafruit_Si7021 humidity = Adafruit_Si7021();
Adafruit_DS3502 ds3502 = Adafruit_DS3502();

unsigned long blinkPreviousMillis = 0;
long startTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long controlPreviousMillis = 0;
unsigned long previousPrint = 0;
unsigned long lastSensorUpdate = 0;

bool manualModeSelected = false;
bool heaterState = false;
bool blinkState = 0;
bool sdActive = false;
bool heaterValue = false;

void setup(){
    Serial.begin(9600);

    sensors.begin();
    humidity.begin();
    ds3502.begin();

    pinMode(powerPin, INPUT);
    pinMode(recipePin, INPUT);
    pinMode(tempUpPin, INPUT);
    pinMode(tempDownPin, INPUT);
    pinMode(startPin, INPUT);
    pinMode(manualPin, INPUT);
    pinMode(heatersOnPin, INPUT);

    pinMode(manualLEDPin, OUTPUT);        
    pinMode(heaterLEDPin, OUTPUT);
    pinMode(greenRGBPin, OUTPUT);
    pinMode(blueRGBPin, OUTPUT);
    pinMode(redRGBPin, OUTPUT);

    lcd.init();
    lcd.backlight();                                
    lcd2.init();
    lcd2.backlight();

    if(!SD.begin(sdCSPin)){
        Serial.println("SD initialization failed!"); //T.B.D - DELETE
        const long errorTime = millis();
        errorLight();
    }

    myFile = SD.open("data.csv", FILE_WRITE);
    if(myFile){
        sdActive = true;
        myFile.println("Hours, minutes, seconds, avg temp, humidity, heater state, wiper value");
        myFile.close();
    }

    digitalWrite(fanMOSFETPin, LOW);
    digitalWrite(heaterTRIACPin, LOW);
    turnOffLEDs();
}

void loop(){
    if (btnPower.pressed()) {
        if (currentState == STATE_OFF) {
            currentState = STATE_IDLE; // Turn ON
        } else {
            currentState = STATE_OFF;  // Turn OFF from any state
        }
    }

    switch (currentState) {
   
        case STATE_OFF:
            digitalWrite(fanMOSFETPin, LOW);
            digitalWrite(heaterTRIACPin, LOW);
            turnOffLEDs();
            lcd.clear();
            lcd2.clear();
            break;

        case STATE_IDLE:
            handleLEDStatus(true); 
            handleRecipeSelection();
            displaySetpoint();

            // 1. Listen for the Manual button first
            if (btnManual.pressed()) {
                manualModeSelected = !manualModeSelected; // Toggle selection
            }
            if (manualModeSelected){
                digitalWrite(manualLEDPin, HIGH);
            }
            if (manualModeSelected == false){
                digitalWrite(manualLEDPin, LOW);
            }
     
            // 2. Only check for Start if Manual has been selected
            if (manualModeSelected && btnStart.pressed()) {
                // Since we already checked the button, we move straight to manual
                currentState = STATE_MANUAL_RUNNING;
            }

            if (btnStart.pressed() && manualModeSelected == false){
                currentState = STATE_AUTO_RUNNING;
                startTime = millis();
                lcd2.clear();
            }
            break;

        case STATE_AUTO_RUNNING:
            handleLEDStatus(false); // false = solid green
            controlHeatersAndFansAuto();
            displaySetpoint();
            updateStopwatch();
            readSensors();
            if (sdActive == true){
                sdCardWrite();
            }
            break;

        case STATE_MANUAL_RUNNING:
            //handleLEDStatus(false); // false = solid green
            //analogWrite(redRGBPin, 40);
            digitalWrite(manualLEDPin, HIGH);
            lcd.clear();
            lcd2.clear();
            controlHeatersAndFansManual();
            break;
    }
}

void handleRecipeSelection() {
    if (btnRecipe.pressed()) {
        recipeCounter++;
        if (recipeCounter > 3) recipeCounter = 1; // Cycle 1, 2, 3
   
        if (recipeCounter == 1) tempSetpoint = fishTemp; 
        else if (recipeCounter == 2) tempSetpoint = seaweedTemp; 
        else if (recipeCounter == 3) tempSetpoint = vegTemp; 
    }

    if (btnTempUp.pressed() && tempSetpoint < maximumTemp) {
        tempSetpoint += 2;
    }
    if (btnTempDown.pressed() && tempSetpoint > minimumTemp) {
        tempSetpoint -= 2;
    }
}

void displaySetpoint() {
    // Only update occasionally to prevent screen flicker
    if (millis() - lastDisplayUpdate > 250) {
        lcd.setCursor(0, 0);
        lcd.print("Set: ");
        lcd.print(tempSetpoint);
        lcd.print(" C  ");
        lastDisplayUpdate = millis();
    }
}

void updateStopwatch() {
    // Update the clock every 1 second [cite: 145]
    if (millis() - startTime >= 1000) {
        long elapsedTime = millis() - startTime; //[cite: 139]
        long hours = elapsedTime / 3600000; //[cite: 141]
        long minutes = (elapsedTime % 3600000) / 60000; //[cite: 143]
        long seconds = ((elapsedTime % 3600000) % 60000) / 1000; //[cite: 145]

        // Use sprintf to format the string cleanly (e.g. 01:05:09) [cite: 147]
        char timeStr[9];
        sprintf(timeStr, "%02ld:%02ld:%02ld", hours, minutes, seconds);
   
        lcd2.setCursor(0, 0);
        lcd2.print(timeStr);
    }
}

void handleLEDStatus(bool blink) {
    if (blink) {
        // Blinking logic
        if (millis() - blinkPreviousMillis >= 500) { 
            blinkPreviousMillis = millis();
            blinkState =! blinkState;
            // Toggle LED state using digitalRead of the output pin
            if(blinkState == true){
                analogWrite(greenRGBPin, 250); //change to green
            }else{
                analogWrite(greenRGBPin, 0); //change to green
            }
        }
    } else {
        // Solid green [cite: 134]
        analogWrite(greenRGBPin, 250); //change to green
    }
}

void turnOffLEDs() {
    analogWrite(greenRGBPin, 0);
    analogWrite(blueRGBPin, 0);
    analogWrite(redRGBPin, 0);
    digitalWrite(heaterLEDPin, LOW);
}

void controlHeatersAndFansManual() {
    if(btnHeatersOn.pressed()){
        heaterState =! heaterState;
    }
    if(heaterState == true){
        digitalWrite(heaterLEDPin, HIGH);
        heaterValue = true;
    }else{
        digitalWrite(heaterLEDPin, LOW);
        heaterValue = false;
    }
}

void controlHeatersAndFansAuto() {
    // Calculate deadbands locally
    float fanDeadbandMax = tempSetpoint + 1.0;
    float fanDeadbandMin = tempSetpoint - 1.0;
    float heaterDeadbandMax = tempSetpoint + 2.0;
    float heaterDeadbandMin = tempSetpoint - 3.0;

    if (millis() - controlPreviousMillis() >= 240000){ //controls every four minutes
        // Heater Control
        if (tempAvg > heaterDeadbandMax) {
            digitalWrite(heaterTRIACPin, LOW);
            digitalWrite(heaterLEDPin, LOW);
            heaterValue = false;
        } else if (tempAvg < heaterDeadbandMin) {
            digitalWrite(heaterTRIACPin, HIGH);
            digitalWrite(heaterLEDPin, HIGH);
            heaterValue = true;
        }

        // Fan Control
        digitalWrite(fanMOSFETPin, HIGH);
        if (tempAvg > fanDeadbandMax) {
            ds3502.setWiper(127);
            wiperVal = 127;
        } else if (tempAvg >= fanDeadbandMin && tempAvg <= fanDeadbandMax) {
            ds3502.setWiper(86);
            wiperVal = 86;
        } else {
            ds3502.setWiper(43);
            wiperVal = 43;
        }

        controlPreviousMillis = millis();
    }    
}

void errorLight(){
    if (errorTime - millis() <= 10000){ //lights up yellow for 10 seconds
        analogWrite(redRGBPin, 100);
        analogWrite(greenRGBPin, 100);
    }
    else {
        turnOffLEDs();
    }
}

void sdCardWrite(){
    if (millis() - writePreviousMillis >= 240000){ //writes every four minutes
        myFile = SD.open("data.csv", FILE_WRITE);
        myFile.print(hours);
        myFile.print(",");
        myFile.print(minutes);
        myFile.print(",");
        myFile.print(seconds);
        myFile.print(",");
        myFile.print(avgTemp);
        myFile.print(",");
        myFile.print(hum);
        myFile.print(",");
        myFile.print(heaterValue);
        myFile.print(",");
        myFile.print(wiperVal);
        myFile.println();
        myFile.close();
    }
}

void readSensors() {
    // Read sensors every 2 seconds
    if (millis() - lastSensorUpdate > 2000) {
        sensors.requestTemperatures(); //[cite: 158]
        tempCsensor1 = sensors.getTempC(sensor1);
        tempCsensor2 = sensors.getTempC(sensor2);
        tempAvg = (tempCsensor1 + tempCsensor2) / 2.0;

        hum = humidity.readHumidity(); //[cite: 160]

        lcd2.setCursor(0, 3);
        lcd2.print(tempAvg);
   
        lastSensorUpdate = millis();
    }
}