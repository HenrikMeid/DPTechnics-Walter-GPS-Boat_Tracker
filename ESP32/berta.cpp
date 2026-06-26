/* Externe Abhängigkeiten
 * ======================= */
#include <HardwareSerial.h>
#include "berta.h"

/** Konstanten & Buffer
 * ======================= 
 * @brief Buffer for incoming HTTPS response
 */
uint8_t incomingBuf[1024] = { 0 };

uint16_t HTTP_MAX_TIMEOUT = 20;
uint16_t HTTP_MAX_NC_TIMEOUT = 10;
uint8_t  HTTP_MAX_INACTIVITY_TIMEOUT = 15;
static bool isrInstalled = false;
static uint8_t wakeEdge;
extern bool pumpRunning;


extern volatile interruptFlags_t interruptFlags = {0};

/**
 * @brief Root CA certificate in PEM format.
 *
 * @note Example uses LetsEncrypt Root
 *
 * Used to validate the server's TLS certificate.
 */
const char ca_cert[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
// Enter Certificat here
-----END CERTIFICATE-----
)EOF";

/**
 *@brief The TLS profile to use for the application (!!!1 is reserved for BlueCherry!!!)
 */
#define HTTPS_TLS_PROFILE 2

/**
 * @brief HTTPS profile No.
 */
#define MODEM_HTTPS_PROFILE 1

/**
* @brief APN Name. Findet sich auf der Homepage des Netzanbieters. Null bei automatischer suche.
*/
CONFIG(CELLULAR_APN, const char *, "internet")

/** Globale Objekte
 * =======================
 * @brief The modem instance.
 */
WalterModem modem;
/**
 * @brief Response object containing command response information.
 */
WalterModemRsp rsp = {};
/** 
* @brief Flag indicates if resetup is necessary after lte disconnection 
*/
bool httpsNeedsSetup = true; 

volatile bool gnssFixRcvd = false;
static bool firstGNSSFix = true;
//WalterModemGNSSFix latestGnssFix = {}; alt
//WMGNSSEventData latestGnssFix = {}; // neu
WMGNSSFixEvent latestGnssFix = {};
/** LTE / Network
 * ======================= 
 * @brief This function checks if we are connected to the LTE network
 *
 * @return true when connected, false otherwise
 */
bool lteConnected(){
  WalterModemNetworkRegState regState = modem.getNetworkRegState();
  return (regState == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
          regState == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING);
}

/**
 * @brief This function waits for the modem to be connected to the LTE network.
 *
 * @param timeout_sec The amount of seconds to wait before returning a time-out.
 *
 * @return true if connected, false on time-out.
 */
bool waitForNetwork(int timeout_sec){
  Serial.print("INFO: Connecting to the network...");
  int time = 0;
  while(!lteConnected()) {
    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(1 * 1000));
    time++;
    if(time > timeout_sec)
      return false;
  }
    Serial.println();
    Serial.println("INFO: Connected to the network");
      return true;
}

/**
 * @brief Disconnect from the LTE network.
 *
 * This function will disconnect the modem from the LTE network and block until
 * the network is actually disconnected. After the network is disconnected the
 * GNSS subsystem can be used.
 *
 * @return true on success, false on error.
 */
bool lteDisconnect(){
  /* Set the operational state to minimum */
  modem.gnssSetEventHandler(nullptr, nullptr);
  if(modem.setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {
    Serial.println("INFO: Successfully set operational state to MINIMUM");
  } else {
    Serial.println("ERROR: Could not set operational state to MINIMUM");
    return false;
  }
  /* Wait for the network to become available */
  WalterModemNetworkRegState regState = modem.getNetworkRegState();
  while(regState != WALTER_MODEM_NETWORK_REG_NOT_SEARCHING) {
    vTaskDelay(pdMS_TO_TICKS(100));
    regState = modem.getNetworkRegState();
  }

  httpsNeedsSetup = true;
  modem.gnssSetEventHandler(gnssEventHandler, nullptr);

  Serial.println("INFO: Disconnected from the network");
  return true;
}

/**
 * @brief This function tries to connect the modem to the cellular network.
 *
 * @return true on success, false on error.
 */
bool lteConnect(){
    // NEU: Früh raus wenn schon verbunden
  if (lteConnected()) {
    if (httpsNeedsSetup) {
      Serial.println("INFO: Already connected, reinitializing HTTPS");
      if(!setupTLSProfile()) return false;
      if(!setupHttpsProfile()) return false;
      httpsNeedsSetup = false;
    }
    return true;
  }
  /* Set the operational state to NO RF */
  if(modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
    Serial.println("INFO: Successfully set operational state to NO RF");
  } else {
    Serial.println("ERROR: Could not set operational state to NO RF");
    return false;
  }

  /* Create PDP context */
  if(modem.definePDPContext(1, CELLULAR_APN)) {
    Serial.println("INFO: Created PDP context");
  } else {
    Serial.println("ERROR: Could not create PDP context");
    return false;
  }

  /* Set the operational state to full */
  if(modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
    Serial.println("INFO: Successfully set operational state to FULL");
  } else {
    Serial.println("ERROR: Could not set operational state to FULL");
    return false;
  }

  /* Set the network operator selection to automatic */
  if(modem.setNetworkSelectionMode(WALTER_MODEM_NETWORK_SEL_MODE_AUTOMATIC)) {
    Serial.println("INFO: Network selection mode was set to automatic");
  } else {
    Serial.println("ERROR: Could not set the network selection mode to automatic");
    return false;
  }

  waitForNetwork();

  if(httpsNeedsSetup) {
    Serial.println("INFO: Reinitializing HTTPS after LTE reconnect");

    if(!setupTLSProfile()) return false;
    if(!setupHttpsProfile()) return false;

    httpsNeedsSetup = false;
  }
  return true;
}

/** TLS
 * ======================= 
 * @brief Writes TLS credentials to the modem's NVS and configures the TLS profile.
 *
 * This function stores the provided TLS certificates and private keys into the modem's
 * non-volatile storage (NVS), and then sets up a TLS profile for secure communication.
 * These configuration changes are persistent across reboots.
 *
 * @note
 * - Certificate indexes 0–10 are reserved for Sequans and BlueCherry internal usage.
 * - Private key index 1 is reserved for BlueCherry internal usage.
 * - Do not attempt to override or use these reserved indexes.
 *
 * @return
 * - true if the credentials were successfully written and the profile configured.
 * - false otherwise.
 */
bool setupTLSProfile(void){

  // Sollte nur bei Aenderung des CA certs und nach Firmwareupgrade aufgerufen werden
  /*if(!modem.tlsWriteCredential(false, 12, ca_cert)) {
    Serial.println("Error: CA cert upload failed");
    return false;
  }*/

  if(modem.tlsConfigProfile(HTTPS_TLS_PROFILE, WALTER_MODEM_TLS_VALIDATION_CA,
                            WALTER_MODEM_TLS_VERSION_13, 12)) {
    Serial.println("INFO: TLS profile configured");
  } else {
    Serial.println("ERROR: TLS profile configuration failed");
    return false;
  }

  return true;
}

/** HTTPS intern
 * ======================= 
 * @brief Common routine to wait for and print an HTTP response.
 */
bool setupHttpsProfile(void){
    /* Configure the HTTPS profile */
  if(modem.httpConfigProfile(MODEM_HTTPS_PROFILE, HTTPS_HOST, 
                            HTTPS_PORT, 
                            HTTPS_TLS_PROFILE,
                            true, 
                            HTTP_USER, 
                            HTTP_PASS, 
                            HTTP_MAX_TIMEOUT, 
                            HTTP_MAX_NC_TIMEOUT, 
                            HTTP_MAX_INACTIVITY_TIMEOUT, 
                            &rsp, 
                            NULL, NULL)) {
    Serial.println("INFO: Successfully configured the HTTPS profile");
    return true;
  } else {
    Serial.println("ERROR: Failed to configure HTTPS profile");
    return false;
  }
}

static bool waitForHttpsResponse(uint8_t profile, const char* contentType){
  Serial.print("INFO: Waiting for reply...");
  const uint16_t maxPolls = 30;
  for(uint16_t i = 0; i < maxPolls; i++) {
    Serial.print(".");
    if(modem.httpDidRing(profile, incomingBuf, sizeof(incomingBuf), &rsp)) {
      Serial.println();
      Serial.printf("HTTPS status code (Modem): %d\r\n", rsp.data.httpResponse.httpStatus);
      Serial.printf("Content type: %s\r\n", contentType);
      Serial.printf("Payload:\r\n%s\r\n", incomingBuf);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(1 * 1000));
  }
  Serial.println();
  Serial.println("ERROR: HTTPS response timeout");
  return false;
}

/** HTTPS API
 * ======================= 
 * @brief Perform an HTTPS GET request.
 */
bool httpsGet(const char* path){
  if (!lteConnected()) {
    Serial.println("ERROR: LTE not connected, reconnecting...");
  }
  if (!lteConnect()) {
    Serial.println("ERROR: LTE reconnect failed");
    return false;
  }

  char contentTypeBufGet[32] = { 0 };

  Serial.printf("Sending HTTPS GET to %s%s\r\n", HTTPS_HOST, path);
  if(!modem.httpQuery(MODEM_HTTPS_PROFILE, 
                      path, 
                      WALTER_MODEM_HTTP_QUERY_CMD_GET, 
                      contentTypeBufGet,
                      sizeof(contentTypeBufGet))){
    Serial.println("ERROR: HTTPS GET query failed");
    return false;
  }
  Serial.println("INFO: HTTPS GET successfully sent");
  return waitForHttpsResponse(MODEM_HTTPS_PROFILE, contentTypeBufGet);
}

/**
 * @brief Perform an HTTPS POST request with a body.
 */
bool httpsPost(const char* path, const uint8_t* body, size_t bodyLen,
               const char* mimeType){

  if (!lteConnected()) {
    Serial.println("ERROR: LTE not connected, reconnecting...");
  if (!lteConnect()) {
      Serial.println("ERROR: LTE reconnect failed");
    return false;
  }
  }

  /** 
  * @brief content type buffer of https response NULL, 0 if not used.
  */
  char contentTypeBuf[32] = { 0 }; 

  Serial.printf("INFO: Sending HTTPS POST to %s%s (%u bytes, type %s)\r\n", HTTPS_HOST, path,
                (unsigned) bodyLen, mimeType);
  if(!modem.httpSend(MODEM_HTTPS_PROFILE, 
                    path,
                    (uint8_t*) body, 
                    (uint16_t) bodyLen,
                     WALTER_MODEM_HTTP_SEND_CMD_POST,
                     WALTER_MODEM_HTTP_POST_PARAM_URL_ENCODED, 
                     contentTypeBuf,
                     sizeof(contentTypeBuf))) {
    Serial.println("ERROR: HTTPS POST failed");
    return false;
  }
  Serial.println("INFO: HTTPS POST successfully sent");
  return waitForHttpsResponse(MODEM_HTTPS_PROFILE, contentTypeBuf);
}

/** GNSS
* =======================
* @brief Sets up the gnss modem
*/
bool setupGnssProfile(){
  if (!modem.gnssConfig(
      WALTER_MODEM_GNSS_SENS_MODE_HIGH,
      WALTER_MODEM_GNSS_ACQ_MODE_COLD_WARM_START,
      WALTER_MODEM_GNSS_LOC_MODE_ON_DEVICE_LOCATION )){
      Serial.println("ERROR: GNSS config failed");
      return false;
  }
  else {
    // Assistance (optional, aber empfohlen)
    modem.gnssUpdateAssistance(WALTER_MODEM_GNSS_ASSISTANCE_TYPE_REALTIME_EPHEMERIS);
    vTaskDelay(pdMS_TO_TICKS(2 * 1000));
    modem.gnssSetEventHandler(gnssEventHandler, NULL);
    return true;
  }
}

/**  
 * @brief Handler that takes action when GNSS action is performed
 */
/*void gnssEventHandler(const WalterModemGNSSFix* fix, void* args){
    Serial.println("INFO: GNSS EVENT RECEIVED");
    if (fix == nullptr) {
      Serial.println("ERROR: GNSS fix pointer is NULL");
      return;
    }
    memcpy(&latestGnssFix, fix, sizeof(WalterModemGNSSFix));
    gnssFixRcvd = true;
}*/


void gnssEventHandler(WMGNSSEventType type, const WMGNSSEventData* data, void* args) {
    if (data == nullptr) return;
    
    if (type == WALTER_MODEM_GNSS_EVENT_FIX) {
        memcpy(&latestGnssFix, &data->gnssfix, sizeof(WMGNSSFixEvent));
        gnssFixRcvd = true;
        Serial.printf("INFO: GNSS FIX received, status=%d\n", latestGnssFix.status);
    } else if (type == WALTER_MODEM_GNSS_EVENT_STATUS) {
        Serial.println("INFO: GNSS STATUS event (ignored)");
    } else if (type == WALTER_MODEM_GNSS_EVENT_ASSISTANCE) {
        Serial.println("INFO: GNSS ASSISTANCE event (ignored)");
    }
}
/**
* @brief Attempts to recieve a GNSS fix
*
* @param maxAttempts Number of attempts the function tryes to get a GNSS fix
*
* @param fixTimeoutMs Timeout in ms for every fix (at least 60 s)
*/
bool attemptGNSSFix(uint8_t maxAttempts, uint32_t fixTimeoutMs) {
    /* GNSS Clock prüfen */
    modem.gnssGetUTCTime(&rsp);
    if (rsp.data.clock.epochTime <= 4) {
        Serial.println("ERROR: GNSS clock invalid - LTE time sync required");
        if (!lteConnected() && !lteConnect()) {
            Serial.println("ERROR: LTE connect failed (clock sync)");
            return false;
        }
        for (int i = 0; i < 5; i++) {
            modem.gnssGetUTCTime(&rsp);
            if (rsp.data.clock.epochTime > 4) {
                Serial.println("INFO: GNSS clock synchronized");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    /* GNSS Assistance holen (braucht LTE) */
    if (!lteConnected()) {
        lteConnect();
    }
    modem.gnssUpdateAssistance(WALTER_MODEM_GNSS_ASSISTANCE_TYPE_REALTIME_EPHEMERIS);
    vTaskDelay(pdMS_TO_TICKS(5000)); // ← NEU: Zeit für Assistance-Download

    /* LTE trennen für GNSS */
    if (lteConnected()) {
        lteDisconnect();
    }

    /* GNSS konfigurieren */
    modem.gnssConfig(
        WALTER_MODEM_GNSS_SENS_MODE_HIGH,
        firstGNSSFix ? WALTER_MODEM_GNSS_ACQ_MODE_COLD_WARM_START
                     : WALTER_MODEM_GNSS_ACQ_MODE_HOT_START,
        WALTER_MODEM_GNSS_LOC_MODE_ON_DEVICE_LOCATION
    );
    firstGNSSFix = false;
    modem.gnssSetEventHandler(gnssEventHandler, NULL);

    /* Fix-Versuche */
    for (uint8_t attempt = 1; attempt <= maxAttempts; attempt++) {
        Serial.printf("INFO: GNSS fix attempt %d/%d\r\n", attempt, maxAttempts);
        gnssFixRcvd = false;

        if (!modem.gnssPerformAction(WALTER_MODEM_GNSS_ACTION_GET_SINGLE_FIX)) {
            Serial.println("ERROR: GNSS fix request failed");
            vTaskDelay(pdMS_TO_TICKS(2000)); // ← NEU: kurz warten vor Retry
            continue;
        }

        uint32_t start = millis();
        while (!gnssFixRcvd && (millis() - start < fixTimeoutMs)) {
            vTaskDelay(pdMS_TO_TICKS(500)); // ← FIX: war 0.5*1000 → jetzt Integer
        }

        if (!gnssFixRcvd) {
            Serial.println("ERROR: GNSS fix timeout");
            modem.gnssPerformAction(WALTER_MODEM_GNSS_ACTION_CANCEL); // ← NEU: cancel vor Retry
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        Serial.printf("Fix received: lat=%.6f lon=%.6f confidence=%.2f\r\n",
            latestGnssFix.latitude, latestGnssFix.longitude, latestGnssFix.estimatedConfidence);

        if (latestGnssFix.estimatedConfidence <= MAX_GNSS_CONFIDENCE) {
            return true;
        }
        Serial.println("ERROR: Fix too inaccurate, retrying...");
        modem.gnssPerformAction(WALTER_MODEM_GNSS_ACTION_CANCEL); // ← NEU
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    modem.gnssPerformAction(WALTER_MODEM_GNSS_ACTION_CANCEL);
    Serial.println("ERROR: GNSS fix failed after retries");
    return false;
}

/** User definded functions
* =======================
* @brief user definded functions
*/
bool sendHttpsConectInfo(void){  
  char PostBody[80];
  
  snprintf(PostBody, sizeof(PostBody),
          "sensor=https_connection&wert=True");
  httpsPost(HTTPS_POST_ENDPOINT, 
            (const uint8_t*) PostBody, 
            strlen(PostBody),
            "application/x-www-form-urlencoded");
  Serial.println("INFO: https connect info send");
  return true;
}

bool sendGpsLoc(void){
  if (!gnssFixRcvd || latestGnssFix.status != WALTER_MODEM_GNSS_FIX_STATUS_READY) {
    Serial.println("ERROR: latestGnssFix not ready");
    return false;
  }

  double lat = latestGnssFix.latitude;
  double lon = latestGnssFix.longitude;
  double confidenceGPS = latestGnssFix.estimatedConfidence;
  uint32_t timeStampGPS = static_cast<uint32_t>(latestGnssFix.timestamp);

  static char PostBodyGPS[80];
  
  snprintf(PostBodyGPS, sizeof(PostBodyGPS),
          "sensor=position&wert=%.6f%%2C%.6f",
          lat, lon);
  httpsPost(HTTPS_POST_ENDPOINT, 
            (const uint8_t*) PostBodyGPS, 
            strlen(PostBodyGPS),
            "application/x-www-form-urlencoded");
            
  return true;
}

void sleepMinutes(uint64_t minutes) { // uint64 um overflow vor umrechnung zu vermeiden
  esp_sleep_enable_timer_wakeup((uint64_t)minutes * 60ULL * 1000000ULL);
  enableWakeupInterrupt();
  esp_light_sleep_start();
  disableWakeupInterrupt();
}

void enableWakeupInterrupt() {
    if (pumpRunning) {
      wakeEdge = 1; // wake on HIGH
    } else {
      wakeEdge = 0; // wake on LOW
    }

    esp_sleep_enable_ext0_wakeup((gpio_num_t) HARDWARE_WAKEUP_PIN, wakeEdge);
}

void disableWakeupInterrupt() {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
}

const char* wakeupToStr(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0: return "EXT0";
        case ESP_SLEEP_WAKEUP_EXT1: return "EXT1";
        case ESP_SLEEP_WAKEUP_TIMER: return "TIMER";
        case ESP_SLEEP_WAKEUP_GPIO: return "GPIO";
        case ESP_SLEEP_WAKEUP_UART: return "UART";
        default: return "POWERON / RESET";
    }
}

