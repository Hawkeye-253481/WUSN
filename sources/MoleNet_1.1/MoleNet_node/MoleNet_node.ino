#include <avr/io.h>
#include <avr/power.h>
#include <LowPower.h>      //from: https://github.com/LowPowerLab/LowPower
#include <RFM69.h>         //from: https://www.github.com/lowpowerlab/rfm69
#include <SDI12.h>
#include <avr/interrupt.h>
#include "definitions.h"

#include "Wire.h"
#include <PCF8523.h>


#include <SPI.h>
#include <SPIEEP.h>        //from: https://bitbucket.org/spirilis/spieep/

#define SENSOR_PWPIN    7         // Sensor Power
#define SENSOR_PIN      8         // Sensor data pin
#define RTC_IRQ_PIN     3         // The Pin ID
#define NODEID          1
#define EEPROM_CSPIN    9         // CS for EEPROM
#define CLOCKPRESC      clock_div_1 // reduce energy consumption, baudrate and

#define RADIOPOWERLEVEL  31        //31: send with full power

#define SENDMINUTE      0         // Send every full hour

#define UTCOFFSET       2         // Time offset while programming this sketch to set the RTC to UTC

// TODO:
// Use new flash lib?
// Use internal EEPROM (EEPROM.h) for config data


SDI12 mySDI12(SENSOR_PIN);

PCF8523 rtc;

SPIEEP eep(16, 128, 65535);

NodeData_v1 nodeData;

volatile boolean alarmTriggered = false;

RFM69 radio;

void setup() {
  clock_prescale_set(CLOCKPRESC); // reduce clock to save energy

  Serial.begin(115200);
  Wire.begin();
  rtc.begin();
  rtc.setTime(DateTime(F(__DATE__), F(__TIME__)) - TimeSpan(UTCOFFSET * 60 * 60));

  rtc.setBatterySwitchover();
  rtc.setTwelveTwentyFourHour(eTWENTYFOURHOUR);
  rtc.setAlarm(SENDMINUTE);
  rtc.enableAlarm(true);
  rtc.ackAlarm();


  init_radio();
  radio.sleep();

  eep.begin_spi(EEPROM_CSPIN);
  eep.sleepmode();

  pinMode(RTC_IRQ_PIN, INPUT);

  // Force one time sending after startup:
  alarmTriggered = true;

  Serial.println("### Setup done");
}

void loop() {
  detachInterrupt(digitalPinToInterrupt(RTC_IRQ_PIN));


  if (alarmTriggered) {
    Serial.println("Sending data");
    initData(&nodeData);
    getSensorData(&nodeData);
    getTime(&nodeData);

    sendData(&nodeData);
    printData_v1(&nodeData);

    //    alarm.resetInterrupt();
    alarmTriggered = false;

  }

  rtc.ackAlarm();

  //create an interupt for the rtc alarm
  attachInterrupt(digitalPinToInterrupt(RTC_IRQ_PIN), irqHandler, FALLING);

  // Go for deep sleep
  Serial.println("Going to sleep mode");
  Serial.flush();
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);

}



/**
   Init the data structure with some basic information
*/
void initData(NodeData_v1 *nd) {
  nd->version = 1;
  nd->sent = false;
  nd->rssi = 0;
  nd->sender_id = NODEID;
}

/**
   Get the current time and store it to the data structure
*/
void getTime(NodeData_v1 *nd) {
  DateTime now = rtc.readTime();
  nd->sec = now.second();
  nd->minute = now.minute();
  nd->hour = now.hour();
  nd->day = now.day();
  nd->month = now.month();
  nd->year = now.year();
  //rtc.get(&(nd->sec), &(nd->minute), &(nd->hour), &(nd->day), &(nd->month), &(nd->year));
}


/**
   Send the data structure to the gateway
*/
boolean sendData(NodeData_v1 *nd) {
  init_radio();
  Serial.print("Size of packet "); Serial.println(sizeof(*nd));
  nd->sent = radio.sendWithRetry(GATEWAYID, (byte*)nd, sizeof(*nd), (uint8_t)3, RFM69_ACK_TIMEOUT * 5);
  radio.sleep();
}

/**
   Read out sensor data and store it to the data structure
*/
void getSensorData(NodeData_v1 *nd) {
  mySDI12.begin();
  pinMode(SENSOR_PWPIN, OUTPUT);
  digitalWrite(SENSOR_PWPIN, HIGH);
  delay(300);


  mySDI12.sendCommand("?I!");
  delay(300);

  String sensorId;

  while (mySDI12.available()) {
    sensorId = mySDI12.readStringUntil('\n');
  }

  String newCommand(sensorId[0]);
  newCommand += "M!";
  mySDI12.sendCommand(newCommand); // 0MC! -> Data from sensor 0 including checksum, exclude C for no checksum
  delay(300);

  String startMeasurements = mySDI12.readStringUntil('\n'); // 00013 -> sensor 0, 001 -> 1sec waiting for measurement, 3 data values
  while (mySDI12.available())mySDI12.read();
  delay(startMeasurements.substring(1, 4).toInt() * 1000); // SleepTime
  int numMeasurements = startMeasurements.substring(4).toInt();   // Number of measurements

  newCommand = sensorId[0];
  newCommand += "D0!";

  mySDI12.sendCommand(newCommand); // 0D0! -> Data from sensor 0 including checksum
  delay(300);

  String datastring = mySDI12.readStringUntil('\n'); // Data string
  while (mySDI12.available())mySDI12.read();

  //Serial.println(datastring);

  // TODO: Change for sensors without conductivity, check with number of sensors variable
  nd->vwc = datastring.substring(getNIndexOf(datastring, '+', 0) + 1, getNIndexOf(datastring, '+', 1)).toFloat();
  nd->temp = datastring.substring(getNIndexOf(datastring, '+', 1) + 1, getNIndexOf(datastring, '+', 2)).toFloat();
  nd->cond = datastring.substring(getNIndexOf(datastring, '+', 2) + 1).toFloat();

  mySDI12.end();
  digitalWrite(SENSOR_PWPIN, LOW);
}

/**
   Initialize the radio and prepare for sending
*/
void init_radio()
{
  //initialize the radio with the desired frequency, nodeID and networkID
  radio.initialize(FREQUENCY, NODEID, NETWORKID);
  //set the transmission power to the maximum
  radio.setPowerLevel(RADIOPOWERLEVEL);
  //enable encryption for the communication
  radio.encrypt(ENCRYPTKEY);
  //radio.setFrequency(434000000); //set frequency to some custom frequency
  //didn't work in test
  delay(1);
}


/**
   Utility function: Find the Nth position of a given character
*/
int getNIndexOf(String s, char stopchar, int index) {
  int checkpos = -1;
  for (int i = 0; i <= index; i++) {
    checkpos = s.indexOf(stopchar, checkpos + 1);
    if (checkpos == -1) {
      return -1;
    }
  }
  return checkpos;
}

/**
   The IRQ handler for the wakeup interrupt by the RTC.
*/
void irqHandler()
{
  alarmTriggered = true;
}