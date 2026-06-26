#ifndef berta_states_H
#define berta_states_H

#include <Arduino.h>
#include "berta.h"
#include <ArduinoJson.h>
#include "esp_sleep.h"
#include <Preferences.h>

/* States
 * ======================= */
enum MainStates {
  REC_SETUP,  // 0
  SETTING_UP, // 1
  EVAL_STATE, // 2
  HAFEN,          // 3
  FAHRT,          // 4
  ERROR,         // 5
  SLEEP_STATE, // 6
  SWITCH_PRESSED // 7
};

enum SubStates {
  IDLE,
  LTE_MODE,
  HTTPS_SENDING,
  GNSS_MODE,
  GETTING_FIX,
  PUMP_RUNNING
};

extern MainStates mainState;
extern SubStates subState;
extern Preferences pers;

/* Konstanten
 * ======================= */
#define HARDWARE_WAKEUP_PIN 13
#define SWITCH_PIN 38
#define INIT_FLAG_ADDR 0 // To check if eeprom got init
#define PREV_SWITCH_ADDR 1
#define GEPOINTS_ADDR 2

/* States
 * ======================= */
bool evalPump(void);
bool recSetup(void);
bool settingUp(void);
bool evalPos(void);
bool hafen(void);
bool fahrt(void);
bool error(void);
bool sleepState(void);
bool switchPressed(void);

/* User definded functions
 * ======================= */
bool evalPosition(void);
bool parseJsonSetup(const uint8_t* buf);

#endif
