// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#pragma once

#include <Arduino.h>
#include <ArduinoGraphics.h>
#include <Arduino_LED_Matrix.h>

#include "Common.h"
#include "clilib.h"
#include "AnsiTerm.h"
#include "Eventing.h"
#include <WiFi.h>
#include <WiFiS3.h>


// System States for admin and led matrix display
const uint8_t     SS_Startup01 = 0;         // consider an enum

// Persistant storage partitions (8k max)
const uint16_t    PS_NetworkConfigBase = 0;
const uint16_t    PS_TempSensorsConfigBase = PS_NetworkConfigBase + 1024;
const uint16_t    PS_MQTTBrokerConfigBase = PS_TempSensorsConfigBase + 256;
const uint16_t    PS_BoilerConfigBase = PS_MQTTBrokerConfigBase + 256;
const uint16_t    PS_TotalConfigSize = PS_BoilerConfigBase + 256;

const uint16_t    PS_TotalDiagStoreSize = (8 * 1024) - PS_TotalConfigSize;
const uint16_t    PS_DiagStoreBase = PS_TotalDiagStoreSize;


//* All Task Types used

// Admin console that can be redirected to any Stream
class ConsoleTask : public ArduinoTask
{
private:
    Stream&       _output;
    CmdLine       _cmdLine;

public:
    ConsoleTask() = delete;
    ConsoleTask(Stream& Output);
    ~ConsoleTask();

    virtual void setup();
    virtual void loop();
};

// Task to maintain the UNO WiFi LED display; presenting a text string
class LedMatrixTask : public ArduinoTask
{
private:
    Stream&           _output;
    uint16_t          _scrollTimeInMS;
    ArduinoLEDMatrix  _matrix;
    String            _text;
    bool              _doScrollDisplay;

public:
    LedMatrixTask() = delete;
    LedMatrixTask(Stream& Output, uint8_t ScrollTimeInMS);
    ~LedMatrixTask();

    virtual void setup();
    virtual void loop();

    void PutString(char* Text);
};


// Task that implements an AP; allowing a user to select and configure that network
class WiFiJoinApTask : public ArduinoTask
{
private:
    #pragma pack(push, 1) 
    struct Config
    {
      uint8_t       _version;
      const int         CurrentVersion = 1;

      // Version 1 and > fields
      char          _ssid[32];                        // selected WiFi network - zero if not configured
      char          _networkPassword[32];             // zero if not set
      char          _adminPassword[32];               // Telnet admin password - zero if not set
    };
    #pragma pack(pop)

    FlashStore<Config, PS_NetworkConfigBase>  _config;
    Stream&             _traceOutput;
    WiFiServer          _server;
    WiFiClient          _client;
    String              _currentLine;

public:
    WiFiJoinApTask() = delete;
    WiFiJoinApTask(Stream& TraceOutput);
    ~WiFiJoinApTask();

    bool IsConfigured() { return _config.IsValid(); }
    void GetNetworkConfig(String& SSID, String& Password);
    void EraseConfig();
    void DumpConfig(Stream& ToStream);

    virtual void setup();
    virtual void loop();
};


// Cross module references
extern class ConsoleTask consoleTask;
extern class LedMatrixTask matrixTask;
