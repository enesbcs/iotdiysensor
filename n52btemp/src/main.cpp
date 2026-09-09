#include <Arduino.h>
#include <bluefruit.h>
#include <Adafruit_BME280.h>
#include <math.h>

#define PIN_LDO13        13  // P0.13 onboard 3.3V rail enable, LOW = rail off
#define PIN_RED_LED      15  // P0.15 SuperMini red LED, active-HIGH, LOW = off
#define PIN_SDA          17  // P0.17 I2C SDA
#define PIN_SCL          20  // P0.20 I2C SCL
#define PIN_SENS_PWR     32  // P1.00 BME280 VIN switch, HIGH = on
#define BME280_ADDR      0x76

#define ADV_INTERVAL_MS    500
#define ADV_DATA_SEC       3
#define SLEEP_SEC          180
#define SENSOR_WARMUP_MS   12

#define WDT_TIMEOUT_SEC    30  // CPU-active watchdog; paused while sleeping (SLEEP=Pause)

static Adafruit_BME280 bme;

static uint8_t advPayload[32];
static uint8_t advPayloadLen = 0;

static uint16_t readVddMv(void) {
  volatile int16_t sample = 0;

  NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_12bit;
  NRF_SAADC->ENABLE = (SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos);
  NRF_SAADC->CH[0].CONFIG =
      ((SAADC_CH_CONFIG_RESP_Bypass   << SAADC_CH_CONFIG_RESP_Pos)   & SAADC_CH_CONFIG_RESP_Msk)   |
      ((SAADC_CH_CONFIG_RESN_Bypass   << SAADC_CH_CONFIG_RESN_Pos)   & SAADC_CH_CONFIG_RESN_Msk)   |
      ((SAADC_CH_CONFIG_GAIN_Gain1_6  << SAADC_CH_CONFIG_GAIN_Pos)   & SAADC_CH_CONFIG_GAIN_Msk)   |
      ((SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) & SAADC_CH_CONFIG_REFSEL_Msk) |
      ((SAADC_CH_CONFIG_TACQ_10us     << SAADC_CH_CONFIG_TACQ_Pos)   & SAADC_CH_CONFIG_TACQ_Msk)   |
      ((SAADC_CH_CONFIG_MODE_SE       << SAADC_CH_CONFIG_MODE_Pos)   & SAADC_CH_CONFIG_MODE_Msk)   |
      ((SAADC_CH_CONFIG_BURST_Disabled<< SAADC_CH_CONFIG_BURST_Pos)  & SAADC_CH_CONFIG_BURST_Msk);
  NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;
  NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_VDD;
  NRF_SAADC->RESULT.PTR   = (uint32_t)&sample;
  NRF_SAADC->RESULT.MAXCNT = 1;

  NRF_SAADC->TASKS_START = 1;
  while (!NRF_SAADC->EVENTS_STARTED) {}
  NRF_SAADC->EVENTS_STARTED = 0;
  NRF_SAADC->TASKS_SAMPLE = 1;
  while (!NRF_SAADC->EVENTS_END) {}
  NRF_SAADC->EVENTS_END = 0;
  NRF_SAADC->TASKS_STOP = 1;
  while (!NRF_SAADC->EVENTS_STOPPED) {}
  NRF_SAADC->EVENTS_STOPPED = 0;
  NRF_SAADC->ENABLE = (SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos);

  if (sample < 0) sample = 0;
  return (uint16_t)(((uint32_t)sample * 3600UL) / 4096UL);
}

static uint8_t batteryPercent(uint16_t mv) {
  static const struct { uint16_t mv; uint8_t pct; } tbl[] = {
    {3200,100}, {3050,95}, {2950,90}, {2900,80}, {2850,70},
    {2800,60},  {2750,45}, {2700,30}, {2600,15}, {2400,5}, {2000,0}
  };
  uint8_t n = sizeof(tbl) / sizeof(tbl[0]);
  if (mv >= tbl[0].mv) return 100;
  for (uint8_t i = 0; i < n - 1; i++) {
    if (mv <= tbl[i].mv && mv > tbl[i + 1].mv) {
      uint16_t span   = tbl[i].mv - tbl[i + 1].mv;
      uint16_t below  = tbl[i].mv - mv;
      uint8_t  pcSpan = tbl[i].pct - tbl[i + 1].pct;
      return (uint8_t)(tbl[i].pct - (((uint32_t)below * pcSpan) + span / 2) / span);
    }
  }
  return 0;
}

static bool readBme(float* tempC, float* humPct, float* pressHpa) {
  bool ok = bme.begin(BME280_ADDR);
  if (!ok) return false;
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::FILTER_OFF);
  if (!bme.takeForcedMeasurement()) return false;
  *tempC    = bme.readTemperature();
  *humPct   = bme.readHumidity();
  *pressHpa = bme.readPressure() / 100.0f;
  return true;
}

static uint8_t buildBthome(uint8_t* out, bool sensorOk,
                           float tempC, float humPct, float pressHpa,
                           uint16_t vddMv, uint8_t battPct) {
  uint8_t n = 0;
  out[n++] = 0xD2;  // BTHome 16-bit UUID, LE
  out[n++] = 0xFC;
  out[n++] = 0x40;  // BTHome v2, unencrypted, regular interval

  out[n++] = 0x01;  // battery u8, %
  out[n++] = battPct;

  if (sensorOk) {
    int16_t t = (int16_t)roundf(tempC * 100.0f);
    out[n++] = 0x02;  // temperature s16, 0.01 C
    out[n++] = (uint8_t)(t & 0xFF);
    out[n++] = (uint8_t)((t >> 8) & 0xFF);

    uint16_t h = (uint16_t)roundf(humPct * 100.0f);
    out[n++] = 0x03;  // humidity u16, 0.01 %
    out[n++] = (uint8_t)(h & 0xFF);
    out[n++] = (uint8_t)((h >> 8) & 0xFF);

    uint32_t p = (uint32_t)roundf(pressHpa * 100.0f);
    out[n++] = 0x04;  // pressure u24, 0.01 hPa
    out[n++] = (uint8_t)(p & 0xFF);
    out[n++] = (uint8_t)((p >> 8) & 0xFF);
    out[n++] = (uint8_t)((p >> 16) & 0xFF);
  }

  out[n++] = 0x0C;  // voltage u16, 0.001 V
  out[n++] = (uint8_t)(vddMv & 0xFF);
  out[n++] = (uint8_t)((vddMv >> 8) & 0xFF);

  return n;
}

static void prepareSleepPins(void) {
  Wire.end();
  NRF_TWIM0->ENABLE = 0;  // TWI modul leallitasa (Wire a TWIM0-t hasznalja)
  pinMode(PIN_SDA, INPUT);
  pinMode(PIN_SCL, INPUT);
  digitalWrite(PIN_SENS_PWR, LOW);
  pinMode(PIN_RED_LED, OUTPUT);
  digitalWrite(PIN_RED_LED, LOW);
}

void doCycle(void) {
  uint16_t vddMv = readVddMv();

  digitalWrite(PIN_SENS_PWR, HIGH);
  delay(SENSOR_WARMUP_MS);
  Wire.begin();

  float tempC = 0.0f, humPct = 0.0f, pressHpa = 0.0f;
  bool sensorOk = readBme(&tempC, &humPct, &pressHpa);

  prepareSleepPins();

  advPayloadLen = buildBthome(advPayload, sensorOk, tempC, humPct, pressHpa,
                              vddMv, batteryPercent(vddMv));

  Bluefruit.Advertising.clearData();
  Bluefruit.Advertising.addFlags(0x06);
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_SERVICE_DATA, advPayload, advPayloadLen);
  Bluefruit.Advertising.setFastTimeout(ADV_DATA_SEC);

  bool advOk = false;
  for (int retry = 0; retry < 3 && !advOk; retry++) {
    advOk = Bluefruit.Advertising.start(ADV_DATA_SEC);
    if (!advOk) delay(100);
  }

  while (Bluefruit.Advertising.isRunning()) delay(10);
}

// nRF52840 WDT - szerint az S140 SDS: Open periferia, regi-szinten hasznalhato SD mellett.
// CONFIG=0: SLEEP=Pause (180s alvas alatt nem szamit), HALT=Pause (debugger nelkul nem resetel).
static void wdtInit(void) {
  NRF_WDT->CONFIG = 0;
  NRF_WDT->CRV = (uint32_t)(WDT_TIMEOUT_SEC * 32768UL);
  NRF_WDT->RREN = WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
  NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}

static inline void wdtFeed(void) { NRF_WDT->RR[0] = WDT_RR_RR_Reload; }

void setup() {
  wdtInit();

  pinMode(PIN_LDO13, OUTPUT);
  digitalWrite(PIN_LDO13, LOW);

  digitalWrite(PIN_SENS_PWR, LOW);
  pinMode(PIN_SENS_PWR, OUTPUT);

  Wire.setPins(PIN_SDA, PIN_SCL);

  Bluefruit.begin(1);
  Bluefruit.Advertising.setIntervalMS(ADV_INTERVAL_MS, ADV_INTERVAL_MS);
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
  Bluefruit.setTxPower(8);
}

void loop() {
  wdtFeed();

  doCycle();
  wdtFeed();

  // FreeRTOS idle task = System ON low-power; internal RTC ebreszti a CPU-t 180s utan.
  // A WDT SLEEP=Pause miatt ebrenlet alatt nem szamit, a 30s-os CRV csak aktív spinre resz.
  delay((uint32_t)SLEEP_SEC * 1000UL);
}