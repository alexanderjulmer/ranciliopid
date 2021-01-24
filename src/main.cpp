/********************************************************
   Version 2.1.1  (29.05.2020) MQTT
******************************************************/

/********************************************************
  INCLUDES
******************************************************/
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include "userConfig.h" // needs to be configured by the user
#include <U8g2lib.h>
#include "PID_v1.h"            //for PID calculation
#include <DallasTemperature.h> //Library for dallas temp sensor
#include "TSIC.h"              //Library for TSIC temp sensor
#if ESP8266
#include <BlynkSimpleEsp8266.h>
#else
#include <BlynkSimpleEsp32.h>
#endif
#include "icon.h" //user icons for display
#include "MQTT.h"
#include <HX711.h>

/********************************************************
  DEFINES
******************************************************/
#define DEBUGMODE // Debug mode is active if #define DEBUGMODE is set

//#define BLYNK_PRINT Serial    // In detail debugging for blynk
//#define BLYNK_DEBUG

#ifndef DEBUGMODE
#define DEBUG_println(a)
#define DEBUG_print(a)
#define DEBUGSTART(a)
#else
#define DEBUG_println(a) Serial.println(a);
#define DEBUG_print(a) Serial.print(a);
#define DEBUGSTART(a) Serial.begin(a);
#endif

#define pinRelayVentil 33 //Output pin for 3-way-valve
#define pinRelayPumpe 32  //Output pin for pump
#define pinRelayHeater 15 //Output pin for heater

#define pinClockWeightCellLeft 27      // Clock pin for left weight cell
#define pinClockWeightCellRight 25     // Clock pin for right weight cell
#define pinDataWeightCellLeft 14       // Data pin for left weight cell
#define pinDataWeightCellRight 26      // Data pin for right weight cell
#define calibrationWeightCellLeft 170  // Calibration Value left
#define calibrationWeightCellRight 170 // Calibration Value right

#define OLED_RESET -1    //Output pin for dispaly reset pin
#define OLED_SCL 22       //Output pin for dispaly clock pin
#define OLED_SDA 21       //Output pin for dispaly data pin
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define ONE_WIRE_BUS 13 // Data wire is plugged into port 2 on the Arduino

/********************************************************
   DISPLAY constructor, change if needed
******************************************************/
//DISPLAY constructor, change if needed
#if DISPLAY == 1
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0); //e.g. 1.3"
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0); //e.g. 0.96"
#endif

/********************************************************
  definitions below must be changed in the userConfig.h file
******************************************************/
int Offlinemodus = OFFLINEMODUS;
const int OnlyPID = ONLYPID;
const int TempSensor = TEMPSENSOR;
const int Brewdetection = BREWDETECTION;
const int fallback = FALLBACK;
const int triggerType = TRIGGERTYPE;
const boolean ota = OTA;
const int grafana = GRAFANA;
const unsigned long wifiConnectionDelay = WIFICINNECTIONDELAY;
const unsigned int maxWifiReconnects = MAXWIFIRECONNECTS;
int machineLogo = MACHINELOGO;

// Wifi
const char *hostname = HOSTNAME;
const char *auth = AUTH;
const char *ssid = D_SSID;
const char *pass = PASS;
unsigned long lastWifiConnectionAttempt = millis();
unsigned int wifiReconnects = 0; //actual number of reconnects

// OTA
const char *OTAhost = OTAHOST;
const char *OTApass = OTAPASS;

//Blynk
const char *blynkaddress = BLYNKADDRESS;
const int blynkport = BLYNKPORT;
unsigned int blynkReCnctFlag;      // Blynk Reconnection Flag
unsigned int blynkReCnctCount = 0; // Blynk Reconnection counter
unsigned long lastBlynkConnectionAttempt = millis();

//backflush values
const unsigned long fillTime = FILLTIME;
const unsigned long flushTime = FLUSHTIME;
int maxflushCycles = MAXFLUSHCYCLES;

//MQTT
WiFiClient net;
MQTTClient client;

/********************************************************
   declarations
******************************************************/
int pidON = 1;                                           // 1 = control loop in closed loop
int relayON, relayOFF;                                   // used for relay trigger type. Do not change!
boolean kaltstart = true;                                // true = Rancilio started for first time
boolean emergencyStop = false;                           // Notstop bei zu hoher Temperatur
const char *sysVersion PROGMEM = "Version 2.1.1 MASTER"; //System version
int inX = 0, inY = 0, inOld = 0, inSum = 0;              //used for filter()
int bars = 0;                                            //used for getSignalStrength()
boolean brewDetected = 0;
boolean setupDone = false;
int backflushON = 0;     // 1 = activate backflush
int flushCycles = 0;     // number of active flush cycles
int backflushState = 10; // counter for state machine

/********************************************************
   moving average - brewdetection
*****************************************************/
const int numReadings = 15;              // number of values per Array
double readingstemp[numReadings];        // the readings from Temp
unsigned long readingstime[numReadings]; // the readings from time
double readingchangerate[numReadings];

int readIndex = 1;          // the index of the current reading
double total = 0;           // total sum of readingchangerate[]
double heatrateaverage = 0; // the average over the numReadings
double changerate = 0;      // local change rate of temprature
double heatrateaveragemin = 0;
unsigned long timeBrewdetection = 0;
int timerBrewdetection = 0; // flag is set if brew was detected
int firstreading = 1;       // Ini of the field, also used for sensor check

/********************************************************
   PID - values for offline brewdetection
*****************************************************/
double aggbKp = AGGBKP;
double aggbTn = AGGBTN;
double aggbTv = AGGBTV;
#if aggbTn == 0
double aggbKi = 0;
#else
double aggbKi = aggbKp / aggbTn;
#endif
double aggbKd = aggbTv * aggbKp;
double brewtimersoftware = 45; // 20-5 for detection
double brewboarder = 150;      // border for the detection, be carefull: to low: risk of wrong brew detection and rising temperature
const int PonE = PONE;

/********************************************************
   Analog Input
******************************************************/
const int analogPin = 0; // AI0 will be used
int brewcounter = 10;
int brewswitch = 0;
double brewtime = 25000;                            //brewtime in ms
double totalbrewtime = 0;                           //total brewtime set in softare or blynk
double preinfusion = 2000;                          //preinfusion time in ms
double preinfusionpause = 5000;                     //preinfusion pause time in ms
unsigned long bezugsZeit = 0;                       //total brewed time
unsigned long startZeit = 0;                        //start time of brew
const unsigned long analogreadingtimeinterval = 10; // ms
unsigned long previousMillistempanalogreading;      // ms for analogreading

/********************************************************
   Weight Cells
******************************************************/
long targetWeight = 30.0;

/********************************************************
   Sensor check
******************************************************/
boolean sensorError = false;
int error = 0;
int maxErrorCounter = 10; //depends on intervaltempmes* , define max seconds for invalid data

/********************************************************
   PID
******************************************************/
unsigned long previousMillistemp; // initialisation at the end of init()
const unsigned long intervaltempmestsic = 400;
const unsigned long intervaltempmesds18b20 = 400;
int pidMode = 1; //1 = Automatic, 0 = Manual

//volatile unsigned int interruptCounter;
//int totalInterruptCounter;

hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

const unsigned int windowSize = 1000;
volatile unsigned int isrCounter = 0; // counter for ISR
//unsigned long windowStartTime;

double Input, Output;
double setPointTemp;
double previousInput = 0;

double setPoint = SETPOINT;
double aggKp = AGGKP;
double aggTn = AGGTN;
double aggTv = AGGTV;
double startKp = STARTKP;
double startTn = STARTTN;
#if startTn == 0
double startKi = 0;
#else
double startKi = startKp / startTn;
#endif

#if aggTn == 0
double aggKi = 0;
#else
double aggKi = aggKp / aggTn;
#endif
double aggKd = aggTv * aggKp;

PID bPID(&Input, &Output, &setPoint, aggKp, aggKi, aggKd, PonE, DIRECT); //PID initialisation

/********************************************************
   DALLAS TEMP
******************************************************/
OneWire oneWire(ONE_WIRE_BUS);       // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire); // Pass our oneWire reference to Dallas Temperature.
DeviceAddress sensorDeviceAddress;   // arrays to hold device address

/********************************************************
   HX7111
******************************************************/
HX711 weightCellLeft;
HX711 weightCellRight;

/********************************************************
   Temp Sensors TSIC 306
******************************************************/
TSIC Sensor1(ONE_WIRE_BUS); // only Signalpin, VCCpin unused by default
uint16_t temperature = 0;   // internal variable used to read temeprature
float Temperatur_C = 0;     // internal variable that holds the converted temperature in °C

/********************************************************
   BLYNK
******************************************************/
//Update Intervall zur App
unsigned long previousMillisBlynk; // initialisation at the end of init()
const unsigned long intervalBlynk = 1000;
int blynksendcounter = 1;

//Update für Display
unsigned long previousMillisDisplay; // initialisation at the end of init()
const unsigned long intervalDisplay = 500;

/********************************************************
   BLYNK define pins and read values
******************************************************/
BLYNK_CONNECTED()
{
  if (Offlinemodus == 0)
  {
    Blynk.syncAll();
    //rtc.begin();
  }
}

BLYNK_WRITE(V4)
{
  aggKp = param.asDouble();
}

BLYNK_WRITE(V5)
{
  aggTn = param.asDouble();
}
BLYNK_WRITE(V6)
{
  aggTv = param.asDouble();
}

BLYNK_WRITE(V7)
{
  setPoint = param.asDouble();
}

BLYNK_WRITE(V8)
{
  brewtime = param.asDouble() * 1000;
}

BLYNK_WRITE(V9)
{
  preinfusion = param.asDouble() * 1000;
}

BLYNK_WRITE(V10)
{
  preinfusionpause = param.asDouble() * 1000;
}
BLYNK_WRITE(V13)
{
  pidON = param.asInt();
}
BLYNK_WRITE(V30)
{
  aggbKp = param.asDouble(); //
}

BLYNK_WRITE(V31)
{
  aggbTn = param.asDouble();
}
BLYNK_WRITE(V32)
{
  aggbTv = param.asDouble();
}
BLYNK_WRITE(V33)
{
  brewtimersoftware = param.asDouble();
}
BLYNK_WRITE(V34)
{
  brewboarder = param.asDouble();
}
BLYNK_WRITE(V40)
{
  backflushON = param.asInt();
}

#if (COLDSTART_PID == 2) // 2=?Blynk values, else default starttemp from config
BLYNK_WRITE(V11)
{
  startKp = param.asDouble();
}
BLYNK_WRITE(V14)
{
  startTn = param.asDouble();
}
#endif

/********************************************************
  after ~28 cycles the input is set to 99,66% if the real input value
  sum of inX and inY multiplier must be 1
  increase inX multiplier to make the filter faster
*****************************************************/
int filter(int input)
{
  inX = input * 0.3;
  inY = inOld * 0.7;
  inSum = inX + inY;
  inOld = inSum;

  return inSum;
}

/********************************************************
  Get Wifi signal strength and set bars for display
*****************************************************/
void getSignalStrength()
{
  if (Offlinemodus == 1)
    return;

  long rssi;
  if (WiFi.status() == WL_CONNECTED)
  {
    rssi = WiFi.RSSI();
  }
  else
  {
    rssi = -100;
  }

  if (rssi >= -50)
  {
    bars = 4;
  }
  else if ((rssi < -50) & (rssi >= -65))
  {
    bars = 3;
  }
  else if ((rssi < -65) & (rssi >= -75))
  {
    bars = 2;
  }
  else if ((rssi < -75) & (rssi >= -80))
  {
    bars = 1;
  }
  else
  {
    bars = 0;
  }
}

/********************************************************
  Read analog input pin
*****************************************************/
void readAnalogInput()
{
  unsigned long currentMillistemp = millis();
  if (currentMillistemp - previousMillistempanalogreading >= analogreadingtimeinterval)
  {
    previousMillistempanalogreading = currentMillistemp;
    brewswitch = filter(analogRead(analogPin));
  }
}

/********************************************************
  Emergency stop inf temp is to high
*****************************************************/
void testEmergencyStop()
{
  if (Input > 120 && emergencyStop == false)
  {
    emergencyStop = true;
  }
  else if (Input < 100 && emergencyStop == true)
  {
    emergencyStop = false;
  }
}

void backflush()
{
  if (backflushState != 10 && backflushON == 0)
  {
    backflushState = 43; // force reset in case backflushON is reset during backflush!
  }
  else if (Offlinemodus == 1 || brewcounter > 10 || maxflushCycles <= 0 || backflushON == 0)
  {
    return;
  }

  if (pidMode == 1)
  { //Deactivate PID
    pidMode = 0;
    bPID.SetMode(pidMode);
    Output = 0;
  }
  digitalWrite(pinRelayHeater, LOW); //Stop heating

  readAnalogInput();

  if (brewswitch < 1000 && backflushState > 10)
  { //abort function for state machine from every state
    backflushState = 43;
  }

  // state machine for brew
  switch (backflushState)
  {
  case 10: // waiting step for brew switch turning on
    if (brewswitch > 1000 && backflushON)
    {
      startZeit = millis();
      backflushState = 20;
    }
    break;
  case 20: //portafilter filling
    DEBUG_println("portafilter filling");
    digitalWrite(pinRelayVentil, relayON);
    digitalWrite(pinRelayPumpe, relayON);
    backflushState = 21;
    break;
  case 21: //waiting time for portafilter filling
    if (millis() - startZeit > FILLTIME)
    {
      startZeit = millis();
      backflushState = 30;
    }
    break;
  case 30: //flushing
    DEBUG_println("flushing");
    digitalWrite(pinRelayVentil, relayOFF);
    digitalWrite(pinRelayPumpe, relayOFF);
    flushCycles++;
    backflushState = 31;
    break;
  case 31: //waiting time for flushing
    if (millis() - startZeit > flushTime && flushCycles < maxflushCycles)
    {
      startZeit = millis();
      backflushState = 20;
    }
    else if (flushCycles >= maxflushCycles)
    {
      backflushState = 43;
    }
    break;
  case 43: // waiting for brewswitch off position
    if (brewswitch < 1000)
    {
      DEBUG_println("backflush finished");
      digitalWrite(pinRelayVentil, relayOFF);
      digitalWrite(pinRelayPumpe, relayOFF);
      flushCycles = 0;
      backflushState = 10;
    }
    break;
  }
}

/********************************************************
  initialize u8g2 display
*****************************************************/
void u8g2_prepare(void)
{
  //u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.setFont(u8g2_font_profont11_tf);
  //u8g2.setFont(u8g2_font_IPAandRUSLCD_tf);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);
  u8g2.setDisplayRotation(DISPALYROTATE);
}

/********************************************************
  DISPLAY - print message
*****************************************************/
void displayMessage(String text1, String text2, String text3, String text4, String text5, String text6)
{
  u8g2.clearBuffer();
  u8g2.setCursor(0, 0);
  u8g2.print(text1);
  u8g2.setCursor(0, 10);
  u8g2.print(text2);
  u8g2.setCursor(0, 20);
  u8g2.print(text3);
  u8g2.setCursor(0, 30);
  u8g2.print(text4);
  u8g2.setCursor(0, 40);
  u8g2.print(text5);
  u8g2.setCursor(0, 50);
  u8g2.print(text6);
  u8g2.sendBuffer();
}

/********************************************************
  DISPLAY - print logo and message at boot
*****************************************************/
void displayLogo(String displaymessagetext, String displaymessagetext2)
{
  u8g2.clearBuffer();
  u8g2.drawStr(0, 47, displaymessagetext.c_str());
  u8g2.drawStr(0, 55, displaymessagetext2.c_str());
  //Rancilio startup logo
  if (machineLogo == 1)
  {
    u8g2.drawXBMP(41, 2, startLogoRancilio_width, startLogoRancilio_height, startLogoRancilio_bits);
  }
  else if (machineLogo == 2)
  {
    u8g2.drawXBMP(0, 2, startLogoGaggia_width, startLogoGaggia_height, startLogoGaggia_bits);
  }
  u8g2.sendBuffer();
}

/********************************************************
  DISPLAY - EmergencyStop
*****************************************************/
void displayEmergencyStop(void)
{
  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 0, logo_width, logo_height, logo_bits_u8g2); //draw temp icon
  u8g2.setCursor(32, 24);
  u8g2.print("Ist :  ");
  u8g2.print(Input, 1);
  u8g2.print(" ");
  u8g2.print((char)176);
  u8g2.print("C");
  u8g2.setCursor(32, 34);
  u8g2.print("Soll:  ");
  u8g2.print(setPoint, 1);
  u8g2.print(" ");
  u8g2.print((char)176);
  u8g2.print("C");

  //draw current temp in icon
  if (isrCounter < 500)
  {
    u8g2.drawLine(9, 48, 9, 5);
    u8g2.drawLine(10, 48, 10, 4);
    u8g2.drawLine(11, 48, 11, 3);
    u8g2.drawLine(12, 48, 12, 4);
    u8g2.drawLine(13, 48, 13, 5);
    u8g2.setCursor(32, 4);
    u8g2.print("HEATING STOPPED");
  }
  u8g2.sendBuffer();
}

/********************************************************
  Moving average - brewdetection (SW)
*****************************************************/
void movAvg()
{
  if (firstreading == 1)
  {
    for (int thisReading = 0; thisReading < numReadings; thisReading++)
    {
      readingstemp[thisReading] = Input;
      readingstime[thisReading] = 0;
      readingchangerate[thisReading] = 0;
    }
    firstreading = 0;
  }

  readingstime[readIndex] = millis();
  readingstemp[readIndex] = Input;

  if (readIndex == numReadings - 1)
  {
    changerate = (readingstemp[numReadings - 1] - readingstemp[0]) / (readingstime[numReadings - 1] - readingstime[0]) * 10000;
  }
  else
  {
    changerate = (readingstemp[readIndex] - readingstemp[readIndex + 1]) / (readingstime[readIndex] - readingstime[readIndex + 1]) * 10000;
  }

  readingchangerate[readIndex] = changerate;
  total = 0;
  for (int i = 0; i < numReadings; i++)
  {
    total += readingchangerate[i];
  }

  heatrateaverage = total / numReadings * 100;
  if (heatrateaveragemin > heatrateaverage)
  {
    heatrateaveragemin = heatrateaverage;
  }

  if (readIndex >= numReadings - 1)
  {
    // ...wrap around to the beginning:
    readIndex = 0;
  }
  else
  {
    readIndex++;
  }
}

/********************************************************
  check sensor value.
  If < 0 or difference between old and new >25, then increase error.
  If error is equal to maxErrorCounter, then set sensorError
*****************************************************/
boolean checkSensor(float tempInput)
{
  boolean sensorOK = false;
  boolean badCondition = (tempInput < 0 || tempInput > 150 || fabs(tempInput - previousInput) > 5);
  if (badCondition && !sensorError)
  {
    error++;
    sensorOK = false;
    DEBUG_print("WARN: temperature sensor reading: consec_errors = ");
    DEBUG_print(error);
    DEBUG_print(", temp_current = ");
    DEBUG_println(tempInput);
  }
  else if (badCondition == false && sensorOK == false)
  {
    error = 0;
    sensorOK = true;
  }
  if (error >= maxErrorCounter && !sensorError)
  {
    sensorError = true;
    DEBUG_print("ERROR: temperature sensor malfunction: emp_current = ");
    DEBUG_println(tempInput);
  }
  else if (error == 0 && sensorError)
  {
    sensorError = false;
  }
  return sensorOK;
}

/********************************************************
  Refresh temperature.
  Each time checkSensor() is called to verify the value.
  If the value is not valid, new data is not stored.
*****************************************************/
void refreshTemp()
{
  unsigned long currentMillistemp = millis();
  previousInput = Input;
  if (TempSensor == 1)
  {
    if (currentMillistemp - previousMillistemp >= intervaltempmesds18b20)
    {
      previousMillistemp = currentMillistemp;
      sensors.requestTemperatures();
      if (!checkSensor(sensors.getTempCByIndex(0)) && firstreading == 0)
        return; //if sensor data is not valid, abort function; Sensor must be read at least one time at system startup
      Input = sensors.getTempCByIndex(0);
      if (Brewdetection != 0)
      {
        movAvg();
      }
      else if (firstreading != 0)
      {
        firstreading = 0;
      }
    }
  }
  if (TempSensor == 2)
  {
    if (currentMillistemp - previousMillistemp >= intervaltempmestsic)
    {
      previousMillistemp = currentMillistemp;
      /*  variable "temperature" must be set to zero, before reading new data
            getTemperature only updates if data is valid, otherwise "temperature" will still hold old values
      */
      temperature = 0;
      Sensor1.getTemperature(&temperature);
      Temperatur_C = Sensor1.calc_Celsius(&temperature);
      //Temperatur_C = random(130,131);
      if (!checkSensor(Temperatur_C) && firstreading == 0)
        return; //if sensor data is not valid, abort function; Sensor must be read at least one time at system startup
      Input = Temperatur_C;
      if (Brewdetection != 0)
      {
        movAvg();
      }
      else if (firstreading != 0)
      {
        firstreading = 0;
      }
    }
  }
}

/********************************************************
    Functions for scales
******************************************************/

bool targetWeightReached()
{
  long left = weightCellLeft.get_units();
  long right = weightCellRight.get_units();
  long currentWeight = (left + right);
  Serial.println(currentWeight);
  return (currentWeight >= targetWeight);
};

/********************************************************
    PreInfusion, Brew , if not Only PID
******************************************************/
void brew()
{
  if (OnlyPID == 0)
  {
    readAnalogInput();
    unsigned long currentMillistemp = millis();

    if (brewswitch < 1000 && brewcounter > 10)
    { //abort function for state machine from every state
      brewcounter = 43;
    }

    if (brewcounter > 10)
    {
      bezugsZeit = currentMillistemp - startZeit;
      weightCellLeft.tare();
      weightCellRight.tare();
    }

    totalbrewtime = preinfusion + preinfusionpause + brewtime; // running every cycle, in case changes are done during brew

    // state machine for brew
    switch (brewcounter)
    {
    case 10: // waiting step for brew switch turning on
      if (brewswitch > 1000 && backflushState == 10 && backflushON == 0)
      {
        startZeit = millis();
        brewcounter = 20;
        kaltstart = false; // force reset kaltstart if shot is pulled
      }
      else
      {
        backflush();
      }
      break;
    case 20: //preinfusioon
      DEBUG_println("Preinfusion");
      digitalWrite(pinRelayVentil, relayON);
      digitalWrite(pinRelayPumpe, relayON);
      brewcounter = 21;
      break;
    case 21: //waiting time preinfusion
      if (bezugsZeit > preinfusion)
      {
        brewcounter = 30;
      }
      break;
    case 30: //preinfusion pause
      DEBUG_println("preinfusion pause");
      digitalWrite(pinRelayVentil, relayON);
      digitalWrite(pinRelayPumpe, relayOFF);
      brewcounter = 31;
      break;
    case 31: //waiting time preinfusion pause
      if (bezugsZeit > preinfusion + preinfusionpause)
      {
        brewcounter = 40;
      }
      break;
    case 40: //brew running
      DEBUG_println("Brew started");
      digitalWrite(pinRelayVentil, relayON);
      digitalWrite(pinRelayPumpe, relayON);
      brewcounter = 41;
      break;
    case 41: //waiting time brew
      if ((bezugsZeit > totalbrewtime) || targetWeightReached())
      {
        brewcounter = 42;
      }
      break;
    case 42: //brew finished
      DEBUG_println("Brew stopped");
      digitalWrite(pinRelayVentil, relayOFF);
      digitalWrite(pinRelayPumpe, relayOFF);
      brewcounter = 43;
      break;
    case 43: // waiting for brewswitch off position
      if (brewswitch < 1000)
      {
        digitalWrite(pinRelayVentil, relayOFF);
        digitalWrite(pinRelayPumpe, relayOFF);
        currentMillistemp = 0;
        bezugsZeit = 0;
        brewDetected = 0; //rearm brewdetection
        brewcounter = 10;
      }
      break;
    }
  }
}

/*******************************************************
  Switch to offline modeif maxWifiReconnects were exceeded
  during boot
*****************************************************/
void initOfflineMode()
{
  displayMessage("", "", "", "", "Begin Fallback,", "No Wifi");
  DEBUG_println("Start offline mode with eeprom values, no wifi:(");
  Offlinemodus = 1;

  EEPROM.begin(1024); // open eeprom
  double dummy;       // check if eeprom values are numeric (only check first value in eeprom)
  EEPROM.get(0, dummy);
  DEBUG_print("check eeprom 0x00 in dummy: ");
  DEBUG_println(dummy);
  if (!isnan(dummy))
  {
    EEPROM.get(0, aggKp);
    EEPROM.get(10, aggTn);
    EEPROM.get(20, aggTv);
    EEPROM.get(30, setPoint);
    EEPROM.get(40, brewtime);
    EEPROM.get(50, preinfusion);
    EEPROM.get(60, preinfusionpause);
    EEPROM.get(90, aggbKp);
    EEPROM.get(100, aggbTn);
    EEPROM.get(110, aggbTv);
    EEPROM.get(120, brewtimersoftware);
    EEPROM.get(130, brewboarder);
  }
  else
  {
    displayMessage("", "", "", "", "No eeprom,", "Values");
    DEBUG_println("No working eeprom value, I am sorry, but use default offline value  :)");
    delay(1000);
  }
  // eeeprom schließen
  EEPROM.commit();
}

/*******************************************************
   Check if Wifi is connected, if not reconnect
   abort function if offline, or brew is running
*****************************************************/
void checkWifi()
{
  if (Offlinemodus == 1 || brewcounter > 11)
    return;
  do
  {
    if ((millis() - lastWifiConnectionAttempt >= wifiConnectionDelay) && (wifiReconnects <= maxWifiReconnects))
    {
      int statusTemp = WiFi.status();
      if (statusTemp != WL_CONNECTED)
      { // check WiFi connection status
        lastWifiConnectionAttempt = millis();
        wifiReconnects++;
        DEBUG_print("Attempting WIFI reconnection: ");
        DEBUG_println(wifiReconnects);
        if (!setupDone)
        {
          displayMessage("", "", "", "", "Wifi reconnect:", String(wifiReconnects));
        }
        WiFi.disconnect();
        WiFi.begin(ssid, pass); // attempt to connect to Wifi network
        int count = 1;
        while (WiFi.status() != WL_CONNECTED && count <= 20)
        {
          delay(100); //give WIFI some time to connect
          count++;    //reconnect counter, maximum waiting time for reconnect = 20*100ms
        }
      }
    }
    yield();                                                                                   //Prevent WDT trigger
  } while (!setupDone && wifiReconnects < maxWifiReconnects && WiFi.status() != WL_CONNECTED); //if kaltstart ist still true when checkWifi() is called, then there was no WIFI connection at boot -> connect or offlinemode

  if (wifiReconnects >= maxWifiReconnects && !setupDone)
  { // no wifi connection after boot, initiate offline mode (only directly after boot)
    initOfflineMode();
  }
}

/*******************************************************
   Check if Blynk is connected, if not reconnect
   abort function if offline, or brew is running
   blynk is also using maxWifiReconnects!
*****************************************************/
void checkBlynk()
{
  if (Offlinemodus == 1 || brewcounter > 11)
    return;
  if ((millis() - lastBlynkConnectionAttempt >= wifiConnectionDelay) && (blynkReCnctCount <= maxWifiReconnects))
  {
    int statusTemp = Blynk.connected();
    if (statusTemp != 1)
    {                                        // check Blynk connection status
      lastBlynkConnectionAttempt = millis(); // Reconnection Timer Function
      blynkReCnctCount++;                    // Increment reconnection Counter
      DEBUG_print("Attempting blynk reconnection: ");
      DEBUG_println(blynkReCnctCount);
      Blynk.connect(3000); // Try to reconnect to the server; connect() is a blocking function, watch the timeout!
    }
  }
}

/********************************************************
    send data to display
******************************************************/
void printScreen()
{
  unsigned long currentMillisDisplay = millis();
  if (currentMillisDisplay - previousMillisDisplay >= intervalDisplay)
  {
    previousMillisDisplay = currentMillisDisplay;
    if (!sensorError)
    {
      u8g2.clearBuffer();
      u8g2.drawXBMP(0, 0, logo_width, logo_height, logo_bits_u8g2); //draw temp icon
      u8g2.setCursor(32, 14);
      u8g2.print("Ist :  ");
      u8g2.print(Input, 1);
      u8g2.print(" ");
      u8g2.print((char)176);
      u8g2.print("C");
      u8g2.setCursor(32, 24);
      u8g2.print("Soll:  ");
      u8g2.print(setPoint, 1);
      u8g2.print(" ");
      u8g2.print((char)176);
      u8g2.print("C");

      // Draw heat bar
      u8g2.drawLine(15, 58, 117, 58);
      u8g2.drawLine(15, 58, 15, 61);
      u8g2.drawLine(117, 58, 117, 61);

      u8g2.drawLine(16, 59, (Output / 10) + 16, 59);
      u8g2.drawLine(16, 60, (Output / 10) + 16, 60);
      u8g2.drawLine(15, 61, 117, 61);

      //draw current temp in icon
      if (fabs(Input - setPoint) < 0.3)
      {
        if (isrCounter < 500)
        {
          u8g2.drawLine(9, 48, 9, 58 - (Input / 2));
          u8g2.drawLine(10, 48, 10, 58 - (Input / 2));
          u8g2.drawLine(11, 48, 11, 58 - (Input / 2));
          u8g2.drawLine(12, 48, 12, 58 - (Input / 2));
          u8g2.drawLine(13, 48, 13, 58 - (Input / 2));
        }
      }
      else if (Input > 106)
      {
        u8g2.drawLine(9, 48, 9, 5);
        u8g2.drawLine(10, 48, 10, 4);
        u8g2.drawLine(11, 48, 11, 3);
        u8g2.drawLine(12, 48, 12, 4);
        u8g2.drawLine(13, 48, 13, 5);
      }
      else
      {
        u8g2.drawLine(9, 48, 9, 58 - (Input / 2));
        u8g2.drawLine(10, 48, 10, 58 - (Input / 2));
        u8g2.drawLine(11, 48, 11, 58 - (Input / 2));
        u8g2.drawLine(12, 48, 12, 58 - (Input / 2));
        u8g2.drawLine(13, 48, 13, 58 - (Input / 2));
      }

      //draw setPoint line
      u8g2.drawLine(18, 58 - (setPoint / 2), 23, 58 - (setPoint / 2));

      // PID Werte ueber heatbar
      u8g2.setCursor(40, 48);

      u8g2.print(bPID.GetKp(), 0); // P
      u8g2.print("|");
      if (bPID.GetKi() != 0)
      {
        u8g2.print(bPID.GetKp() / bPID.GetKi(), 0);
        ;
      } // I
      else
      {
        u8g2.print("0");
      }
      u8g2.print("|");
      u8g2.print(bPID.GetKd() / bPID.GetKp(), 0); // D
      u8g2.setCursor(98, 48);
      if (Output < 99)
      {
        u8g2.print(Output / 10, 1);
      }
      else
      {
        u8g2.print(Output / 10, 0);
      }
      u8g2.print("%");

      // Brew
      u8g2.setCursor(32, 34);
      u8g2.print("Brew:  ");
      u8g2.print(bezugsZeit / 1000, 1);
      u8g2.print("/");
      if (ONLYPID == 1)
      {
        u8g2.print(brewtimersoftware, 0); // deaktivieren wenn Preinfusion ( // voransetzen )
      }
      else
      {
        u8g2.print(totalbrewtime / 1000); // aktivieren wenn Preinfusion
      }
      //draw box
      u8g2.drawFrame(0, 0, 128, 64);

      // Für Statusinfos
      u8g2.drawFrame(32, 0, 84, 12);
      if (Offlinemodus == 0)
      {
        getSignalStrength();
        if (WiFi.status() == WL_CONNECTED)
        {
          u8g2.drawXBMP(40, 2, 8, 8, antenna_OK_u8g2);
          for (int b = 0; b <= bars; b++)
          {
            u8g2.drawVLine(45 + (b * 2), 10 - (b * 2), b * 2);
          }
        }
        else
        {
          u8g2.drawXBMP(40, 2, 8, 8, antenna_NOK_u8g2);
          u8g2.setCursor(88, 2);
          u8g2.print("RC: ");
          u8g2.print(wifiReconnects);
        }
        if (Blynk.connected())
        {
          u8g2.drawXBMP(60, 2, 11, 8, blynk_OK_u8g2);
        }
        else
        {
          u8g2.drawXBMP(60, 2, 8, 8, blynk_NOK_u8g2);
        }
        if (MQTT == 1)
        {
          if (client.connect("arduino", "try", "try"))
          {
            u8g2.setCursor(77, 2);
            u8g2.print("MQTT");
          }
          else
          {
            u8g2.setCursor(77, 2);
            u8g2.print("");
          }
        }
      }
      else
      {
        u8g2.setCursor(40, 2);
        u8g2.print("Offlinemodus");
      }
      u8g2.sendBuffer();
    }
  }
}

/********************************************************
  send data to Blynk server
*****************************************************/

void sendToBlynk()
{
  if (Offlinemodus == 1)
    return;

  unsigned long currentMillisBlynk = millis();

  if (currentMillisBlynk - previousMillisBlynk >= intervalBlynk)
  {

    //MQTT
    if (MQTT == 1)
    {
      if (client.connect("arduino", "try", "try"))
      {
        DEBUG_println("MQTT connected");
      }
      else
      {
        DEBUG_println("MQTT connection failed");
      }
    }

    previousMillisBlynk = currentMillisBlynk;
    if (Blynk.connected())
    {
      if (blynksendcounter == 1)
      {
        Blynk.virtualWrite(V2, Input);
        //MQTT
        if (MQTT == 1)
        {
          client.publish("/temp", String(Input));
        }
      }
      if (blynksendcounter == 2)
      {
        Blynk.virtualWrite(V23, Output);
      }
      if (blynksendcounter == 3)
      {
        Blynk.virtualWrite(V7, setPoint);
        //MQTT
        if (MQTT == 1)
        {
          client.publish("/setPoint", String(setPoint));
        }
      }
      if (blynksendcounter == 4)
      {
        Blynk.virtualWrite(V35, heatrateaverage);
      }
      if (blynksendcounter == 5)
      {
        Blynk.virtualWrite(V36, heatrateaveragemin);
      }
      if (grafana == 1 && blynksendcounter >= 6)
      {
        Blynk.virtualWrite(V60, Input, Output, bPID.GetKp(), bPID.GetKi(), bPID.GetKd(), setPoint);
        blynksendcounter = 0;
      }
      else if (grafana == 0 && blynksendcounter >= 5)
      {
        blynksendcounter = 0;
      }
      blynksendcounter++;
    }
  }
}

/********************************************************
    Brewdetection
******************************************************/
void brewdetection()
{
  if (brewboarder == 0)
    return; //abort brewdetection if deactivated

  // Brew detecion == 1 software solution , == 2 hardware
  if (Brewdetection == 1)
  {
    if (millis() - timeBrewdetection > brewtimersoftware * 1000)
    {
      timerBrewdetection = 0; //rearm brewdetection
      if (OnlyPID == 1)
      {
        bezugsZeit = 0; // brewdetection is used in OnlyPID mode to detect a start of brew, and set the bezugsZeit
      }
    }
  }
  else if (Brewdetection == 2)
  {
    if (millis() - timeBrewdetection > brewtimersoftware * 1000)
    {
      timerBrewdetection = 0; //rearm brewdetection
    }
  }

  if (Brewdetection == 1)
  {
    if (heatrateaverage <= -brewboarder && timerBrewdetection == 0)
    {
      DEBUG_println("SW Brew detected");
      timeBrewdetection = millis();
      timerBrewdetection = 1;
    }
  }
  else if (Brewdetection == 2)
  {
    if (brewcounter > 10 && brewDetected == 0 && brewboarder != 0)
    {
      DEBUG_println("HW Brew detected");
      timeBrewdetection = millis();
      timerBrewdetection = 1;
      brewDetected = 1;
    }
  }
}

/********************************************************
    Timer 1 - ISR for PID calculation and heat realay output
******************************************************/
void IRAM_ATTR onTimer()
{

  portENTER_CRITICAL_ISR(&timerMux);
  //interruptCounter++;

  if (Output <= isrCounter)
  {
    digitalWrite(pinRelayHeater, LOW);
    //DEBUG_println("Power off!");
  }
  else
  {
    digitalWrite(pinRelayHeater, HIGH);
    //DEBUG_println("Power on!");
  }

  isrCounter += 10; // += 10 because one tick = 10ms
  //set PID output as relais commands
  if (isrCounter > windowSize)
  {
    isrCounter = 0;
  }

  //run PID calculation
  bPID.Compute();

  timerAlarmWrite(timer, 6250, true);

  portEXIT_CRITICAL_ISR(&timerMux);
}

//MQTT
void messageReceived(String &topic, String &payload)
{
  //DEBUG_println("incoming: " + topic + " - " + payload);
  setPoint = payload.toDouble();
}

void setup()
{
  DEBUGSTART(115200);

  if (MQTT == 1)
  {
    //MQTT
    client.begin("public.cloud.shiftr.io", net);
  }

  /********************************************************
    Define trigger type
  ******************************************************/
  if (triggerType)
  {
    relayON = HIGH;
    relayOFF = LOW;
  }
  else
  {
    relayON = LOW;
    relayOFF = HIGH;
  }

  /********************************************************
    Init Pins
  ******************************************************/
  pinMode(pinRelayVentil, OUTPUT);
  pinMode(pinRelayPumpe, OUTPUT);
  pinMode(pinRelayHeater, OUTPUT);
  digitalWrite(pinRelayVentil, relayOFF);
  digitalWrite(pinRelayPumpe, relayOFF);
  digitalWrite(pinRelayHeater, LOW);

  /********************************************************
    DISPLAY 128x64
  ******************************************************/
  u8g2.begin();
  u8g2_prepare();
  displayLogo(sysVersion, "");
  delay(2000);

  /********************************************************
     BLYNK & Fallback offline
  ******************************************************/
  if (Offlinemodus == 0)
  {
    WiFi.setHostname(hostname);
    unsigned long started = millis();
    displayLogo("1: Connect Wifi to:", ssid);
    /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
      would try to act as both a client and an access-point and could cause
      network-issues with your other WiFi-devices on your WiFi-network. */
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false); //needed, otherwise exceptions are triggered \o.O/
    WiFi.begin(ssid, pass);
    DEBUG_print("Connecting to ");
    DEBUG_print(ssid);
    DEBUG_println(" ...");

    // wait up to 20 seconds for connection:
    while ((WiFi.status() != WL_CONNECTED) && (millis() - started < 20000))
    {
      yield(); //Prevent Watchdog trigger
    }

    checkWifi(); //try to reconnect

    if (WiFi.status() == WL_CONNECTED)
    {
      DEBUG_println("WiFi connected");
      DEBUG_println("IP address: ");
      DEBUG_println(WiFi.localIP());
      DEBUG_println("Wifi works, now try Blynk (timeout 30s)");
      if (fallback == 0)
      {
        displayLogo("Connect to Blynk", "no Fallback");
      }
      else if (fallback == 1)
      {
        displayLogo("2: Wifi connected, ", "try Blynk   ");
      }
      delay(1000);

      //try blynk connection
      Blynk.config(auth, blynkaddress, blynkport);
      Blynk.connect(30000);

      if (Blynk.connected() == true)
      {
        displayLogo("3: Blynk connected", "sync all variables...");
        DEBUG_println("Blynk is online");
        if (fallback == 1)
        {
          DEBUG_println("sync all variables and write new values to eeprom");
          // Blynk.run() ;
          Blynk.syncVirtual(V4);
          Blynk.syncVirtual(V5);
          Blynk.syncVirtual(V6);
          Blynk.syncVirtual(V7);
          Blynk.syncVirtual(V8);
          Blynk.syncVirtual(V9);
          Blynk.syncVirtual(V10);
          Blynk.syncVirtual(V11);
          Blynk.syncVirtual(V12);
          Blynk.syncVirtual(V13);
          Blynk.syncVirtual(V14);
          Blynk.syncVirtual(V30);
          Blynk.syncVirtual(V31);
          Blynk.syncVirtual(V32);
          Blynk.syncVirtual(V33);
          Blynk.syncVirtual(V34);
          // Blynk.syncAll();  //sync all values from Blynk server
          // Werte in den eeprom schreiben
          // ini eeprom mit begin
          EEPROM.begin(1024);
          EEPROM.put(0, aggKp);
          EEPROM.put(10, aggTn);
          EEPROM.put(20, aggTv);
          EEPROM.put(30, setPoint);
          EEPROM.put(40, brewtime);
          EEPROM.put(50, preinfusion);
          EEPROM.put(60, preinfusionpause);
          EEPROM.put(90, aggbKp);
          EEPROM.put(100, aggbTn);
          EEPROM.put(110, aggbTv);
          EEPROM.put(120, brewtimersoftware);
          EEPROM.put(130, brewboarder);
          // eeprom schließen
          EEPROM.commit();
        }
      }
      else
      {
        DEBUG_println("No connection to Blynk");
      }
    }
    else
    {
      displayLogo("No ", "WIFI");
      DEBUG_println("No WIFI");
      WiFi.disconnect(true);
      delay(1000);
    }
  }

  /********************************************************
     OTA
  ******************************************************/
  if (ota && Offlinemodus == 0 && WiFi.status() == WL_CONNECTED)
  {
    ArduinoOTA.setHostname(OTAhost); //  Device name for OTA
    ArduinoOTA.setPassword(OTApass); //  Password for OTA
    ArduinoOTA.begin();
  }

  /********************************************************
     Ini PID
  ******************************************************/

  setPointTemp = setPoint;
  bPID.SetSampleTime(windowSize);
  bPID.SetOutputLimits(0, windowSize);
  bPID.SetMode(AUTOMATIC);

  /********************************************************
     TEMP SENSOR
  ******************************************************/
  if (TempSensor == 1)
  {
    sensors.begin();
    sensors.getAddress(sensorDeviceAddress, 0);
    sensors.setResolution(sensorDeviceAddress, 10);
    sensors.requestTemperatures();
    Input = sensors.getTempCByIndex(0);
  }

  if (TempSensor == 2)
  {
    temperature = 0;
    Sensor1.getTemperature(&temperature);
    Input = Sensor1.calc_Celsius(&temperature);
  }

  /********************************************************
     SCALES
  ******************************************************/
  // This initializes the scales
  weightCellLeft.begin(pinDataWeightCellLeft, pinClockWeightCellLeft);
  weightCellRight.begin(pinDataWeightCellRight, pinClockWeightCellRight);

  // Calibrate to initially weighted value
  weightCellLeft.set_scale(calibrationWeightCellLeft);
  weightCellRight.set_scale(calibrationWeightCellRight);

  // set the scales to 0
  weightCellLeft.tare();
  weightCellRight.tare();

  /********************************************************
    movingaverage ini array
  ******************************************************/
  if (Brewdetection == 1)
  {
    for (int thisReading = 0; thisReading < numReadings; thisReading++)
    {
      readingstemp[thisReading] = 0;
      readingstime[thisReading] = 0;
      readingchangerate[thisReading] = 0;
    }
  }
  if (TempSensor == 2)
  {
    temperature = 0;
    Sensor1.getTemperature(&temperature);
    Input = Sensor1.calc_Celsius(&temperature);
  }

  //Initialisation MUST be at the very end of the init(), otherwise the time comparision in loop() will have a big offset
  unsigned long currentTime = millis();
  previousMillistemp = currentTime;
  //windowStartTime = currentTime;
  previousMillisDisplay = currentTime;
  previousMillisBlynk = currentTime;

  /********************************************************
    Timer ISR - Initialisierung
    TIM_DIV1 = 0,   //80MHz (80 ticks/us - 104857.588 us max)
    TIM_DIV16 = 1,  //5MHz (5 ticks/us - 1677721.4 us max)
    TIM_DIV256 = 3  //312.5Khz (1 tick = 3.2us - 26843542.4 us max)
  ******************************************************/
  timer = timerBegin(0, 256, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 6250, true);
  timerAlarmEnable(timer);
  /*
  timer1_attachInterrupt(onTimer1ISR);
  //timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  //timer1_write(50000); // set interrupt time to 10ms
  timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
  timer1_write(6250); // set interrupt time to 20ms
  */
  setupDone = true;
}

void loop()
{
  //Only do Wifi stuff, if Wifi is connected
  if (WiFi.status() == WL_CONNECTED && Offlinemodus == 0)
  {

    //MQTT
    if (MQTT == 1)
    {
      client.loop();
    }

    ArduinoOTA.handle(); // For OTA
    // Disable interrupt it OTA is starting, otherwise it will not work
    ArduinoOTA.onStart([]() {
      portENTER_CRITICAL_ISR(&timerMux);
      digitalWrite(pinRelayHeater, LOW); //Stop heating
    });
    ArduinoOTA.onError([](ota_error_t error) {
      portEXIT_CRITICAL_ISR(&timerMux);
    });
    // Enable interrupts if OTA is finished
    ArduinoOTA.onEnd([]() {
      portEXIT_CRITICAL_ISR(&timerMux);
    });

    if (Blynk.connected())
    { // If connected run as normal
      Blynk.run();
      blynkReCnctCount = 0; //reset blynk reconnects if connected
    }
    else
    {
      checkBlynk();
    }
    wifiReconnects = 0; //reset wifi reconnects if connected
  }
  else
  {
    checkWifi();
  }

  //MQTT
  if (MQTT == 1)
  {
    client.subscribe("/solltemp");
    client.onMessage(messageReceived);
  }

  refreshTemp();       //read new temperature values
  testEmergencyStop(); // test if Temp is to high
  brew();              //start brewing if button pressed

  sendToBlynk();

  //check if PID should run or not. If not, set to manuel and force output to zero
  if (pidON == 0 && pidMode == 1)
  {
    pidMode = 0;
    bPID.SetMode(pidMode);
    Output = 0;
  }
  else if (pidON == 1 && pidMode == 0 && !sensorError && !emergencyStop && backflushState == 10)
  {
    pidMode = 1;
    bPID.SetMode(pidMode);
  }

  //Sicherheitsabfrage
  if (!sensorError && Input > 0 && !emergencyStop && backflushState == 10 && (backflushON == 0 || brewcounter > 10))
  {
    brewdetection(); //if brew detected, set PID values
    printScreen();   // refresh display

    //Set PID if first start of machine detected
    if (Input < setPoint && kaltstart)
    {
      if (startTn != 0)
      {
        startKi = startKp / startTn;
      }
      else
      {
        startKi = 0;
      }
      bPID.SetTunings(startKp, startKi, 0, P_ON_M);
    }
    else if (timerBrewdetection == 0)
    { //Prevent overwriting of brewdetection values
      // calc ki, kd
      if (aggTn != 0)
      {
        aggKi = aggKp / aggTn;
      }
      else
      {
        aggKi = 0;
      }
      aggKd = aggTv * aggKp;
      bPID.SetTunings(aggKp, aggKi, aggKd, PonE);
      kaltstart = false;
    }

    if (millis() - timeBrewdetection < brewtimersoftware * 1000 && timerBrewdetection == 1)
    {
      // calc ki, kd
      if (aggbTn != 0)
      {
        aggbKi = aggbKp / aggbTn;
      }
      else
      {
        aggbKi = 0;
      }
      aggbKd = aggbTv * aggbKp;
      bPID.SetTunings(aggbKp, aggbKi, aggbKd);
      if (OnlyPID == 1)
      {
        bezugsZeit = millis() - timeBrewdetection;
      }
    }
  }
  else if (sensorError)
  {

    //Deactivate PID
    if (pidMode == 1)
    {
      pidMode = 0;
      bPID.SetMode(pidMode);
      Output = 0;
    }

    digitalWrite(pinRelayHeater, LOW); //Stop heating

    displayMessage("Error, Temp: ", String(Input), "Check Temp. Sensor!", "", "", ""); //DISPLAY AUSGABE
  }
  else if (emergencyStop)
  {

    //Deactivate PID
    if (pidMode == 1)
    {
      pidMode = 0;
      bPID.SetMode(pidMode);
      Output = 0;
    }

    digitalWrite(pinRelayHeater, LOW); //Stop heating

    displayEmergencyStop();
  }
  else if (backflushON || backflushState > 10)
  {
    if (backflushState == 43)
    {
      displayMessage("Backflush finished", "Please reset brewswitch...", "", "", "", "");
    }
    else if (backflushState == 10)
    {
      displayMessage("Backflush activated", "Please set brewswitch...", "", "", "", "");
    }
    else if (backflushState > 10)
    {
      displayMessage("Backflush running:", String(flushCycles), "from", String(maxflushCycles), "", "");
    }
  }
}
