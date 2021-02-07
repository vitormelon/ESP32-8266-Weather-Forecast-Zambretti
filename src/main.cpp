
#include <WiFiManager.h>
#include <Wire.h>
#include <time.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "FS.h"
#include <SPIFFS.h>
#include <LiquidCrystal_I2C.h>

#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10

#define PRESSURE_READINGS_LENGH 24


#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme;
WiFiManager wifiManager;

LiquidCrystal_I2C lcd(0x3F,20,4);

int altitude  = 479; 

unsigned long delayTime = 60000; // 1 minute
unsigned long lastMillis = delayTime; // this is to the code run in the initialization

float temperature = 0.0;
float pressure = 0.0;
float seaLevelPressure = 0.0;
float humidity = 0.0;
float deltaPressure = 0.0;
int pressureTrend = 0;
int zambretti = 0;
String forecast = "";

float pressureReading[PRESSURE_READINGS_LENGH];

int lastPressureReadTime = -1;
int readingsCount = 0;

char storageFileName [] = "/data.txt";


void getDataFromSensor() {
  bme.takeForcedMeasurement();
  delay(500);
  temperature = bme.readTemperature();
  pressure = (bme.readPressure()) / 100.0F; //from Pa to hPa
  seaLevelPressure = bme.seaLevelForAltitude(altitude, pressure);
  humidity = bme.readHumidity();
}

void initBmeSensor() {
    if (! bme.begin(0x76, &Wire)) {
        Serial.println("Could not find a valid BME280 sensor, check wiring!");
        while (1);
    }


    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, // temperature
                    Adafruit_BME280::SAMPLING_X1, // pressure
                    Adafruit_BME280::SAMPLING_X1, // humidity
                    Adafruit_BME280::FILTER_OFF   );
}

void initWifiManager() {
    WiFi.mode(WIFI_STA); 
    wifiManager.setConfigPortalBlocking(false);

    if(wifiManager.autoConnect("AutoConnectAP")){
        Serial.println("connected...yeey :)");
    }
    else {
        Serial.println("Configportal running");
    }
}

bool istTimeToRun() {
    unsigned long millisNow = millis();
    return ((millisNow < lastMillis) || (millisNow - lastMillis >= delayTime));
}

void setupTime(){
    delay(1000);
    configTime(-3 * 3600, 0, "a.st1.ntp.br", "b.st1.ntp.br", "time.nist.gov");
    delay(1000);
}

tm getDateTime(){
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
  }
  return timeinfo;
}

int getMonth(tm timeinfo) {
  char output[30];
  strftime(output, 30, "%m", &timeinfo);
  String mon = output;
  return mon.toInt();  
}

int getHour(tm time) {
    char output[30];
    strftime(output, 30, "%H", &time);
    String hour = output;
    return hour.toInt(); 
}

int getMinute(tm time) {
    char output[30];
    strftime(output, 30, "%M", &time);
    String minute = output;
    return minute.toInt(); 
}

void shiftPressureReadArray()
{
    for (int i = 0; i < PRESSURE_READINGS_LENGH - 1; i++) {
        pressureReading[i] = pressureReading[i + 1];
    }
}

void updatePressureReading(tm time) {
    int now = getHour(time);
    if (lastPressureReadTime != now) {
      shiftPressureReadArray();
      pressureReading[23] = seaLevelPressure;
      lastPressureReadTime = now;
      readingsCount++;
    }
}

void saveVariablesToSPIFFS() {
  File myDataFile = SPIFFS.open(storageFileName, "w"); 
  if (!myDataFile) {
    Serial.println("Failed to open file");
  }
  myDataFile.println(readingsCount);
  myDataFile.println(lastPressureReadTime);
  Serial.println("Reading hour="+String(lastPressureReadTime));
  for (int i = 0; i <= PRESSURE_READINGS_LENGH - 1; i++) {
    myDataFile.println(pressureReading[i]);
  }
  myDataFile.close();

  Serial.println("Now reading back after write");
  myDataFile = SPIFFS.open(storageFileName, "r");  
  while (myDataFile.available()) {
    Serial.print("R="); Serial.println(myDataFile.readStringUntil('\n'));
  }
  myDataFile.close();
}

void readVariablesFromPIFFS() {
  File myDataFile = SPIFFS.open(storageFileName, "r"); 
  if (!myDataFile) {
    Serial.println("Failed to open file");
  }
  String temp_data;
  readingsCount = (myDataFile.readStringUntil('\n')).toInt();
  lastPressureReadTime = (myDataFile.readStringUntil('\n')).toInt();
  for (int i = 0; i <= PRESSURE_READINGS_LENGH - 1; i++) {
    temp_data = myDataFile.readStringUntil('\n');
    pressureReading[i] = temp_data.toFloat();
  }
  myDataFile.close();
}

void initializePressureReadings() {
    for (int i = 0; i <= PRESSURE_READINGS_LENGH - 1; i++) {
        pressureReading[i] = seaLevelPressure;
    }
}

float getHistoricalDeltaPressureByPeriod(int periodLenght) {
    int lastReadIdx = PRESSURE_READINGS_LENGH -1;
    int comparationIdx = lastReadIdx - periodLenght;
    return (pressureReading[lastReadIdx] - pressureReading[comparationIdx])/periodLenght;
}

float getImediateDeltaPressureByPeriod(int periodLenght, tm now) {
    int minutesElapsed = getMinute(now);
    int comparationIdx = PRESSURE_READINGS_LENGH - 1 - periodLenght;
    float timeElapsedInHous = periodLenght + (float)((float)minutesElapsed/60);
    return (seaLevelPressure - pressureReading[comparationIdx]) /timeElapsedInHous;
}

bool isRising(float deltaPressure) {
    return deltaPressure > 0.53;
}

bool isFalling(float deltaPressure) {
    return deltaPressure < -0.53;
}

int getDeltaPressureTrend(float deltaPressure)
{
    if(isRising(deltaPressure)) {
        return 2;
    }
    if(isFalling(deltaPressure)) {
        return 0;
    }
    return 1; //steady
}

int zambrettiForecast(unsigned int pressure, int pressureTrend){
  
//  fall    Z = 130-10P/81  
//  steady  Z = 147-50P/376 
//  rise    Z = 179-20P/129 
  
  int result = 0;

  switch(pressureTrend)
  {
    case 0:   // pressure falling
      result = 130 - 10*pressure/81;
      break;
    case 1:   // pressure steady
      result = 147 - 50*pressure/376;
      break;
    case 2:   // pressure rising
      result = 179 - 20*pressure/129 ;
      break;
  }

  result = constrain(result, 1, 32);
  return result;
}

int zambrettiForecast2(unsigned int pressure, int pressureTrend){  
  int result = 0;

  switch(pressureTrend)
  {
    case 0:   // pressure falling
      result = 127 - (0.12 * pressure);
      break;
    case 1:   // pressure steady
      result = 144 - (0.13 * pressure);
      break;
    case 2:   // pressure rising
      result = 185 - (0.16 * pressure) ;
      break;
  }

  result = constrain(result, 1, 32);
  return result;
}

String getForecastText(int zambretti) {
    switch(zambretti)   
    {
    case 1:
        return "Settled Fine";
    case 2:
        return "Fine Weather";
    case 3:
        return "Fine. Becoming less settled";
    case 4:
        return "Mostly fine. Showers developing later";
    case 5:
        return "Showers. Becoming more unsettled";
    case 6:
        return "Unsettled, Rain later";
    case 7:
        return "Rain at times, worse later";
    case 8:
        return "Rain at times, becoming very unsettled";
    case 9:
        return "Very Unsettled, Rain";
    case 10:
        return "Settled Fine";
    case 11:
        return "Fine Weather";
    case 12:
        return "Fine, Possibly showers";
    case 13:
        return "Fairly Fine , Showers likely";
    case 14:
        return "Showery Bright Intervals";
    case 15:
        return "Changeable some rain";
    case 16:
        return "Unsettled, rain at times";
    case 17:
        return "Rain at Frequent Intervals";
    case 18:
        return "Very Unsettled, Rain";
    case 19:
        return "Stormy, much rain";
    case 20:
        return "Settled Fine";
    case 21:
        return "Fine Weather";
    case 22:
        return "Becoming Fine";
    case 23:
        return "Fairly Fine, Improving";
    case 24:
        return "Fairly Fine, Possibly showers, early";
    case 25:
        return "Showery Early, Improving";
    case 26:
        return "Changeable Mendin";
    case 27:
        return "Rather Unsettled Clearing Later";
    case 28:
        return "Unsettled, Probably Improving";
    case 29:
        return "Unsettled, short fine Interval";
    case 30:
        return "Very Unsettled, Finer at times";
    case 31:
        return "Stormy, possibly improving";
    case 32:
        return "Stormy, much rain";
    default:
        return "Unknow";
    }
    
}

void processData(tm now) {
    deltaPressure = getImediateDeltaPressureByPeriod(3, now);
    pressureTrend = getDeltaPressureTrend(deltaPressure);
    zambretti = zambrettiForecast(seaLevelPressure, pressureTrend);
    forecast = getForecastText(zambretti);
}

void serialPrintValues(tm now) {
    Serial.println();

    Serial.print("Time = ");
    Serial.println(&now, "%H:%M:%S");

    Serial.print("Month = ");
    Serial.println(getMonth(now));

    Serial.print("Last Read Time = ");
    Serial.println(lastPressureReadTime);

    Serial.print("Readings Count = ");
    Serial.println(readingsCount);

    Serial.print("Temperature = ");
    Serial.print(temperature);
    Serial.println(" *C");

    Serial.print("Humidity = ");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.print("Pressure = ");
    Serial.print(pressure);
    Serial.println(" hPa");

    Serial.print("Sea Level Pressure = ");
    Serial.print(seaLevelPressure);
    Serial.println(" hPa");

    Serial.print("3h Delta = ");
    Serial.print(getHistoricalDeltaPressureByPeriod(3));
    Serial.println(" hPa/h");

    Serial.print("1h Delta = ");
    Serial.print(getHistoricalDeltaPressureByPeriod(1));
    Serial.println(" hPa/h");

    Serial.print("3h Imediat Delta = ");
    Serial.print(getImediateDeltaPressureByPeriod(3, now));
    Serial.println(" hPa/h");

    Serial.print("1h Imediat Delta = ");
    Serial.print(getImediateDeltaPressureByPeriod(1, now));
    Serial.println(" hPa/h");

    Serial.print("Pressure Trend = ");
    Serial.println(pressureTrend);

    Serial.print("Zambretti = ");
    Serial.println(zambretti);
    
    Serial.print("Zambretti2 = ");
    Serial.println(zambrettiForecast2(seaLevelPressure, pressureTrend));

    Serial.print("Forecast = ");
    Serial.println(forecast);
    
    Serial.println();
}

void printForecast() {
    lcd.setCursor(0, 2);
    for(int i = 0; i < forecast.length(); i++) {
        if (i == 20) {
            lcd.setCursor(0, 3);
        }
        lcd.print(forecast[i]);
    }
}

void printToLcd(tm now) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(&now, "%d/%m %H:%M");
    lcd.setCursor(13, 0);
    lcd.print(deltaPressure, 2);
    lcd.print("/h");
    lcd.setCursor(0, 1);
    lcd.print(temperature, 1);
    lcd.print("C  ");
    lcd.setCursor(6, 1);
    lcd.print(round(humidity),0);
    lcd.print("%");
    lcd.setCursor(10, 1);
    lcd.print(seaLevelPressure, 2);
    lcd.print("hpa");
    printForecast();
}

void setup() {
    Serial.begin(9600);
    initWifiManager();
    initBmeSensor();
    setupTime();
    initializePressureReadings();
    lcd.init();
    lcd.begin(20, 4);
    lcd.backlight();

    if (!SPIFFS.begin(true)) { 
        Serial.println("SPIFFS Mount Failed"); 
        while(1);
  }

    readVariablesFromPIFFS();
    Serial.println();
}


void loop() {    
    wifiManager.process();
    if(istTimeToRun()){
        tm now = getDateTime();
        getDataFromSensor();
        updatePressureReading(now);
        processData(now);
        saveVariablesToSPIFFS();
        printToLcd(now);
        serialPrintValues(now);
        lastMillis = millis();
    }
}