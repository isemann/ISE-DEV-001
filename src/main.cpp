/**
 * @file LoRaWAN_OTAA.ino
 * @author Bernhard Isemann
 * @brief LoRaWan node with OTAA registration
 * @version 1.0
 * @date 2024-12-27
 * 
 * @copyright Copyright (c) 2025
 * 
 * @note RAK5005-O GPIO mapping to RAK4631 GPIO ports
 * IO1 <-> P0.17 (Arduino GPIO number 17)
 * IO2 <-> P1.02 (Arduino GPIO number 34)
 * IO3 <-> P0.21 (Arduino GPIO number 21)
 * IO4 <-> P0.04 (Arduino GPIO number 4)
 * IO5 <-> P0.09 (Arduino GPIO number 9)
 * IO6 <-> P0.10 (Arduino GPIO number 10)
 * SW1 <-> P0.01 (Arduino GPIO number 1)
 */
#include <Arduino.h>
#include <LoRaWan-RAK4630.h> //http://librarymanager/All#SX126x
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h> // Click to install library: http://librarymanager/All#Adafruit_BME680

Adafruit_BME680 bme;

#include <Arduino_LPS22HB.h>

// RAK4630 supply two LED
#ifndef LED_BUILTIN
#define LED_BUILTIN 35
#endif

#ifndef LED_BUILTIN2
#define LED_BUILTIN2 36
#endif

bool doOTAA = true;
#define SCHED_MAX_EVENT_DATA_SIZE APP_TIMER_SCHED_EVENT_DATA_SIZE /**< Maximum size of scheduler events. */
#define SCHED_QUEUE_SIZE 60                                       /**< Maximum number of events in the scheduler queue. */
#define LORAWAN_DATERATE DR_0                                     /*LoRaMac datarates definition, from DR_0 to DR_5*/
#define LORAWAN_TX_POWER TX_POWER_5                               /*LoRaMac tx power definition, from TX_POWER_0 to TX_POWER_15*/
#define JOINREQ_NBTRIALS 3                                        /**< Number of trials for the join request. */
DeviceClass_t gCurrentClass = CLASS_A;                            /* class definition*/
lmh_confirm gCurrentConfirm = LMH_CONFIRMED_MSG;                  /* confirm/unconfirm packet definition*/
uint8_t gAppPort = LORAWAN_APP_PORT;                              /* data port*/

/** Definition of the Analog input that is connected to the battery voltage divider */
#define PIN_VBAT A0
/** Definition of milliVolt per LSB => 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096 */
#define VBAT_MV_PER_LSB (0.73242188F)
/** Voltage divider value => 1.5M + 1M voltage divider on VBAT = (1.5M / (1M + 1.5M)) */
#define VBAT_DIVIDER (0.4F)
/** Compensation factor for the VBAT divider */
#define VBAT_DIVIDER_COMP (1.73)
/** Fixed calculation of milliVolt from compensation value */
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

/**@brief Structure containing LoRaWan parameters, needed for lmh_init()
 */
static lmh_param_t lora_param_init = {LORAWAN_ADR_ON, LORAWAN_DATERATE, LORAWAN_PUBLIC_NETWORK, JOINREQ_NBTRIALS, LORAWAN_TX_POWER, LORAWAN_DUTYCYCLE_OFF};

// Foward declaration
static void lorawan_has_joined_handler(void);
static void lorawan_rx_handler(lmh_app_data_t *app_data);
static void lorawan_confirm_class_handler(DeviceClass_t Class);
static void send_lora_frame(void);
static void bme680_get(void);
void init_bme680(void);
void initReadVBAT(void);
float readVBAT (void);
uint8_t mvToLoRaWanBattVal(float mvolts);

/**@brief Structure containing LoRaWan callback functions, needed for lmh_init()
*/
static lmh_callback_t lora_callbacks = {BoardGetBatteryLevel, BoardGetUniqueId, BoardGetRandomSeed,
                                        lorawan_rx_handler, lorawan_has_joined_handler, lorawan_confirm_class_handler};

//OTAA keys
uint8_t nodeDeviceEUI[8] = {0x1c, 0xf6, 0x8b, 0x8d, 0x8b, 0x0b, 0x27, 0x37}; // Windmühle
uint8_t nodeAppEUI[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t nodeAppKey[16] = {0xfd, 0xd7, 0x8d, 0xbc, 0x18, 0x72, 0x8c, 0x46, 0x81, 0xc3, 0x1c, 0x40, 0x5c, 0x77, 0x6c, 0x0c};

// Private defination
#define LORAWAN_APP_DATA_BUFF_SIZE 64                                         /**< buffer size of the data to be transmitted. */
#define LORAWAN_APP_INTERVAL 600000                                            /**< Defines for user timer, the application data transmission interval. 20s, value in [ms]. */
static uint8_t m_lora_app_data_buffer[LORAWAN_APP_DATA_BUFF_SIZE];            //< Lora user application data buffer.
static lmh_app_data_t m_lora_app_data = {m_lora_app_data_buffer, 0, 0, 0, 0}; //< Lora user application data structure.

TimerEvent_t appTimer;
static uint32_t timers_init(void);
static uint32_t count = 0;
static uint32_t count_fail = 0;

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Initialize LoRa chip.
  lora_rak4630_init();

  // Initialize Debugger
  //Serial.begin(115200);
  // while (!Serial)
  // {
  //   delay(10);
  // }

  delay(10);

  initReadVBAT();

  readVBAT();

  Serial.println("=====================================");
  Serial.println("Welcome to RAK4630 LoRaWan!!!");
  Serial.println("Type: OTAA");

// #if defined(REGION_AS923)
//   Serial.println("Region: AS923");
// #elif defined(REGION_AU915)
//   Serial.println("Region: AU915");
// #elif defined(REGION_CN470)
//   Serial.println("Region: CN470");
// #elif defined(REGION_CN779)
//   Serial.println("Region: CN779");
// #elif defined(REGION_EU433)
//   Serial.println("Region: EU433");
// #elif defined(REGION_IN865)
//   Serial.println("Region: IN865");
// #elif defined(REGION_EU868)
//   Serial.println("Region: EU868");
// #elif defined(REGION_KR920)
//   Serial.println("Region: KR920");
// #elif defined(REGION_US915)
//   Serial.println("Region: US915");
// #elif defined(REGION_US915_HYBRID)
//   Serial.println("Region: US915_HYBRID");
// #else
//   Serial.println("Please define a region in the compiler options.");
// #endif
//   Serial.println("=====================================");

  //creat a user timer to send data to server period
  uint32_t err_code;
  err_code = timers_init();
  if (err_code != 0)
  {
    Serial.printf("timers_init failed - %d\n", err_code);
  }

  // Setup the EUIs and Keys
  lmh_setDevEui(nodeDeviceEUI);
  lmh_setAppEui(nodeAppEUI);
  lmh_setAppKey(nodeAppKey);

  // Initialize LoRaWan
  err_code = lmh_init(&lora_callbacks, lora_param_init, doOTAA, gCurrentClass, LORAMAC_REGION_EU868);
  if (err_code != 0)
  {
    Serial.printf("lmh_init failed - %d\n", err_code);
  }

  // shtc3 init
  Serial.println("SHTC3 - Basic Readings"); // Title
  Wire.begin();
  
/* bme680 init */
  init_bme680();

  Serial.println("Waiting for 5 seconds so you can read this info ^^^");
  delay(5000);

  // LPS22HB init
  // if (!BARO.begin()) {
  //    Serial.println("Failed to initialize pressure sensor!");
  //   while (1);
  // }

  // Start Join procedure
  lmh_join();
}

void loop()
{
  // Get a raw ADC reading
  BoardGetBatteryLevel();

  // Handle Radio events
  Radio.IrqProcess();
}

/**@brief LoRa function for handling HasJoined event.
 */
void lorawan_has_joined_handler(void)
{
  Serial.println("OTAA Mode, joined LoRa Network of Out of Band!");

  lmh_error_status ret = lmh_class_request(gCurrentClass);
  if (ret == LMH_SUCCESS)
  {
    delay(1000);
    TimerSetValue(&appTimer, LORAWAN_APP_INTERVAL);
    TimerStart(&appTimer);
  }
}

/**@brief Function for handling LoRaWan received data from Gateway
 *
 * @param[in] app_data  Pointer to rx data
 */
void lorawan_rx_handler(lmh_app_data_t *app_data)
{
  Serial.printf("LoRa Packet received on port %d, size:%d, rssi:%d, snr:%d, data:%s\n",
                 app_data->port, app_data->buffsize, app_data->rssi, app_data->snr, app_data->buffer);
}

void lorawan_confirm_class_handler(DeviceClass_t Class)
{
  Serial.printf("switch to class %c done\n", "ABC"[Class]);
  // Informs the server that switch has occurred ASAP
  m_lora_app_data.buffsize = 0;
  m_lora_app_data.port = gAppPort;
  lmh_send(&m_lora_app_data, gCurrentConfirm);
}

void send_lora_frame(void)
{
  if (lmh_join_status_get() != LMH_SET)
  {
    //Not joined, try again later
    return;
  }
   if (!bme.performReading()) {
    return;
  }
  bme680_get();

  lmh_error_status error = lmh_send(&m_lora_app_data, gCurrentConfirm);
  if (error == LMH_SUCCESS)
  {
    count++;
    Serial.printf("lmh_send ok count %d\n", count);
  }
  else
  {
    count_fail++;
    Serial.printf("lmh_send fail count %d\n", count_fail);
  }
}

/**@brief Function for handling user timerout event.
 */
void tx_lora_periodic_handler(void)
{
  TimerSetValue(&appTimer, LORAWAN_APP_INTERVAL);
  TimerStart(&appTimer);
  // Serial.println("Sending ENV data now...");
  send_lora_frame();
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
uint32_t timers_init(void)
{
  TimerInit(&appTimer, tx_lora_periodic_handler);
  return 0;
}

void   bme680_get()
{
  gAppPort = 3;
  char payload[64];
  uint32_t i = 0;
  memset(m_lora_app_data.buffer, 0, LORAWAN_APP_DATA_BUFF_SIZE);
  m_lora_app_data.port = gAppPort;
  double temp = bme.temperature;
  double pres = bme.pressure / 100.0;
  double hum = bme.humidity;
  uint32_t gas = bme.gas_resistance;
  double lux = readVBAT()/1000;

  sprintf(payload, "%.2f:%.2f:%.2f:%.2f", temp, hum, pres, lux);
  uint32_t h = strlen(payload);

  for (i = 0; i < h; i++)
  {
    m_lora_app_data.buffer[i] = (unsigned char)payload[i];
  }

  m_lora_app_data.buffsize = i;
}

void init_bme680(void)
{
  Wire.begin();

  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME680 sensor, check wiring!");
    return;
  }

  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms
}

void initReadVBAT(void)
{
	// Set the analog reference to 3.0V (default = 3.6V)
	analogReference(AR_INTERNAL_3_0);
	// Set the resolution to 12-bit (0..4095)
	analogReadResolution(12); // Can be 8, 10, 12 or 14
	// Let the ADC settle
	delay(1);
}

float readVBAT(void)
{
  float raw;
  // Get the raw 12-bit, 0..3000mV ADC value
  raw = analogRead(PIN_VBAT);

  return raw * REAL_VBAT_MV_PER_LSB;
}
