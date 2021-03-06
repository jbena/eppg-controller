// Copyright 2018 <Zach Whitehead>
// OpenPPG

#include <AceButton.h>
#include <Adafruit_DRV2605.h>  // haptic controller
#include <Adafruit_SSD1306.h>  // screen
#include <AdjustableButtonConfig.h>
#include <ResponsiveAnalogRead.h>  // smoothing for throttle
#include <Servo.h>  // to control ESCs
#include <SPI.h>
#include <TimeLib.h>
#include <Wire.h>

using namespace ace_button;

// Arduino Pins
#define BATT_IN       A1  // Battery voltage in (3.3v max)
#define BUTTON_TOP    6   // arm/disarm button_top
#define BUTTON_SIDE   7   // secondary button_top
#define BUZZER_PIN    5   // output for buzzer speaker
#define ESC_PIN       12  // the ESC signal output
#define FULL_BATT     3420 // 60v/14s(max) = 3920(3.3v) and 50v/12s(max) = ~3420
#define LED_SW        9  // output for LED on button_top switch 
#define LED_2         0  // output for LED 2 
#define LED_3         38  // output for LED 3
#define THROTTLE_PIN  A0  // throttle pot input

#define FEATURE_AUTO_PAGING false  // use button by default to change page
#define FEATURE_CRUISE false

#define CRUISE_GRACE 2  // 2 sec period to get off throttle
#define CRUISE_MAX 300  // 5 min max cruising

Adafruit_SSD1306 display(128, 64, &Wire, 4);
Adafruit_DRV2605 drv;

Servo esc;  // Creating a servo class with name of esc

ResponsiveAnalogRead pot(THROTTLE_PIN, false);
ResponsiveAnalogRead analogBatt(BATT_IN, false);
AceButton button_top(BUTTON_TOP);
AceButton button_side(BUTTON_SIDE);
AdjustableButtonConfig buttonConfig;

const int bgInterval = 750;  // background updates (milliseconds)

bool armed = false;
int page = 0;
unsigned long armedAtMilis = 0;
unsigned long cruisedAtMilis = 0;
unsigned int armedSecs = 0;
unsigned int last_throttle = 0;

unsigned long previousMillis = 0;  // stores last time background tasks done

#pragma message "Warning: OpenPPG software is in beta"

void setup() {
  delay(250); // power-up safety delay
  //Serial.begin(115200);
  //Serial.println(F("Booting up OpenPPG V2"));

  pinMode(LED_SW, OUTPUT);      // set up the external LED pin
  pinMode(LED_2, OUTPUT);       // set up the internal LED2 pin
  pinMode(LED_3, OUTPUT);       // set up the internal LED3 pin
  analogReadResolution(12);     // M0 chip provides 12bit resolution
  pot.setAnalogResolution(4096);
  analogBatt.setAnalogResolution(4096);
  analogBatt.setSnapMultiplier(0.01);  // more smoothing
  unsigned int startup_vibes[] = { 29, 29, 0 };
  runVibe(startup_vibes, 3);

  initButtons();
  initDisplay();

  esc.attach(ESC_PIN);
  esc.writeMicroseconds(0);  // make sure motors off
}

void blinkLED() {
  byte ledState = !digitalRead(LED_2);
  setLEDs(ledState);
}

void setLEDs(byte state) {
  digitalWrite(LED_2, state);
  digitalWrite(LED_SW, state);
}

void loop() {
  button_side.check();
  button_top.check();
  if (armed) {
    handleThrottle();
  }
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= bgInterval) {
    // handle background tasks
    previousMillis = currentMillis; // reset
    updateDisplay();
    if (!armed){ blinkLED();}
  }
}

float getBatteryVolts() {
  analogBatt.update();
  int sensorValue = analogBatt.getValue();
  return mapf(sensorValue, 0, FULL_BATT, 0, 50.4);
}

byte getBatteryPercent() {
  float volts = getBatteryVolts();
  // Serial.print(voltage);
  // Serial.println(" volts");
  // TODO: LiPo curve
  float percent = mapf(volts, 42, 50, 1, 100);
  // Serial.print(percent);
  // Serial.println(" percentage");
  percent = constrain(percent, 0, 100);

  return round(percent);
}

void disarmSystem() {
  unsigned int disarm_melody[] = { 2093, 1976, 880};
  unsigned int disarm_vibes[] = { 70, 33, 0 };

  esc.writeMicroseconds(0);
  armed = false;
  updateDisplay();
  // Serial.println(F("disarmed"));
  runVibe(disarm_vibes, 3);
  playMelody(disarm_melody, 3);
  delay(1500);  // dont allow immediate rearming
}

void initButtons() {
  pinMode(BUTTON_TOP, INPUT_PULLUP);
  pinMode(BUTTON_SIDE, INPUT_PULLUP);

  button_side.setButtonConfig(&buttonConfig);
  button_top.setButtonConfig(&buttonConfig);
  buttonConfig.setEventHandler(handleButtonEvent);
  buttonConfig.setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig.setFeature(ButtonConfig::kFeatureSuppressAfterDoubleClick);
}

void initDisplay() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)
  // Clear the buffer.
  display.clearDisplay();
  display.setRotation(2);  // for right hand throttle

  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(F("OpenPPG"));
  display.println(F("V2.0.1"));
  display.display();
  display.clearDisplay();
}

void handleThrottle() {
  pot.update();
  int rawval = pot.getValue();
  int val = map(rawval, 0, 4095, 1110, 2000);  // mapping val to minimum and maximum
  esc.writeMicroseconds(val);  // using val as the signal to esc
}

void armSystem() {
  unsigned int arm_melody[] = { 1760, 1976, 2093 };
  unsigned int arm_vibes[] = { 83, 27, 0 };
  // Serial.println(F("Sending Arm Signal"));
  esc.writeMicroseconds(1000);  // initialize the signal to 1000

  armed = true;
  armedAtMilis = millis();

  runVibe(arm_vibes, 3);
  playMelody(arm_melody, 3);

  setLEDs(HIGH);
}

// The event handler for the the buttons
void handleButtonEvent(AceButton *button, uint8_t eventType, uint8_t buttonState){
  uint8_t pin = button->getPin();

  switch (eventType) {
  case AceButton::kEventReleased:
    // Serial.println(F("normal clicked"));
    if (pin == BUTTON_SIDE) nextPage();
    break;
  case AceButton::kEventDoubleClicked:
    // Serial.print(F("double clicked "));
    if (pin == BUTTON_SIDE) {
    // Serial.println(F("side"));
    }else{
    // Serial.println(F("top"));
    }
    if (digitalRead(BUTTON_TOP) == LOW) {
      if (armed) {
        disarmSystem();
      } else if (throttleSafe()) {
        armSystem();
      }
    }
    break;
  }
}

// Returns true if the throttle/pot is below the safe threshold
bool throttleSafe() {
  pot.update();
  if (pot.getValue() < 100) {
    return true;
  }
  return false;
}

void runVibe(unsigned int sequence[], int siz) {
  drv.begin();
  for (int thisNote = 0; thisNote < siz; thisNote++) {
    drv.setWaveform(thisNote, sequence[thisNote]);
  }
  drv.go();
}

void playMelody(unsigned int melody[], int siz) {
  for (int thisNote = 0; thisNote < siz; thisNote++) {
    // quarter note = 1000 / 4, eigth note = 1000/8, etc.
    int noteDuration = 125;
    tone(BUZZER_PIN, melody[thisNote], noteDuration);
    delay(noteDuration);  // to distinguish the notes, delay a minimal time between them.
  }
  noTone(BUZZER_PIN);
}

void updateDisplay() {
  float voltage;
  byte percentage;
  String status;

  if (armed) {
    status = F("Armed");
    armedSecs = (millis() - armedAtMilis) / 1000;  // update time while armed
  } else {
    status = F("Disarmd");
  }

  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(status);
  display.setTextSize(4);

  switch (page) {
  case 0:
    voltage = getBatteryVolts();
    display.print(voltage, 1);
    display.println(F("V"));
    break;
  case 1:
    percentage = getBatteryPercent();
    display.print(percentage, 1);
    display.println(F("%"));
    break;
  case 2:
    displayTime(armedSecs);
    break;
  default:
    display.println(F("Dsp Err"));  // should never hit this
    break;
  }
  if (FEATURE_AUTO_PAGING) nextPage();
  display.display();
  display.clearDisplay();
}

// displays number of minutes and seconds (since armed)
void displayTime(int val) {
  int minutes = val / 60;
  int seconds = numberOfSeconds(val);

  printDigits(minutes);
  display.print(":");
  printDigits(seconds);
}
