// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#pragma once

#include <Arduino.h>
#include <RTC.h>
#include <ArduinoGraphics.h>
#include <Arduino_LED_Matrix.h>
#include <time.h>


#include "Common.h"
#include "clilib.h"
#include "AnsiTerm.h"
#include "Eventing.h"
#include <WiFi.h>
#include <WiFiS3.h>


// Persistant storage partitions (8k max)
const uint16_t        PS_NetworkConfigBase = 0;
    const uint16_t    PS_NetworkConfigBlkSize = 256;
const uint16_t        PS_BootRecordBase = PS_NetworkConfigBase + PS_NetworkConfigBlkSize;
    const uint16_t    PS_BootRecordBlkSize = 32;
const uint16_t        PS_TempSensorsConfigBase = PS_BootRecordBase + PS_BootRecordBlkSize;
const uint16_t        PS_MQTTBrokerConfigBase = PS_TempSensorsConfigBase + 256;
const uint16_t        PS_BoilerConfigBase = PS_MQTTBrokerConfigBase + 256;
const uint16_t        PS_TotalConfigSize = PS_BoilerConfigBase + 256;

const uint16_t        PS_TotalDiagStoreSize = (8 * 1024) - PS_TotalConfigSize;
const uint16_t        PS_DiagStoreBase = PS_TotalDiagStoreSize;


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
      uint8_t             _version;
      static const int       CurrentVersion = 1;

      // Version 1 and > fields
      char                _ssid[32];                        // selected WiFi network - zero if not configured
      char                _networkPassword[32];             // zero if not set
      char                _adminPassword[32];               // Telnet admin password - zero if not set
    };
    #pragma pack(pop)

    FlashStore<Config, PS_NetworkConfigBase>  
                        _config;
    Stream&             _traceOutput;
    bool                _isInSleepState;
    WiFiServer          _server;
    WiFiClient          _client;
    String              _currentLine;
    String              _apNetName;
    String              _apNetPassword;

    static_assert(PS_NetworkConfigBlkSize >= sizeof(FlashStore<Config, PS_NetworkConfigBase>));
               
public:
    WiFiJoinApTask() = delete;
    WiFiJoinApTask(Stream& TraceOutput, const char* ApNetName, const char* ApNetPassword);
    ~WiFiJoinApTask();

    bool IsCompleted() { return _isInSleepState; }
    bool IsConfigured() { return _config.IsValid(); }
    void GetNetworkConfig(String& SSID, String& Password);
    void EraseConfig();
    void DumpConfig(Stream& ToStream);
    void SetConfig(const char* SSID, const char* NetPassword, const char* AdminPassword);
    
    virtual void setup();
    virtual void loop();

private:
    static String GetValueByKey(String& Data, String Key);
    static void ParsePostData(String& PostData, String& Network, String& WifiPassword, String& TelnetAdminPassword);
    static void PrintWiFiStatus(Stream& ToStream);
};

class Logger
{
public:
    enum class RecType
    {
        Start,              // Reserved
        Info,
        Warning,
        Critical,
    };

    Logger() = delete;
    Logger(Stream& ToStream);
    ~Logger();

    void Begin(uint32_t InstanceSeq);
    int Printf(Logger::RecType Type, const char* Format, ...);

    static const char* ToString(Logger::RecType From);

private:
    Stream&     _out;
    uint32_t    _logSeq;
    uint32_t    _instanceSeq;
};




// Cross module references
extern class ConsoleTask        consoleTask;
extern class LedMatrixTask      matrixTask;
extern class WiFiJoinApTask     wifiJoinApTask;
extern class Logger             logger;

