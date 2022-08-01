
#include <Arduino.h>

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <U8x8lib.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "secrets/lorawan_keys.h"

#define REVISION 3
#define DEVID 1
#define FPORT REVISION

struct __attribute__((__packed__)) databytes_struct {
    uint8_t revision;
    uint8_t device_id;
    int16_t temperature;
};
typedef databytes_struct databytes_t;

// the OLED used
// ux8x(clock, data, reset)
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(15, 4, 16);

#define ONE_WIRE_BUS 13 // GPIO 13

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress temperatureSensor;

static databytes_t mydata = {REVISION, DEVID, 0};

static osjob_t sendjob;
static osjob_t triggerjob;
static osjob_t readjob;

void do_temp_read(osjob_t *j);

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60 * 5;

#define SPI_LORA_CLK 5
#define SPI_LORA_MISO 19
#define SPI_LORA_MOSI 27
#define LORA_RST 14
#define LORA_CS 18
#define LORA_DIO_0 26
#define LORA_DIO_1 35
#define LORA_DIO_2 34

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = LORA_CS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LORA_RST,
    .dio = {LORA_DIO_0, LORA_DIO_1, LORA_DIO_2},
};

// Code for the OTAA - Start
void os_getArtEui(u1_t *buf)
{
    Serial.println(F("Get APPEUI"));
    memcpy_P(buf, APPEUI, 8);
}

void os_getDevEui(u1_t *buf)
{
    Serial.println(F("Get DEVEUI"));
    memcpy_P(buf, DEVEUI, 8);
}

void os_getDevKey(u1_t *buf)
{
    Serial.println(F("Get APPKEY"));
    memcpy_P(buf, APPKEY, 16);
}
// Code for OTAA - Stop

// Code for the 1-wire - Start
void triggerReadTemp()
{
    sensors.requestTemperatures();
    Serial.println(F("Trigger Temps"));
}

void readTemps()
{
     mydata.temperature = sensors.getTemp(temperatureSensor);
    Serial.printf("Read Temps = %04x\n", mydata.temperature);

    float temperature = sensors.rawToCelsius(mydata.temperature);
    char buf[16];
    sprintf(buf, "=%0.2f%c%d", temperature,'a'+mydata.device_id,mydata.revision);
    u8x8.drawString(0, 0, buf);
}

void do_temp_trigger(osjob_t *j)
{
    triggerReadTemp();

    // Scehdule the actual read in 2 seconds
    os_setTimedCallback(&readjob, os_getTime() + sec2osticks(2), do_temp_read);
}

void do_temp_read(osjob_t *j)
{
    readTemps();

    os_setTimedCallback(&triggerjob, os_getTime() + sec2osticks(28), do_temp_trigger);
}
// Code for the 1-wire - Stop

void do_send(osjob_t *j)
{
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND)
    {
        Serial.println(F("OP_TXRXPEND, not sending"));
    }
    else
    {
        // Prepare upstream data transmission at the next possible time.
        LMIC_setTxData2(FPORT, (xref2u1_t)&mydata, sizeof(mydata), 0);
        Serial.println(F("Packet queued"));
        digitalWrite(LED_BUILTIN, HIGH);
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void onEvent(ev_t ev)
{
    Serial.print(os_getTime());
    Serial.print(": ");
    switch (ev)
    {
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
    case EV_JOINED:
        Serial.println(F("EV_JOINED"));

        // Disable link check validation (automatically enabled
        // during join, but not supported by TTN at this time).
        LMIC_setLinkCheckMode(0);
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
        break;
    case EV_TXCOMPLETE:
        Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
        if (LMIC.txrxFlags & TXRX_ACK)
            Serial.println(F("Received ack"));
        if (LMIC.dataLen)
        {
            Serial.println(F("Received "));
            Serial.println(LMIC.dataLen);
            Serial.println(F(" bytes of payload"));
        }
        // Schedule next transmission
        os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
        digitalWrite(LED_BUILTIN, LOW);
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
    case EV_TXSTART:
        Serial.println(F("EV_TXSTART"));
        break;
    case EV_TXCANCELED:
        Serial.println(F("EV_TXCANCELED"));
        break;
    case EV_RXSTART:
        /* do not print anything -- it wrecks timing */
        break;
    case EV_JOIN_TXCOMPLETE:
        Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
        break;

    default:
        Serial.print(F("Unknown event: "));
        Serial.println((unsigned)ev);
        break;
    }
}

void init_one_wire()
{
    sensors.begin();

    sensors.getAddress(temperatureSensor, 0);
    sensors.setResolution(temperatureSensor, 10); // 0.25 degrees increments
}

void setup()
{
    Serial.begin(115200);
    Serial.println(F("OTAA Node v0.2"));

    u8x8.begin();
    u8x8.setFont(u8x8_font_courB18_2x3_r);

    init_one_wire();

    // Trigger the initial temperature reading
    triggerReadTemp();

    pinMode(LED_BUILTIN, OUTPUT);
    for (int i = 0; i < 10; i++)
    {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        delay(50);
    }

    readTemps();

    SPI.begin(SPI_LORA_CLK, SPI_LORA_MISO, SPI_LORA_MOSI);

    // LMIC init
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // Start job (sending automatically starts OTAA too)
    do_send(&sendjob);
    do_temp_trigger(&triggerjob);
}

void loop()
{
    os_runloop_once();
}



/*
  Payload format in TTN:

  function decodeUplink(input) {

  var data = {};
  var warnings = [];
  var pos = 0;
  
  switch (input.fPort) {
    case 1:
      warnings.push("retired");
      break;
    case 2:
      data.items = input.bytes.length / 2
      var temp = []
    
      for (i = 0, pos = 0; i < data.items; i++, pos += 2) {
        var raw = input.bytes[pos+1]*256+input.bytes[pos];
        var temperature = raw * 0.0078125;
        temp.push(temperature)
      }
      
      data.temperature = temp;
      
      break;
    case 3:
      pos = 0;
      data.revision = input.bytes[pos++];
      data.devid = input.bytes[pos++];
      data.items = 1;
      var raw2 = input.bytes[pos++]+input.bytes[pos++]*256;
      data.temperature = raw2 * 0.0078125;
      break;
    default:
      warnings.push("unkown fport value");
      break;
  }
  
  return {
    data: data,
    warnings: warnings
  };
}


*/
