/**
 * @file berta2p0.ino
 * @author  <@he121mei@htwg-konstanz.de>
 * @date 15 March 2026
 * @copyright me
 * @brief 
 */

#include "berta.h"
#include "berta_states.h"
#include <driver/gpio.h>
#include "esp_rom_gpio.h"

/** GPIO Pins*/
#define SWITCH_PIN 38 // digital I/O detects 12 V RE/FE when pump is running
#define LED_PIN 39 // not used
#define V33_EN_PIN 0 // Enables 3.3 V for the 3.3 V plane on the PCB
#define HARDWARE_WAKEUP_PIN 13 // Connected to ppin 38, wakes up if pump starts running
#define EEPROM_SIZE 512 // enough EEPROM size

MainStates mainState = REC_SETUP; // Main Statemachine
SubStates subState = IDLE; // Not yet in use

/**
 *@brief The main Arduino setup method
 */
void setup(){
  esp_task_wdt_delete(NULL);   // Remove Arduino-WDT for loopTask 
  esp_task_wdt_deinit();              // Stop completly

  esp_task_wdt_config_t wdt_config = { // WDT config
    .timeout_ms = WDT_SETUP_TIMEOUT * 1000,
    .idle_core_mask = 0, // (1 << portNUM_PROCESSORS) - 1
    .trigger_panic = true
  };

  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL); 

  esp_rom_gpio_pad_select_gpio(SWITCH_PIN);
  esp_rom_gpio_pad_select_gpio(V33_EN_PIN);

  gpio_set_direction((gpio_num_t) SWITCH_PIN, GPIO_MODE_INPUT);
  gpio_set_direction((gpio_num_t) V33_EN_PIN, GPIO_MODE_OUTPUT);

  gpio_set_pull_mode((gpio_num_t) SWITCH_PIN, GPIO_PULLUP_ONLY);

  gpio_set_level((gpio_num_t)V33_EN_PIN, 0); // sets 3.3 V / 250 mA on pin V33_EN_PIN

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL); // Deactivates all wakeup sources

  Serial.begin(115200);

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause(); // Return restart cause (GPIO Wakeup)
  Serial.printf("\nINFO: Wakeup cause = %s\n", wakeupToStr(cause));

  delay(5000);

  Serial.println("INFO: WD added");

  Serial.printf("\r\n\r\n=== WalterModem Berta2p0 ===\r\n\r\n");

  if(WalterModem::begin(&Serial2)) { // Start the modem
    Serial.println("Successfully initialized the modem");
    esp_task_wdt_reset();
  } else {
    Serial.println("Error: Could not initialize the modem");
    return;
  }

  // EEPROM
  EEPROM.begin(EEPROM_SIZE); //  Bit Array
  
  setupTLSProfile();

  esp_task_wdt_reset();

  setupHttpsProfile();

  esp_task_wdt_reset();

  setupGnssProfile();

  esp_task_wdt_reset();

  esp_task_wdt_delete(NULL);
  esp_task_wdt_deinit();
}

//Loop
//===================
void loop(){
  static unsigned long lastRequest = 0;
  const unsigned long requestInterval = 1000; // 1 seconds

  if(millis() - lastRequest >= requestInterval) {
    lastRequest = millis();

    Serial.printf("STATE: @begin %d \n", mainState);

    switch (mainState){
      
      case REC_SETUP:       // 0
        recSetup();
        break;

      case SETTING_UP:      // 1
        settingUp();
        break;

      case EVAL_STATE:      // 2
        if (!evalPump()) {
          evalPos();
        }
        break;

      case HAFEN:           // 3
        hafen();
        break;

      case FAHRT:           // 4
        fahrt();
        break;

      case ERROR:           // 5
        error();
        break;

      case SLEEP_STATE:     // 6
        sleepState();
        break;

      case SWITCH_PRESSED:  // 7
          switchPressed();
          mainState = EVAL_STATE;
        break;
    }
  }
}





