// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// WiFiJoinApTask definitions

#pragma once
#include <memory.h>
#include <string.h>
#include <WiFi.h>
#include <WiFiS3.h>

#include "SpaHeaterCntl.hpp"
#include "WiFiJoinApTask.hpp"


//* Task that implements an AP; allowing a user to select and configure that SSID/Password
class WiFiJoinApTask : public ArduinoTask
{
private:
#pragma pack(push, 1)
    struct Config
    {
        uint8_t _version;
        static const int CurrentVersion = 1;

        // Version 1 and > fields
        char _ssid[32];            // selected WiFi network - zero if not configured
        char _networkPassword[32]; // zero if not set
        char _adminPassword[32];   // Telnet admin password - zero if not set
    };
#pragma pack(pop)

    FlashStore<Config, PS_NetworkConfigBase>
        _config;
    Stream &_traceOutput;
    bool _isInSleepState;
    WiFiServer _server;
    WiFiClient _client;
    String _currentLine;
    String _apNetName;
    String _apNetPassword;

    static_assert(PS_NetworkConfigBlkSize >= sizeof(FlashStore<Config, PS_NetworkConfigBase>));

public:
    WiFiJoinApTask() = delete;
    WiFiJoinApTask(Stream &TraceOutput, const char *ApNetName, const char *ApNetPassword);
    ~WiFiJoinApTask();

    bool IsCompleted() { return _isInSleepState; }
    bool IsConfigured() { return _config.IsValid(); }
    void GetNetworkConfig(const char *&SSID, const char *&Password);
    void EraseConfig();
    void DumpConfig(Stream &ToStream);
    void SetConfig(const char *SSID, const char *NetPassword, const char *AdminPassword);

    virtual void setup() override;
    virtual void loop() override;

private:
    static String GetValueByKey(String &Data, String Key);
    static void ParsePostData(String &PostData, String &Network, String &WifiPassword, String &TelnetAdminPassword);
    static void PrintWiFiStatus(Stream &ToStream);
};

//** Cross module references
extern class WiFiJoinApTask wifiJoinApTask;
