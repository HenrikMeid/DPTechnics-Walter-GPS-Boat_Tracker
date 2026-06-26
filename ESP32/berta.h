#ifndef berta_H
#define berta_H

#include <Arduino.h>
#include <esp_mac.h>
#include <WalterModem.h>
#include <HardwareSerial.h>
#include <cstdint>
#include <berta_states.h>
#include "esp_task_wdt.h"
#include <EEPROM.h>

/* Konstanten
 * ======================= */
#define HTTPS_PORT 443
#define HTTPS_HOST "adress.com" // Change me
#define HTTPS_GET_ENDPOINT "/daten"
#define HTTPS_POST_ENDPOINT "/daten"
#define HTTP_USER "User" // Change me
#define HTTP_PASS "password" // Change me
#define WDT_SETUP_TIMEOUT 60 // Watchdog timout in seconds

struct GeoPoint {
  double lat;
  double lon;
};

typedef struct { // unused
    volatile uint32_t pumpStart:1;  
    volatile uint32_t pumpStop:1;    
    volatile uint32_t pumpPrevState:1;
    volatile uint32_t pumpTriggered:1;
    volatile uint32_t unusedBits:28;     
} interruptFlags_t;

extern uint16_t HTTP_MAX_TIMEOUT;
extern uint16_t HTTP_MAX_NC_TIMEOUT;
extern uint8_t  HTTP_MAX_INACTIVITY_TIMEOUT;
extern uint8_t incomingBuf[1024];
extern GeoPoint PORT_BOUND_RIGHT;
extern GeoPoint PORT_BOUND_LEFT;

/* Externe globale Objekte
 * ======================= */
extern WalterModem modem;
extern WalterModemRsp rsp;

extern volatile bool gnssFixRcvd;
// extern WalterModemGNSSFix latestGnssFix; alt
//extern WMGNSSEventData latestGnssFix;
extern WMGNSSFixEvent latestGnssFix;
/* Konfiguration
 * ======================= */
/**
 * @brief The TLS profile to use for the application (!!!1 is reserved for BlueCherry!!!)
 */
#define HTTPS_TLS_PROFILE 2
/**
 * @brief HTTPS profile
 */
#define MODEM_HTTPS_PROFILE 1
#define MAX_GNSS_CONFIDENCE 300
#define SLOT_IDX_CERT 12

/* LTE / Network
 * ======================= */
bool lteConnected();
bool waitForNetwork(int timeout_sec = 300);
bool lteDisconnect();
bool lteConnect();

/* TLS / SSL
 * ======================= */
bool setupTLSProfile();

/* HTTPS
 * ======================= */
bool setupHttpsProfile(void);
bool httpsGet(const char* path);
bool httpsPost(const char* path,
               const uint8_t* body,
               size_t bodyLen,
               const char* mimeType = "application/x-www-form-urlencoded");

/* GNSS
 * ======================= */
bool setupGnssProfile();
// void gnssEventHandler(const WalterModemGNSSFix* fix, void* args); alt
void gnssEventHandler(WMGNSSEventType type, const WMGNSSEventData* data, void* args);
bool attemptGNSSFix(uint8_t maxAttempts, uint32_t fixTimeoutMs);

/** User definded functions
* =======================*/
bool sendHttpsConectInfo(void);
bool sendGpsLoc(void);
void sleepMinutes(uint64_t minutes);
const char* wakeupToStr(esp_sleep_wakeup_cause_t cause);
// Interrupts
void IRAM_ATTR onInterrupt(void* arg);
void enableWakeupInterrupt(void);
void disableWakeupInterrupt(void);

#endif
