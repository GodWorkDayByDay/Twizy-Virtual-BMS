/**
 * ==========================================================================
 * Twizy Virtual BMS
 * ==========================================================================
 * 
 * Emulation of Renault Twizy BMS (battery management system)
 * Interface between the Twizy and custom built / third party batteries
 * 
 * Author: Michael Balzer <dexter@dexters-web.de>
 * 
 * Twizy CAN object dictionary:
 * https://docs.google.com/spreadsheets/d/1gOrG9rnGR9YuMGakAbl4s97a6irHF6UNFV1TS5Ll7MY/edit#gid=0
 * (Maintainer: Michael Balzer <dexter@dexters-web.de>)
 * 
 * Twizy BMS CAN & hardware protocol decoding and reengineering has been done 
 * by a joint effort of (in reverse alphabetical order):
 *  - Lutz Schäfer <aquillo@t-online.de>
 *  - Pascal Ripp <pascal@ripp.li>
 *  - Bernd Eickhoff <b.eickhoff@gmx.de>
 *  - Michael Balzer <dexter@dexters-web.de>
 * 
 * Libraries used:
 *  - MCP_CAN: https://github.com/coryjfowler/MCP_CAN_lib
 *  - TimerOne: https://github.com/PaulStoffregen/TimerOne
 * 
 * Licenses:
 *  This is free software and information under the following licenses:
 *  - Source code: GNU Lesser General Public License (LGPL)
 *    https://www.gnu.org/licenses/lgpl.html
 *  - Documentation: GNU Free Documentation License (FDL)
 *    https://www.gnu.org/licenses/fdl.html
 * 
 */

#ifndef _TwizyVirtualBMS_h
#define _TwizyVirtualBMS_h

#include <Arduino.h>
#include <avr/pgmspace.h>

#include <mcp_can.h>
#include <mcp_can_dfs.h>
#include <TimerOne.h>

#ifndef _TwizyVirtualBMS_config_h
#warning "Fallback to default TwizyVirtualBMS_config.h -- you should copy this into your sketch folder"
#include "TwizyVirtualBMS_config.h"
#endif

#define TWIZY_VBMS_VERSION		"V1.0.0 (2017-06-17)"

#ifndef TWIZY_TAG
#define TWIZY_TAG							"twizy."
#endif


// ==========================================================================
// TYPES AND CONSTANTS
// ==========================================================================


// Twizy states:
enum TwizyState {
	Off,
	Init,
	Error,
	Ready,
	StartDrive,
	Driving,
	StopDrive,
	StartCharge,
	Charging,
	StopCharge,
	StartTrickle,
	Trickle,
	StopTrickle
};

// Twizy state names:
#if TWIZY_DEBUG_LEVEL >= 1
const char PROGMEM twizyStateName[13][13] = {
	"Off",
	"Init",
	"Error",
	"Ready",
	"StartDrive",
	"Driving",
	"StopDrive",
	"StartCharge",
	"Charging",
	"StopCharge",
	"StartTrickle",
	"Trickle",
	"StopTrickle"
};
#endif


// Known error codes for setError():
// Note: these can be used singularly or be ORed to set multiple indicators.
// i.e. do setError(TWIZY_SERV_TEMP|TWIZY_SERV_STOP) to indicate
// a severely high temperature
#define TWIZY_OK            0x00000000    // clear all indicators
#define TWIZY_SERV          0x00eeee00    // set SERV indicator
#define TWIZY_SERV_12V      0x00eeee20    // set SERV + 12V battery indicator
#define TWIZY_SERV_BATT     0x00eeee40    // set SERV + main battery indicator
#define TWIZY_SERV_TEMP     0x00eeee80    // set SERV + temperature indicator
#define TWIZY_SERV_STOP     0x00eeff00    // set SERV + STOP indicator + beep


// User callbacks hooks:
typedef void (*TwizyEnterStateCallback)(TwizyState currentState, TwizyState newState);
typedef bool (*TwizyCheckStateCallback)(TwizyState currentState, TwizyState newState);
typedef void (*TwizyTickerCallback)(unsigned int clockCnt);
typedef void (*TwizyProcessCanMsgCallback)(unsigned long rxId, byte rxLen, byte *rxBuf);


// ==========================================================================
// LIBRARY CODE AREA
// ==========================================================================

// PROGMEM string helpers:
#define FLASHSTRING 	const __FlashStringHelper
#define FS(x) 				(__FlashStringHelper*)(x)

// Parameter boundary check with error output:
#define CHECKLIMIT(var, minval, maxval) \
if (var < minval || var > maxval) { \
	Serial.print(TWIZY_TAG); \
	Serial.print(__func__); \
	Serial.print(F(": ERROR " #var "=")); \
	Serial.print(var); \
	Serial.println(F(" out of bounds [" #minval "," #maxval "]")); \
	return false; \
}


// -----------------------------------------------------
// Interrupt handlers
// 

volatile bool twizyCanMsgReceived = false;

#ifdef TWIZY_CAN_IRQ_PIN
void twizyCanISR() {
	twizyCanMsgReceived = true;
}
#endif

volatile bool twizyClockTick = false;

void twizyClockISR() {
	twizyClockTick = true;
}



// -----------------------------------------------------
// TwizyVirtualBMS class definition
// 

class TwizyVirtualBMS {

public:
	
	// -----------------------------------------------------
	// Public API
	// 
	
	TwizyVirtualBMS();
	
	void begin();		// to be called in setup()
	void looper();	// to be called in loop()
	
	// User callback registration:
	void attachEnterState(TwizyEnterStateCallback fn);
	void attachCheckState(TwizyCheckStateCallback fn);
	void attachTicker(TwizyTickerCallback fn);
	void attachProcessCanMsg(TwizyProcessCanMsgCallback fn);
	
	// Model access:
	bool setChargeCurrent(int amps);
	bool setCurrent(float amps);
	bool setSOC(float soc);
	bool setPowerLimits(unsigned int drive, unsigned int recup);
	bool setSOH(int soh);
	bool setCellVoltage(int cell, float volt);
	bool setVoltage(float volt, bool deriveCells);
	bool setModuleTemperature(int module, int temp);
	bool setTemperature(int tempMin, int tempMax, bool deriveModules);
	bool setError(unsigned long error);
	
	// State access:
	TwizyState state() {
		return twizyState;
	}
	bool inState(TwizyState state1) {
    return (twizyState==state1);
  }
  bool inState(TwizyState state1, TwizyState state2) {
    return (twizyState==state1 || twizyState==state2);
  }
  bool inState(TwizyState state1, TwizyState state2, TwizyState state3) {
    return (twizyState==state1 || twizyState==state2 || twizyState==state3);
  }
  bool inState(TwizyState state1, TwizyState state2, TwizyState state3, TwizyState state4) {
    return (twizyState==state1 || twizyState==state2 || twizyState==state3 || twizyState==state4);
  }
  bool inState(TwizyState state1, TwizyState state2, TwizyState state3, TwizyState state4, TwizyState state5) {
    return (twizyState==state1 || twizyState==state2 || twizyState==state3 || twizyState==state4 || twizyState==state5);
  }
  void enterState(TwizyState newState);
	
	// CAN interface access:
	bool sendMsg(INT32U id, INT8U len, INT8U *buf);
	void setCanFilter(byte filterNum, unsigned int canId);

	// Debug utils:
	void dumpId(FLASHSTRING *name, int len, byte *buf);
	void debugInfo();
	

private:

	// -----------------------------------------------------
	// Twizy CAN model
	// 
	
	// BMS (controlled by us):
	byte id155[8] = { 0x07, 0x97, 0xCA, 0x54, 0x52, 0x30, 0x00, 0x6C };
	byte id424[8] = { 0x11, 0x40, 0x10, 0x20, 0x39, 0x63, 0x00, 0x3A };
	byte id425[8] = { 0x2A, 0x1F, 0x44, 0xFF, 0xFE, 0x42, 0x01, 0x20 };
	byte id554[8] = { 0x3A, 0x3A, 0x3A, 0x3A, 0x3A, 0x3A, 0x3A, 0x00 };
	byte id556[8] = { 0x30, 0x93, 0x09, 0x30, 0x93, 0x09, 0x30, 0x9A };
	byte id557[8] = { 0x30, 0x93, 0x09, 0x30, 0x93, 0x09, 0x30, 0x90 };
	byte id55E[8] = { 0x30, 0x93, 0x09, 0x30, 0x93, 0x09, 0x0C, 0xF9 };
	byte id55F[8] = { 0xFF, 0xFF, 0x73, 0x00, 0x00, 0x21, 0xF2, 0x1F };
	byte id628[3] = { 0x00, 0x00, 0x00 };
	byte id659[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
	
	// CHARGER:
	byte id423[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	byte id597[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	
	// DISPLAY:
	byte id599[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	
	
	// -----------------------------------------------------
	// Twizy CAN interface
	// 

	MCP_CAN twizyCAN;

	unsigned int sendErrors = 0;
	unsigned int sendRetries = 0;
	
	// RX buffer:
	unsigned long rxId;
	byte rxLen;
	byte rxBuf[8];
	
	void receiveCanMsgs();
	void process423();
	void process597();
	void process599();
	
	
	// -----------------------------------------------------
	// Twizy state machine
	// 
	
	TwizyState twizyState = (TwizyState) -1;
	
	// 3MW pulse tick counter:
	byte counter3MW = 0;
	
	
	// -----------------------------------------------------
	// Twizy ticker:
	//
	
	unsigned int clockCnt = 0;
	
	void ticker();
	
	
	// -----------------------------------------------------
	// User callbacks hooks
	// 
	
	TwizyEnterStateCallback 			bmsEnterState = NULL;
	TwizyCheckStateCallback 			bmsCheckState = NULL;
	TwizyTickerCallback 					bmsTicker = NULL;
	TwizyProcessCanMsgCallback 		bmsProcessCanMsg = NULL;
	
	
};


// -----------------------------------------------------
// API
// 

TwizyVirtualBMS::TwizyVirtualBMS()
	: twizyCAN(TWIZY_CAN_CS_PIN) {
}

void TwizyVirtualBMS::attachEnterState(TwizyEnterStateCallback fn) {
	bmsEnterState = fn;
}
void TwizyVirtualBMS::attachCheckState(TwizyCheckStateCallback fn) {
	bmsCheckState = fn;
}
void TwizyVirtualBMS::attachTicker(TwizyTickerCallback fn) {
	bmsTicker = fn;
}
void TwizyVirtualBMS::attachProcessCanMsg(TwizyProcessCanMsgCallback fn) {
	bmsProcessCanMsg = fn;
}


// -----------------------------------------------------
// Twizy CAN model
// 


// Set battery charge current level
//  amps: 0 .. 35 [A] (5 A resolution, 35 not reached with current charger generation)
// Note: will enter state StopCharge if set to 0 during charge
bool TwizyVirtualBMS::setChargeCurrent(int amps) {
  CHECKLIMIT(amps, 0, 35);
  id155[0] = amps / 5;
  if (twizyState == Charging && id155[0] == 0) {
    enterState(StopCharge);
  }
  return true;
}

// Set battery pack current level
//  amps: -500 .. +500 (positive = charge, negative = discharge)
bool TwizyVirtualBMS::setCurrent(float amps) {
  CHECKLIMIT(amps, -500.0, 500.0);
  unsigned int level = 2000 + (amps * 4);
  id155[1] = (id155[1] & 0xf0) | ((level & 0x0f00) >> 8);
  id155[2] = level & 0x00ff;
  return true;
}

// Set battery pack SOC
//  soc: 0.00 .. 100.00
// Note: the charger will not start charging at SOC=100%
bool TwizyVirtualBMS::setSOC(float soc) {
  CHECKLIMIT(soc, 0.0, 100.0);
  unsigned int level = soc * 400;
  id155[4] = level >> 8;
  id155[5] = level & 0x00ff;
  return true;
}

// Set SEVCON power limits
//  drive: 0 .. 30000 [W]
//  recup: 0 .. 30000 [W]
// Note: both limits have a resolution of 500 W
bool TwizyVirtualBMS::setPowerLimits(unsigned int drive, unsigned int recup) {
  CHECKLIMIT(drive, 0, 30000);
  CHECKLIMIT(recup, 0, 30000);
  id424[2] = recup / 500;
  id424[3] = drive / 500;
  return true;
}

// Set battery pack SOH
//  soh: 0 .. 100
bool TwizyVirtualBMS::setSOH(int soh) {
  CHECKLIMIT(soh, 0, 100);
  id424[5] = soh;
  return true;
}

// Set battery cell voltage level
//  cell: 1 .. 14
//  volt: 1.0 .. 5.0
bool TwizyVirtualBMS::setCellVoltage(int cell, float volt) {
  CHECKLIMIT(cell, 1, 14);
  CHECKLIMIT(volt, 1.0, 5.0);
  
  // cell voltages are packed 12 bit values
  // determine frame and position:
  
  byte *frame;
  
  if (cell <= 5) {
    frame = id556;
    cell -= 1;
  }
  else if (cell <= 10) {
    frame = id557;
    cell -= 6;
  }
  else {
    frame = id55E;
    cell -= 11;
  }
  
  int pos = cell * 1.5;
  unsigned int level = volt * 200;
  
  if (cell & 1) {
    // odd cell number: pack right
    frame[pos]   = (frame[pos] & 0xf0) | (level >> 8);
    frame[pos+1] = level & 0x00ff;
  }
  else {
    // even cell number: pack left
    frame[pos]   = level >> 4;
    frame[pos+1] = (frame[pos+1] & 0x0f) | ((level << 4) & 0xf0);
  }
  
  return true;
}

// Set battery pack voltage level
//  volt: 19.3 … 69.6 (SEVCON G48 series voltage range)
//  deriveCells: true = set all cell voltages to volt/14
bool TwizyVirtualBMS::setVoltage(float volt, bool deriveCells) {
  CHECKLIMIT(volt, 19.3, 69.6);
  
  unsigned long level;
  
  // frame 55F: volt * 10, packed 2x:
  level = volt * 10;
  level |= level << 12;
  id55F[5] = (level & 0x00ff0000) >> 16;
  id55F[6] = (level & 0x0000ff00) >> 8;
  id55F[7] = (level & 0x000000ff);
  
  // frame 425: voltage scaling not 100% known yet, do approximation:
  level = (volt - 13.051) * 6.981;
  id425[6] = level >> 8;
  id425[7] = level & 0x00ff;
  level = 0xfc00 | (level << 1);
  id425[4] = level >> 8;
  id425[5] = level & 0x00ff;
  
  if (deriveCells) {
    float cellVolt = volt / 14.0;
    for (int i=1; i<=14; i++) {
      setCellVoltage(i, cellVolt);
    }
  }
  
  return true;
}

// Set battery module temperature
//  module: 1 .. 7
//  temp: -40 .. 100 [°C]
bool TwizyVirtualBMS::setModuleTemperature(int module, int temp) {
  CHECKLIMIT(module, 1, 7);
  CHECKLIMIT(temp, -40, 100);
  id554[module-1] = 40 + temp;
  return true;
}

// Set battery overall temperature
//  tempMin: -40 .. 100 [°C]
//  tempMax: -40 .. 100 [°C]
//  deriveModules: true = set all cell temperatures to avg(min,max)
bool TwizyVirtualBMS::setTemperature(int tempMin, int tempMax, bool deriveModules) {
  CHECKLIMIT(tempMin, -40, 100);
  CHECKLIMIT(tempMax, -40, 100);
  
  // set min/max temperatures:
  id424[4] = 40 + tempMin;
  id424[7] = 40 + tempMax;
  
  // derive module temperatures:
  if (deriveModules) {
    int tempAvg = (tempMin + tempMax) / 2;
    for (int i=0; i<7; i++) {
      id554[i] = 40 + tempAvg;
    }
  }
  
  return true;
}

// Set display battery error indicators
//  error: 0x000000 .. 0xFFFFFF  (0 = no error)
// See error code definitions.
bool TwizyVirtualBMS::setError(unsigned long error) {
  CHECKLIMIT(error, 0x000000, 0xFFFFFF);
  id628[0] = (error & 0xFF0000) >> 16;
  id628[1] = (error & 0x00FF00) >> 8;
  id628[2] = (error & 0x0000FF);
  return true;
}



// -----------------------------------------------------
// Twizy CAN interface
// 


// Set free CAN filters:
//  filterNum: 1…3
//  canId: 11 bit CAN ID i.e. 0x196
void TwizyVirtualBMS::setCanFilter(byte filterNum, unsigned int canId) {
  CHECKLIMIT(filterNum, 1, 3);
  twizyCAN.init_Filt(2+filterNum, 0, (unsigned long)canId << 16);
}


#define SAVEMSG(dst) for(int i = 0; i<rxLen; i++) { dst[i] = rxBuf[i]; }

// Read and process Twizy CAN messages:
void TwizyVirtualBMS::receiveCanMsgs() {
  while (twizyCAN.readMsgBuf(&rxId, &rxLen, rxBuf) == CAN_OK) {
    
    if (rxId == 0x423) {
      SAVEMSG(id423);
      process423();
    }
    else if (rxId == 0x597) {
      SAVEMSG(id597);
      process597();
    }
    else if (rxId == 0x599) {
      SAVEMSG(id599);
      process599();
    }
    
    // User space callback:
    if (bmsProcessCanMsg) {
			(*bmsProcessCanMsg)(rxId, rxLen, rxBuf);
		}
  }
}


// Process CHARGER frame 423:
void TwizyVirtualBMS::process423() {
  if (twizyState == Off && id423[0] != 0) {
    enterState(Init);
  }
  else if (twizyState != Off && id423[0] == 0) {
    enterState(Off);
  }
}


// Process CHARGER frame 597:
void TwizyVirtualBMS::process597() {
  byte address = id597[1] & 0xE5;
  byte chgmode = id597[3] & 0xF0;
  
  if (address != 0xE4) {
    return; // charger does not talk to us
  }
  
  if (chgmode == 0xC0) {
    if (twizyState != Driving) {
      enterState(StartDrive);
    }
  }
  else if (chgmode == 0xB0) {
    if (twizyState != Charging) {
      enterState(StartCharge);
    }
  }
  else if (chgmode == 0x90) {
    if (twizyState != Trickle) {
      enterState(StartTrickle);
    }
  }
  else if (chgmode == 0xD0) {
    if (twizyState == Driving) {
      enterState(StopDrive);
    }
    else if (twizyState == Charging) {
      enterState(StopCharge);
    }
    else if (twizyState == Trickle) {
      enterState(StopTrickle);
    }
  }
}


// Process DISPLAY frame 599:
void TwizyVirtualBMS::process599() {
  // copy odometer to BMS frame 55E
	unsigned long odo = ((unsigned long)id599[0] << 24)
										| ((unsigned long)id599[1] << 16)
										| ((unsigned long)id599[2] << 8)
										| (id599[3]);
  odo /= 10;
  id55E[6] = odo >> 8;
  id55E[7] = odo & 0x00ff;
}


// -----------------------------------------------------
// Send Twizy CAN messages:
//

bool TwizyVirtualBMS::sendMsg(INT32U id, INT8U len, INT8U *buf) {
  
  #if TWIZY_CAN_SEND == 1
  
  for (int tries=3; tries>0; tries--) {
    if (twizyCAN.sendMsgBuf(id, 0, len, buf) != CAN_GETTXBFTIMEOUT) {
      // CAN_OK = frame has been sent
      // CAN_SENDMSGTIMEOUT = we made it into a send buffer
      //  → no need to repeat the send:
      return true;
    }
    sendRetries++;
  }
  
  #endif //TWIZY_CAN_SEND
  
	sendErrors++;
	return false;

  // Note: MCP_CAN.sendMsgBuf() is not optimized for throughput.
  // Despite having three send buffers in the MCP, it will wait
  // for the send to finish and return an error on timeout.
  // The timeout is 50 MCP register reads via SPI.
  // It could be worth replacing MCP_CAN.sendMsgBuf() to utilize
  // asynchronous writes.
}


// -----------------------------------------------------
// Twizy ticker:
//

void TwizyVirtualBMS::ticker() {
  
  if (++clockCnt == 3000)
    clockCnt = 0;
  
  // Note: currently we turn off all CAN sends in state Error,
  //  as that reliably lets the SEVCON and charger switch off.
  //  It may be an option to only turn off ID 554 (or all 55x)
  //  instead, as that happened on one CAN trace of a defective
  //  original battery.
  
  if ((twizyState != Off) && (twizyState != Error)) {
    
    bool ms100 = (clockCnt % 10 == 0);
    bool ms1000 = (clockCnt % 100 == 0);
    bool ms3000 = (clockCnt % 300 == 0);
    bool ms10000 = (clockCnt % 1000 == 0);
    
    
    //
    // Send CAN messages
    //
    
    sendMsg(0x155, sizeof(id155), id155);
    
    if (ms100) {
      sendMsg(0x424, sizeof(id424), id424);
      sendMsg(0x425, sizeof(id425), id425);
    }
    if (ms1000) {
      sendMsg(0x554, sizeof(id554), id554);
    }
    if (ms100) {
      sendMsg(0x556, sizeof(id556), id556);
    }
    if (ms1000) {
      sendMsg(0x557, sizeof(id557), id557);
      sendMsg(0x55E, sizeof(id55E), id55E);
      sendMsg(0x55F, sizeof(id55F), id55F);
    }
    if (ms100) {
      sendMsg(0x628, sizeof(id628), id628);
    }
    if (ms3000) {
      sendMsg(0x659, sizeof(id659), id659);
    }
    
    
    //
    // Create 3MW pulse cycle
    // (high 150ms → low 150ms → high)
    //
    
    if (counter3MW > 0) {
      --counter3MW;
      // 3MW low after 150 ms:
      if (counter3MW == 15) {
        digitalWrite(TWIZY_3MW_CONTROL_PIN, 0);
      }
      // 3MW high after 300 ms (pulse finished):
      else if (counter3MW == 0) {
        digitalWrite(TWIZY_3MW_CONTROL_PIN, 1);
      }
    }
    
    
    //
    // Check for state transition
    //
    
    switch (twizyState) {
      
      // Transition to Ready?
      case Init:
      case StopDrive:
      case StopCharge:
      case StopTrickle:
        if (!bmsCheckState || (*bmsCheckState)(twizyState, Ready)) {
          enterState(Ready);
        }
        break;
        
      // Transition to Driving?
      case StartDrive:
        if (!bmsCheckState || (*bmsCheckState)(twizyState, Driving)) {
          enterState(Driving);
        }
        break;
        
      // Transition to Charging?
      case StartCharge:
        if (!bmsCheckState || (*bmsCheckState)(twizyState, Charging)) {
          enterState(Charging);
        }
        break;
        
      // Transition to Trickle?
      case StartTrickle:
        if (!bmsCheckState || (*bmsCheckState)(twizyState, Trickle)) {
          enterState(Trickle);
        }
        break;
        
    }
    
    
    //
    // Debug info every 10 seconds
    //
    
    #if TWIZY_DEBUG_LEVEL >= 1
    if (ms10000) {
      debugInfo();
    }
    #endif
    
  } // if ((twizyState != Off) && (twizyState != Error))
  
  
  //
  // Callback for BMS ticker code:
  //
  
  if (bmsTicker) {
		(*bmsTicker)(clockCnt);
	}
}


// -----------------------------------------------------
// Twizy debug info:
//

void TwizyVirtualBMS::dumpId(FLASHSTRING *name, int len, byte *buf) {
  Serial.print(F("- "));
  Serial.print(name);
  Serial.print(F(":"));
  for (int i=0; i<len; i++) {
    Serial.print(buf[i] < 0x10 ? F(" 0") : F(" "));
    Serial.print(buf[i], HEX);
  }
  Serial.println();
}

void TwizyVirtualBMS::debugInfo() {

  #if TWIZY_DEBUG_LEVEL >= 1

	Serial.println(F("\n" TWIZY_TAG "debugInfo:"));
  
  Serial.print(F("- twizyState="));
  Serial.println(FS(twizyStateName[twizyState]));
  
  Serial.print(F("- clockCnt="));
  Serial.println(clockCnt);
  
  if (sendRetries) {
    Serial.print(F("- sendRetries="));
    Serial.println(sendRetries);
    sendRetries = 0;
  }
  if (sendErrors) {
    Serial.print(F("- sendErrors="));
    Serial.println(sendErrors);
    sendErrors = 0;
  }

  #endif

  #if TWIZY_DEBUG_LEVEL >= 2
  
  // CHARGER & DISPLAY:
  dumpId(F("id423"), sizeof(id423), id423);
	dumpId(F("id597"), sizeof(id597), id597);
	dumpId(F("id599"), sizeof(id599), id599);
  
  // BMS:
	dumpId(F("id155"), sizeof(id155), id155);
	dumpId(F("id424"), sizeof(id424), id424);
	dumpId(F("id425"), sizeof(id425), id425);
	dumpId(F("id554"), sizeof(id554), id554);
	dumpId(F("id556"), sizeof(id556), id556);
	dumpId(F("id557"), sizeof(id557), id557);
	dumpId(F("id55E"), sizeof(id55E), id55E);
	dumpId(F("id55F"), sizeof(id55F), id55F);
	dumpId(F("id628"), sizeof(id628), id628);
	dumpId(F("id659"), sizeof(id659), id659);
  
  #endif
  
}


// -----------------------------------------------------
// Twizy state transitions:
//

void TwizyVirtualBMS::enterState(TwizyState newState) {
  
  if (twizyState == newState) {
    return;
  }
  
  #if TWIZY_DEBUG_LEVEL >= 1
  Serial.print(F(TWIZY_TAG "enterState: newState="));
  Serial.println(FS(twizyStateName[newState]));
  #endif
  
  switch (newState) {
    
    case Off:
    case Init:
      id155[3] = 0x94;
      id424[0] = 0x00;
      id425[0] = 0x1D;
      digitalWrite(TWIZY_3MW_CONTROL_PIN, 0);
      counter3MW = 0;
      clockCnt = 0;
      break;
      
    case Error:
      // Note: these updates are just for consistency, will currently
      //  not be sent as Error turns off sending (may change)
      id155[3] = 0x94;
      id424[0] |= 0x80;
      id425[0] = 0x24;
      digitalWrite(TWIZY_3MW_CONTROL_PIN, 0);
      break;
      
    case Ready:
      // restore minimum charge current level:
      if (id155[0] == 0) {
        id155[0] = 1;
      }
      id155[3] = 0x54;
      // keep stop charge request if set:
      if (id424[0] != 0x12) {
        id424[0] = 0x11;
      }
      id425[0] = 0x24;
      // if switching on...
      if (digitalRead(TWIZY_3MW_CONTROL_PIN) == 0) {
        // ...start 3MW pulse cycle:
        digitalWrite(TWIZY_3MW_CONTROL_PIN, 1);
        counter3MW = 30;
      }
      break;
      
    case Driving:
      id425[0] = 0x2A;
      break;
      
    case Charging:
      id425[0] = 0x0A;
      break;
      
    case StopCharge:
      id424[0] = 0x12;
      break;
      
    case StartTrickle:
      id425[0] = 0x2C;
      break;
      
    case Trickle:
      id425[0] = 0x2A;
      break;
  }
  
  // call BMS state transition:
  if (bmsEnterState) {
		(*bmsEnterState)(twizyState, newState);
	}
  
  // set new state:
  twizyState = newState;
}


// -----------------------------------------------------
// Twizy setup
//

void TwizyVirtualBMS::begin() {
  
  Serial.println(F("Twizy Virtual BMS " TWIZY_VBMS_VERSION));
  
  //
  // Init Twizy CAN interface
  //
  
  while (twizyCAN.begin(MCP_STDEXT, CAN_500KBPS, TWIZY_CAN_MCP_FREQ) != CAN_OK) {
		Serial.println(F(TWIZY_TAG "begin: waiting for CAN connection..."));
    delay(500);
  }
  
  // Set filters:
  
  twizyCAN.init_Mask(0, 0, 0x07FF0000);
  twizyCAN.init_Filt(0, 0, 0x04230000); // CHARGER: 423
  twizyCAN.init_Filt(1, 0, 0x05970000); // CHARGER: 597
  
  twizyCAN.init_Mask(1, 0, 0x07FF0000);
  twizyCAN.init_Filt(2, 0, 0x05990000); // DISPLAY: 599
  twizyCAN.init_Filt(3, 0, 0x00000000); // usable by setCanFilter(1)
  twizyCAN.init_Filt(4, 0, 0x00000000); // usable by setCanFilter(2)
  twizyCAN.init_Filt(5, 0, 0x00000000); // usable by setCanFilter(3)
  
  #ifdef TWIZY_CAN_IRQ_PIN
  pinMode(TWIZY_CAN_IRQ_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TWIZY_CAN_IRQ_PIN), twizyCanISR, FALLING);
  #endif
  
  twizyCAN.setMode(MCP_NORMAL);
  
  
  //
  // Init Twizy state machine & clock
  //
  
  pinMode(TWIZY_3MW_CONTROL_PIN, OUTPUT);
  
	enterState(Off);
	
  Timer1.initialize(TWIZY_CAN_CLOCK_US);
  Timer1.attachInterrupt(twizyClockISR);
  
	Serial.println(F(TWIZY_TAG "begin: done"));
  
}


// -----------------------------------------------------
// Twizy loop
//

void TwizyVirtualBMS::looper() {
  //
  // Receive Twizy CAN messages
  //
  
  #ifndef TWIZY_CAN_IRQ_PIN
  // No IRQ, we need to poll:
	twizyCanMsgReceived = twizyCAN.checkReceive();
  #endif
  
  if (twizyCanMsgReceived) {
		twizyCanMsgReceived = false;
    receiveCanMsgs();
  }
  
  //
  // Twizy ticker (send CAN messages, check for state transitions)
  //
  
  if (twizyClockTick) {
		twizyClockTick = false;
    ticker();
  }
}


#endif // _TwizyVirtualBMS_h
