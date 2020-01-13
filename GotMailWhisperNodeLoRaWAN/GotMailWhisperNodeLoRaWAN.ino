#define DEBUG
#include <T2WhisperNode.h>
#include <LowPower.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <RH_RF95.h>
#include "secrets.h"

const int nssPin = 10;
const int rstPin = 7;
const int dioPin = 2;

const int wakeUpPin = 3;
#define RADIO_TX_POWER 5

RH_RF95 myRadio;
T2Flash myFlash;

// Keys are defined in secrets.h
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

static byte mydata[2]; 
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 1;

// Pin mapping
const lmic_pinmap lmic_pins = {
  .nss = nssPin,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = rstPin,
  .dio = {dioPin, A2, LMIC_UNUSED_PIN},
};

void wakeUp() 
{
}

void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
      #ifdef DEBUG
        Serial.println("OP_TXRXPEND, not sending");
        #endif
    } else {
        // Prepare upstream data transmission at the next possible time.
        #ifdef DEBUG
        Serial.println("do_send");
        #endif
        uint16_t voltage = GetVoltage();
        mydata[0] = voltage >> 8;
        mydata[1] = voltage;
        #ifdef DEBUG
        Serial.print("Voltage: ");
        Serial.println((int)voltage);
        #endif

        myRadio.setTxPower(RADIO_TX_POWER);
        LMIC_setTxData2(1, mydata, sizeof(mydata), 0);
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {
  delay(1000);
  pinMode(wakeUpPin, INPUT_PULLUP);
  #ifdef DEBUG
  Serial.begin(115200);
  Serial.println("setup");
  Serial.flush();
  #endif
  
  DisableNonEssentials();
  
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();
  // Make LMiC initialize the default channels, choose a channel, and
  // schedule the OTAA join
  LMIC_startJoining();

  // Start job (sending automatically starts OTAA too)
  do_send(&sendjob);
}

void loop() 
{
  os_runloop_once();
}

// Put device to sleep until interrupt
void PowerDown()
{
  #ifdef DEBUG
  Serial.println("PowerDown");
  Serial.flush();
  #endif
  myRadio.sleep();  
  attachInterrupt(digitalPinToInterrupt(wakeUpPin), wakeUp, FALLING);
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
}

// Put device to sleep for a while to avoid multiple transmissions per usage of mail box
void GraceSleep()
{
  #ifdef DEBUG
  Serial.println("GraceSleep");
  Serial.flush();
  #endif
  myRadio.sleep();  
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
}

void PowerUp()
{
  detachInterrupt(digitalPinToInterrupt(wakeUpPin));
  #ifdef DEBUG
  Serial.println("PowerUp");
  Serial.flush();
  #endif
}

void DisableNonEssentials()
{
  myFlash.init(T2_WPN_FLASH_SPI_CS);
  myFlash.powerDown();
}

void onEvent (ev_t ev) {
   #ifdef DEBUG
   Serial.print(os_getTime());
   Serial.print(": ");
   #endif
    switch(ev) {
      case EV_JOINED:
          // Ignore the channels from the Join Accept
          // Disable link check validation (automatically enabled
          // during join, but not supported by TTN at this time).
          LMIC_setLinkCheckMode(0);
          #ifdef DEBUG
          Serial.println("EV_JOINED");
          Serial.flush();
          #endif
          break;
      case EV_TXCOMPLETE:
          #ifdef DEBUG
          Serial.println("EV_TXCOMPLETE");
          Serial.flush();
          #endif
          //Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
//            if (LMIC.txrxFlags & TXRX_ACK)
//              Serial.println(F("Received ack"));
//            if (LMIC.dataLen) {
//              Serial.println(F("Received "));
//              Serial.println(LMIC.dataLen);
//              Serial.println(F(" bytes of payload"));
//            }
          GraceSleep();
          PowerDown();
          PowerUp();
          os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
          break;
      #ifdef DEBUG
      case EV_SCAN_TIMEOUT:
          Serial.println(F("EV_SCAN_TIMEOUT"));
          break;
      case EV_BEACON_FOUND:
          Serial.println(F("EV_BEACON_FOUND"));
          break;
      case EV_BEACON_MISSED:
          Serial.println(F("EV_BEACON_MISSED"));
          break;
      case EV_BEACON_TRACKED:
          Serial.println(F("EV_BEACON_TRACKED"));
          break;
      case EV_JOINING:
          Serial.println(F("EV_JOINING"));
          break;
      case EV_RFU1:
          Serial.println(F("EV_RFU1"));
          break;
      case EV_JOIN_FAILED:
          Serial.println(F("EV_JOIN_FAILED"));
          break;
      case EV_REJOIN_FAILED:
          Serial.println(F("EV_REJOIN_FAILED"));
          break;
      case EV_LOST_TSYNC:
           Serial.println(F("EV_LOST_TSYNC"));
          break;
      case EV_RESET:
          Serial.println(F("EV_RESET"));
          break;
      case EV_RXCOMPLETE:
          // data received in ping slot
          Serial.println(F("EV_RXCOMPLETE"));
          break;
      case EV_LINK_DEAD:
          Serial.println(F("EV_LINK_DEAD"));
          break;
      case EV_LINK_ALIVE:
          Serial.println(F("EV_LINK_ALIVE"));
          break;
      #endif
      default:
          #ifdef DEBUG
          Serial.println("Unknown event");
          Serial.flush();
          #endif
          break;
    }
}

uint16_t GetVoltage()
{
  uint16_t voltage = 0;
  voltage = T2Utils::readVoltage(T2_WPN_VBAT_VOLTAGE, T2_WPN_VBAT_CONTROL);
  if (voltage == 0)
  {
    voltage = T2Utils::readVoltage(T2_WPN_VIN_VOLTAGE, T2_WPN_VIN_CONTROL);
  }
  return voltage;
}