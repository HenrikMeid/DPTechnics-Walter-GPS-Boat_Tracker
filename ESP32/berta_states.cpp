#include "berta_states.h"

GeoPoint PORT_BOUND_RIGHT = {0.0, 0.0}; // Top right corner of a rectangle
GeoPoint PORT_BOUND_LEFT = {0.0, 0.0}; // Bottom left corner of a rectangle
volatile uint8_t errorCount = 0;
bool insidePort;
bool flgRecSetup = false;
bool flgInsidePortValid = false;
bool pumpRunning = false;
bool prevSwitchState = false;
bool flgPumpRunningOnce = false;

Preferences pers;

bool evalPump(void) {
  bool switchState = !gpio_get_level((gpio_num_t) SWITCH_PIN); // "!" bc of pullup on gpio
  Serial.printf("INFO: switchState = %d\n", switchState);

  if (EEPROM.readBool(INIT_FLAG_ADDR) == 0) {
    prevSwitchState = EEPROM.readBool(PREV_SWITCH_ADDR);
    Serial.printf("INFO: persStor key found prevSwitchState = %d\n", prevSwitchState);
  } else {
    prevSwitchState = switchState;  // erster Start -> kein Trigger
    EEPROM.writeBool(PREV_SWITCH_ADDR, switchState);
    EEPROM.commit();
    Serial.printf("INFO: No persStor key found, sot to %d\n", prevSwitchState);
  }

  pumpRunning = switchState ? true : false;
  Serial.printf("INFO: pumpRunning = %d\n", pumpRunning);

  if (switchState == true && prevSwitchState == false) {
    pumpRunning = true;
    flgPumpRunningOnce = true;
    mainState = SWITCH_PRESSED;
    Serial.println("INFO: Pump turned on");
  }
  
  if (switchState == false && prevSwitchState == true) {

    pumpRunning = false;
    flgPumpRunningOnce = true;
    mainState = SWITCH_PRESSED;
    Serial.println("INFO: Pump turned off");
  }

  if (switchState == prevSwitchState) {
    flgPumpRunningOnce = false;
    Serial.println("INFO: Pumpstate same");
    return false;
  }

  EEPROM.writeBool(PREV_SWITCH_ADDR, switchState);     // speichern
  EEPROM.commit();
  prevSwitchState = switchState;

  Serial.println("INFO: Pumpstate checked");
  return true;
}

bool recSetup(void){ // aktuell beide true aber i.O
  flgRecSetup = false;

  if (httpsGet((const char*) HTTPS_GET_ENDPOINT)) {
    flgRecSetup = true;
    mainState = SETTING_UP;
    return true;
  }
  else {
    flgRecSetup = false;
    mainState = SETTING_UP;
    return true;
  }
}

bool settingUp(void){
  if (flgRecSetup == true) {
    if (parseJsonSetup(incomingBuf)) {

      mainState = EVAL_STATE;

      Serial.printf("INFO PORT_BOUND_RIGHT.lon = %.6f\n",PORT_BOUND_RIGHT.lon);
      return true;
    }
  }
  else { 
    Serial.println("ERROR: recSetup() failed, using standard values");
    EEPROM.get(GEPOINTS_ADDR, PORT_BOUND_RIGHT);
    EEPROM.get(sizeof(GeoPoint)+GEPOINTS_ADDR, PORT_BOUND_LEFT);

    char PostBody[80];
    
    snprintf(PostBody, sizeof(PostBody),
            "sensor=info&wert=using std p.cord.");

    if (!httpsPost(HTTPS_POST_ENDPOINT, 
              (const uint8_t*) PostBody, 
              strlen(PostBody),
              "application/x-www-form-urlencoded")) {
      mainState = ERROR;
    }


    mainState = EVAL_STATE;
    return true;
  }
}

bool evalPos(){
  if (!attemptGNSSFix((uint8_t)2, (uint32_t)120000)) {
    mainState = ERROR;
    return false;
  }
  else {
    if (evalPosition() && flgInsidePortValid) { // Switches state to either HAFEN or FAHRT
      errorCount = 0;

      if (insidePort) {
        mainState = HAFEN;
      }
      else if (!insidePort) {
        mainState = FAHRT;
      }
    } 
    else { 
      mainState = ERROR;
      return false; 
    }
  }

  return true;
}

bool hafen(void) {
  char PostBody[80];
  
  snprintf(PostBody, sizeof(PostBody),
          "sensor=state&wert=hafen");

  if (!httpsPost(HTTPS_POST_ENDPOINT, 
            (const uint8_t*) PostBody, 
            strlen(PostBody),
            "application/x-www-form-urlencoded")) {
    mainState = ERROR;
  }

  if (!sendGpsLoc()) {
    mainState = ERROR;
  }
  else { errorCount = 0; }

  mainState = SLEEP_STATE;

  return true;
}

bool fahrt(void) {
  if (!sendGpsLoc()) {
    mainState = ERROR;
    return false;
  }

  errorCount = 0;

  mainState = EVAL_STATE;

  sleepMinutes((uint64_t) 1ULL);

  return true;
}

bool error(void) {
  char PostBody[80];
  
  snprintf(PostBody, sizeof(PostBody),
          "sensor=state&wert=error");
  httpsPost(HTTPS_POST_ENDPOINT, 
            (const uint8_t*) PostBody, 
            strlen(PostBody),
            "application/x-www-form-urlencoded");

  if (errorCount == 4) {
    errorCount = 0;

    char PostBody[80];
  
    snprintf(PostBody, sizeof(PostBody),
            "sensor=state&wert=restarting");
    httpsPost(HTTPS_POST_ENDPOINT, 
              (const uint8_t*) PostBody, 
              strlen(PostBody),
              "application/x-www-form-urlencoded");

    Serial.println("ERROR: 5 concat errors, restarting ...");

    ESP.restart();
  }

  errorCount = errorCount  + 1;

  mainState = EVAL_STATE;
  
  return true;
}

bool sleepState(void) {
  Serial.println("Going to bed ...");
  char PostBody[80];
  
  snprintf(PostBody, sizeof(PostBody),
          "sensor=state&wert=sleep");
  httpsPost(HTTPS_POST_ENDPOINT, 
            (const uint8_t*) PostBody, 
            strlen(PostBody),
            "application/x-www-form-urlencoded");

  esp_sleep_enable_timer_wakeup(15ULL * 60ULL * 1000ULL * 1000ULL);
  
  enableWakeupInterrupt();

  esp_light_sleep_start();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const char* causeStr = wakeupToStr(cause);
  Serial.printf("Sleep wakeup cause: %s\n", causeStr);

  disableWakeupInterrupt();

  Serial.println("nach sleep EVAL_STATE");
  mainState = EVAL_STATE;

  return true;
}

bool switchPressed(void) { // 12 V pump running  
  char PostBody[80];

  if (pumpRunning && flgPumpRunningOnce) {
    snprintf(PostBody, sizeof(PostBody),
            "sensor=pumpe&wert=True");
    Serial.println("INFO: Pump running");
  }

  if (!pumpRunning && flgPumpRunningOnce) {
    snprintf(PostBody, sizeof(PostBody),
            "sensor=pumpe&wert=False");
    Serial.println("INFO: Pump stoped");
  }

  if (!flgPumpRunningOnce) {
    return false;
  }

  httpsPost(HTTPS_POST_ENDPOINT, 
            (const uint8_t*) PostBody, 
            strlen(PostBody),
            "application/x-www-form-urlencoded");
  return true;
  // mainState -> EVAL_STATE in loop() !!!
}

/* User definded functions
 * ======================= */
bool evalPosition(void) { // Evaluates weather GPS is within the set port borders or outside
  flgInsidePortValid = false;
  if (!gnssFixRcvd || latestGnssFix.status != WALTER_MODEM_GNSS_FIX_STATUS_READY) {
    Serial.println("ERROR: latestGnssFix not ready");
    return false;
  }

  double latMeas = latestGnssFix.latitude;
  double lonMeas = latestGnssFix.longitude;
  double confidenceGPSMeas = latestGnssFix.estimatedConfidence;
  uint32_t timeStampGPSMeas = static_cast<uint32_t>(latestGnssFix.timestamp);

  insidePort = (latMeas >= PORT_BOUND_LEFT.lat && latMeas <= PORT_BOUND_RIGHT.lat &&
                     lonMeas >= PORT_BOUND_RIGHT.lon && lonMeas <= PORT_BOUND_LEFT.lon);
  flgInsidePortValid = true;
  return true;
}

/**
 * @brief Parst die vom Server empfangenen Hafen-Koordinaten und speichert sie.
 * 
 * Diese Funktion extrahiert die Hafen-Bounds aus dem JSON-Response und
 * schreibt sie nur dann in den EEPROM, wenn sich die Werte signifikant
 * geändert haben (mehr als 1 Meter Unterschied).
 * 
 * @param buf Buffer mit JSON-Daten vom Server
 * @return true bei erfolgreicher Verarbeitung
 * @return false bei JSON-Parse-Fehler
 * 
 * @note Verwendet Epsilon-Vergleich um Floating-Point-Rundungsfehler zu vermeiden.
 *       Ein Unterschied < 0.0001° (≈ 10 Meter) wird ignoriert.
 */
bool parseJsonSetup(const uint8_t* buf) {
  const char* json = reinterpret_cast<const char*>(buf);

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, json);

  if (err) {
    Serial.print("ERROR: JSON fail ");
    Serial.println(err.c_str());
    return false;
  }

  // ===== 1. NEUE WERTE AUS JSON PARSEN =====
  PORT_BOUND_RIGHT.lat = doc["port_bound_right"]["lat"];
  PORT_BOUND_RIGHT.lon = doc["port_bound_right"]["lon"];

  PORT_BOUND_LEFT.lat  = doc["port_bound_left"]["lat"];
  PORT_BOUND_LEFT.lon  = doc["port_bound_left"]["lon"];

  Serial.printf("INFO: Received from server:\n");
  Serial.printf("  PORT_BOUND_RIGHT: lat=%.6f, lon=%.6f\n", 
                PORT_BOUND_RIGHT.lat, PORT_BOUND_RIGHT.lon);
  Serial.printf("\n PORT_BOUND_LEFT:  lat=%.6f, lon=%.6f\n", 
                PORT_BOUND_LEFT.lat, PORT_BOUND_LEFT.lon);

  // ===== 2. ALTE WERTE AUS EEPROM LESEN (KORREKT!) =====
  GeoPoint PORT_BOUND_RIGHT_OLD;  // ← Erst deklarieren
  GeoPoint PORT_BOUND_LEFT_OLD;
  
  EEPROM.get(GEPOINTS_ADDR, PORT_BOUND_RIGHT_OLD);  // ← DANN lesen
  EEPROM.get(sizeof(GeoPoint) + GEPOINTS_ADDR, PORT_BOUND_LEFT_OLD);

  Serial.printf("INFO: Previously stored in EEPROM:\n");
  Serial.printf("  PORT_BOUND_RIGHT: lat=%.6f, lon=%.6f\n", 
                PORT_BOUND_RIGHT_OLD.lat, PORT_BOUND_RIGHT_OLD.lon);
  Serial.printf("  PORT_BOUND_LEFT:  lat=%.6f, lon=%.6f\n", 
                PORT_BOUND_LEFT_OLD.lat, PORT_BOUND_LEFT_OLD.lon);

  // ===== 3. EPSILON-VERGLEICH =====
  const double EPSILON = 0.00001;  // ~1 Meter Toleranz
  
  bool lat_right_changed = fabs(PORT_BOUND_RIGHT_OLD.lat - PORT_BOUND_RIGHT.lat) > EPSILON;
  bool lon_right_changed = fabs(PORT_BOUND_RIGHT_OLD.lon - PORT_BOUND_RIGHT.lon) > EPSILON;
  bool lat_left_changed  = fabs(PORT_BOUND_LEFT_OLD.lat - PORT_BOUND_LEFT.lat) > EPSILON;
  bool lon_left_changed  = fabs(PORT_BOUND_LEFT_OLD.lon - PORT_BOUND_LEFT.lon) > EPSILON;

  bool changed = lat_right_changed || lon_right_changed || 
                 lat_left_changed || lon_left_changed;

  // ===== 4. NUR BEI ÄNDERUNG SCHREIBEN =====
  if (changed) {
    EEPROM.put(GEPOINTS_ADDR, PORT_BOUND_RIGHT);
    EEPROM.put(sizeof(GeoPoint) + GEPOINTS_ADDR, PORT_BOUND_LEFT);
    EEPROM.commit();
    
    Serial.println("INFO: Harbour bounds changed significantly (>10 m), wrote to EEPROM");
    
    if (lat_right_changed || lon_right_changed) {
      Serial.printf("  -> RIGHT changed by lat=%.6f° lon=%.6f°\n", 
                    fabs(PORT_BOUND_RIGHT_OLD.lat - PORT_BOUND_RIGHT.lat),
                    fabs(PORT_BOUND_RIGHT_OLD.lon - PORT_BOUND_RIGHT.lon));
    }
    if (lat_left_changed || lon_left_changed) {
      Serial.printf("  -> LEFT changed by lat=%.6f° lon=%.6f°\n", 
                    fabs(PORT_BOUND_LEFT_OLD.lat - PORT_BOUND_LEFT.lat),
                    fabs(PORT_BOUND_LEFT_OLD.lon - PORT_BOUND_LEFT.lon));
    }
  } else {
    Serial.println("INFO: Harbour bounds unchanged (difference < 10 m), no EEPROM write");
  }

  return true;
}
