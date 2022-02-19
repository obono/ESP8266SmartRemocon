/**
 * ESP8266SmartRemocon
 *
 * Copyright (c) 2022 OBONO
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FS.h>
#include <IRsend.h>
#include <DaikinHeatpumpARC480A14IR.h>
#include <Wire.h>
#include <uri/UriBraces.h>
#include <uri/UriRegex.h>
#include <Adafruit_BMP280.h>

#include "credential.h"

#define BUILD_VERSION       "0.1.0"
#define BUILD_DATETIME      __DATE__ " " __TIME__

#define PIN_IRSEND          12

#define HTTP_OK             200
#define HTTP_NO_CONTENT     204
#define HTTP_BAD_REQUEST    400
#define HTTP_NOT_FOUND      404

#define MIMETYPE_TEXT       "text/plain"
#define MIMETYPE_HTML       "text/html"
#define MIMETYPE_JS         "application/x-javascript"
#define MIMETYPE_CSS        "text/css"
#define MIMETYPE_PNG        "image/png"
#define MIMETYPE_JPEG       "image/jpeg"
#define MIMETYPE_GIF        "image/gif"
#define MIMETYPE_ICO        "image/x-icon"

#define PARAM_VALUE         "value"
#define PARAM_BITS          "bits"
#define PARAM_REPEAT        "repeat"
#define BITS_MAX            64
#define REPEAT_MAX          8

#define INDEX_FSPATH        "/index.html"

#define PARAM_POWER         "power"
#define PARAM_MODE          "mode"
#define PARAM_TEMP          "temp"
#define PARAM_FAN           "fan"
#define PARAM_SWING         "swing"

#define POWER_UNKNOWN       255
#define TEMP_MAX            30
#define TEMP_DEFAULT        24
#define TEMP_MIN            18

/*---------------------------------------------------------------------------*/

PROGMEM static const char *powerChoices[] = {
    "off", "on", NULL
};

PROGMEM static const char *modeChoices[] = {
    "-", "auto", "heat", "cool", "dry", "fan", NULL
};

PROGMEM static const char *fanChoices[] = {
    "auto", "level1", "level2", "level3", "level4", "level5", "silent", NULL
};

PROGMEM static const char *swingChoices[] = {
    "-", "swing", "up", "upmiddle", "middle", "downmiddle", "down", NULL
};

typedef void (IRsend::*IRsendFunc)(uint64_t, uint16_t, uint16_t);

PROGMEM struct {
    const char  *protocol;
    uint8_t     bits;
    uint8_t     repeat;
    IRsendFunc  pFunc;
} const protocolTable[] = {
    { "sony",      kSony20Bits,    kSonyMinRepeat, &IRsend::sendSony        },
    { "panasonic", kPanasonicBits, kNoRepeat,      &IRsend::sendPanasonic64 },
    { "nec",       kNECBits,       kNoRepeat,      &IRsend::sendNEC         },
};

static IRsend                   simpleIRSender(PIN_IRSEND);
static IRSenderBitBang          airConIRSender(PIN_IRSEND);
static HeatpumpIR               *pAirCon = new DaikinHeatpumpARC480A14IR();

static Adafruit_BMP280          *pBmp = new Adafruit_BMP280();

static ESP8266WebServer         httpServer(PORT);
static ESP8266HTTPUpdateServer  httpUpdater;

/*---------------------------------------------------------------------------*/

static void responseOk(void)
{
    httpServer.send(HTTP_OK, F(MIMETYPE_TEXT), F("OK"));
}

static void responseNone(void)
{
    httpServer.send(HTTP_NO_CONTENT);
}

static void responseBadRequest(String message)
{
    httpServer.send(HTTP_BAD_REQUEST, F(MIMETYPE_TEXT), "400 Bad Request\n" + message);
}

/*---------------------------------------------------------------------------*/

static uint64_t hex2dec(String arg)
{
    uint64_t ret = 0;
    for (int i = 0, len = arg.length(); i < len; i++) {
        char c = arg.charAt(i);
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (c >= 'A' && c <= 'F') {
            c -= 'A' - 10;
        } else if (c >= 'a' && c <= 'f') {
            c -= 'a' - 10;
        } else {
            return 0;
        }
        ret = ret << 4UL | c;
    }
    return ret;
}

static bool getSimpleIRParams(uint64_t &value, uint16_t &bits, uint16_t &repeat)
{
    bool ret = false;
    for (int i = 0; i < httpServer.args(); i++) {
        String argName = httpServer.argName(i);
        String arg = httpServer.arg(i);
        argName.toLowerCase();
        if (argName.equals(F(PARAM_VALUE))) value = hex2dec(arg);
        if (argName.equals(F(PARAM_BITS))) bits = arg.toInt();
        if (argName.equals(F(PARAM_REPEAT))) repeat = arg.toInt();
    }
    if (value == 0) {
        responseBadRequest(F(PARAM_VALUE));
    } else if (bits == 0 || bits > BITS_MAX) {
        responseBadRequest(F(PARAM_BITS));
    } else if (repeat > REPEAT_MAX) {
        responseBadRequest(F(PARAM_REPEAT));
    } else {
        ret = true;
    }
    return ret;
}

static String getContentType(String &path)
{
    String ret = F(MIMETYPE_TEXT);
    int dotPos = path.lastIndexOf('.');
    if (dotPos >= 0) {
        String ext = path.substring(dotPos + 1);
        ext.toLowerCase();
        if (ext.equals(F("html"))) ret = F(MIMETYPE_HTML);
        if (ext.equals(F("htm")))  ret = F(MIMETYPE_HTML);
        if (ext.equals(F("js")))   ret = F(MIMETYPE_JS);
        if (ext.equals(F("css")))  ret = F(MIMETYPE_CSS);
        if (ext.equals(F("png")))  ret = F(MIMETYPE_PNG);
        if (ext.equals(F("jpg")))  ret = F(MIMETYPE_JPEG);
        if (ext.equals(F("jpeg"))) ret = F(MIMETYPE_JPEG);
        if (ext.equals(F("gif")))  ret = F(MIMETYPE_GIF);
        if (ext.equals(F("ico")))  ret = F(MIMETYPE_ICO);
    }
    return ret;
}

static bool getParamFromChoices(uint8_t &value, String arg, const char *choices[])
{
    const char *choice;
    for (uint8_t i = 0; choice = (const char *)pgm_read_ptr(&choices[i]); i++) {
        if (arg.equals(choice)) {
            value = i;
            return true;
        }
    }
    return false;
}

/*---------------------------------------------------------------------------*/

static void handleRoot(void)
{
    String fsPath = "/" + httpServer.pathArg(0);
    if (fsPath.length() == 1) fsPath = INDEX_FSPATH;
    if (SPIFFS.exists(fsPath)) {
        File file = SPIFFS.open(fsPath, "r");
        httpServer.streamFile(file, getContentType(fsPath));
        file.close();
    } else {
        handleNotFound();
    }
}

static void handleIRAirCon(void)
{
    uint8_t power = POWER_UNKNOWN;
    uint8_t mode = MODE_AUTO;
    uint8_t temp = TEMP_DEFAULT;
    uint8_t fan = FAN_AUTO;
    uint8_t swing = VDIR_SWING;

    for (int i = 0; i < httpServer.args(); i++) {
        String argName = httpServer.argName(i);
        String arg = httpServer.arg(i);
        argName.toLowerCase();
        arg.toLowerCase();
        if (argName.equals(F(PARAM_POWER))) {
            if (!getParamFromChoices(power, arg, powerChoices)) {
                responseBadRequest(F(PARAM_POWER));
                return;
            }
        } else if (argName.equals(F(PARAM_MODE))) {
            if (!getParamFromChoices(mode, arg, modeChoices) || mode == 0) {
                responseBadRequest(F(PARAM_MODE));
                return;
            }
        } else if (argName.equals(F(PARAM_TEMP))) {
            temp = arg.toInt();
            if (temp < TEMP_MIN || temp > TEMP_MAX) {
                responseBadRequest(F(PARAM_TEMP));
                return;
            }
        } else if (argName.equals(F(PARAM_FAN))) {
            if (!getParamFromChoices(fan, arg, fanChoices)) {
                responseBadRequest(F(PARAM_FAN));
                return;
            }
        } else if (argName.equals(F(PARAM_SWING))) {
            if (!getParamFromChoices(swing, arg, swingChoices) || swing == 0) {
                responseBadRequest(F(PARAM_SWING));
                return;
            }
        }
    }

    if (power == POWER_UNKNOWN) {
        responseBadRequest(F(PARAM_POWER));
        return;
    }
    pAirCon->send(airConIRSender, power, mode, fan, temp, swing, HDIR_AUTO);
    responseOk();
}

static void handleIRGeneric(void)
{
    String protocol = httpServer.pathArg(0);
    protocol.toLowerCase();
    bool isHandled = false;
    for (int i = 0; i < sizeof(protocolTable) / sizeof(protocolTable[0]); i++) {
        if (protocol.equals((const char *)pgm_read_ptr(&protocolTable[i].protocol))) {
            uint64_t value = 0;
            uint16_t bits = pgm_read_byte(&protocolTable[i].bits);
            uint16_t repeat = pgm_read_byte(&protocolTable[i].repeat);
            if (getSimpleIRParams(value, bits, repeat)) {
                IRsendFunc pFunc;
                memcpy_P(&pFunc, &protocolTable[i].pFunc, sizeof(pFunc));
                (simpleIRSender.*pFunc)(value, bits, repeat);
                responseOk();
            }
            isHandled = true;
        }
    }
    if (!isHandled) {
        handleNotFound();
    }
}

static void handleTemp(void)
{
    float temp = pBmp->readTemperature();
    httpServer.send(HTTP_OK, F(MIMETYPE_TEXT), String(temp));
}

static void handleVersion(void)
{
    httpServer.send(HTTP_OK, F(MIMETYPE_TEXT),
            F("Version: " BUILD_VERSION " (" BUILD_DATETIME ")"));
}

static void handleNotFound(void)
{
    String message = "404 Not Found\n";
    message += (httpServer.method() == HTTP_GET) ? "GET " : "POST ";
    message += httpServer.uri();
    httpServer.send(HTTP_NOT_FOUND, F(MIMETYPE_TEXT), message);
}

/*---------------------------------------------------------------------------*/

void setup(void)
{
    Serial.begin(74880);
    Serial.print(F("Version: " BUILD_VERSION " (" BUILD_DATETIME ")\r\n"));

    /*  Connect to Access Point  */
    WiFi.mode(WIFI_STA);
#ifdef STATIC_ADDRESS
    WiFi.config(staticIP, subnet, gateway, dns);
#endif
    WiFi.begin(STA_SSID, STA_PASSWORD);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println(F("WiFi failed, retrying."));
        delay(5000);
        WiFi.begin(STA_SSID, STA_PASSWORD);
    }
    wifi_set_sleep_type(LIGHT_SLEEP_T);
    Serial.print(F("IP Address: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("Mac Address: "));
    Serial.println(WiFi.macAddress());

    /*  Setup IR Sender  */
    pinMode(PIN_IRSEND, OUTPUT);
    digitalWrite(PIN_IRSEND, LOW);
    simpleIRSender.begin();

    /*  Setup Thermometer  */
    Wire.begin();
    if (pBmp->begin(BMP280_ADDRESS_ALT)) {
        pBmp->setSampling(
                Adafruit_BMP280::MODE_NORMAL,       // Operating Mode
                Adafruit_BMP280::SAMPLING_X16,      // Temp. oversampling
                Adafruit_BMP280::SAMPLING_NONE,     // Pressure oversampling
                Adafruit_BMP280::FILTER_X4,         // Filtering
                Adafruit_BMP280::STANDBY_MS_1000);  // Standby time
    } else {
        Serial.println(F("Couldn't find BMP280."));
    }

    /*  Setup HTTP Server  */
    httpUpdater.setup(&httpServer);
    httpServer.on(F("/ir/aircon"), handleIRAirCon);
    httpServer.on(UriRegex("^\\/ir\\/([a-z]+)$"), handleIRGeneric);
    httpServer.on(F("/temp"), handleTemp);
    httpServer.on(F("/version"), handleVersion);
    httpServer.on(UriBraces("/{}"), handleRoot);
    httpServer.onNotFound(handleNotFound);
    httpServer.begin();

    /*  Setup MDNS  */
    MDNS.begin(F(HOSTNAME));
    MDNS.addService(F("http"), F("tcp"), PORT);
    Serial.printf("http://%s.local:%d/\r\n", HOSTNAME, PORT);

    /*  Setup SPIFFS  */
    SPIFFS.begin();
}

void loop(void)
{
    httpServer.handleClient();
    MDNS.update();
    delay(1000);
}
