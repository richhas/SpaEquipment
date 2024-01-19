// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#pragma once

#include <Arduino.h>
#include <RTC.h>
#include <ArduinoGraphics.h>
#include <Arduino_LED_Matrix.h>
#include <time.h>
#include <vector>

using namespace std;

#include "Common.h"
#include "clilib.h"
#include "AnsiTerm.h"
#include "Eventing.h"
#include <WiFi.h>
#include <WiFiS3.h>


//* Persistant storage partitions (8k max)
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


//** All Task Types used

//* Admin console Task that can be redirected to any Stream
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

//* Task to maintain the UNO WiFi LED display; presenting a text string
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

    virtual void setup() override;
    virtual void loop() override;

    void PutString(char* Text);
};


//* Task that implements an AP; allowing a user to select and configure that SSID/Password
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
    void GetNetworkConfig(const char*& SSID, const char*& Password);
    void EraseConfig();
    void DumpConfig(Stream& ToStream);
    void SetConfig(const char* SSID, const char* NetPassword, const char* AdminPassword);
    
    virtual void setup() override;
    virtual void loop() override;

private:
    static String GetValueByKey(String& Data, String Key);
    static void ParsePostData(String& PostData, String& Network, String& WifiPassword, String& TelnetAdminPassword);
    static void PrintWiFiStatus(Stream& ToStream);
};

//* System Logger
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

//* System Instance Record - In persistant storage
#pragma pack(push, 1)
struct BootRecord
{
    uint32_t        BootCount;
};
#pragma pack(pop)

static_assert(PS_BootRecordBlkSize >= sizeof(FlashStore<BootRecord, PS_BootRecordBase>));


//** Network related definitions
class NetworkService;
class NetworkClient;

//* Main Network Task - maintains WiFi connection and hosts outgoing Client and incoming Services
class NetworkTask : public ArduinoTask
{
public:
    NetworkTask() {}
    ~NetworkTask() { $FailFast(); }

    void Begin(const char* SSID, const char* NetPassword);

    virtual void setup() override;
    virtual void loop() override;

private:
    static String& AppendHEX(uint8_t Byte, String& AddTo);
    static String MacToString(uint8_t* Mac);

    friend class NetworkService;
    friend class NetworkClient;

    void RegisterService(shared_ptr<NetworkService> ToRegister);
    void RegisterClient(shared_ptr<NetworkClient> ToRegister);

    void UnregisterService(shared_ptr<NetworkService> ToRegister);
    void UnregisterClient(shared_ptr<NetworkClient> ToRegister);

private:
    const char*     _ssid;
    const char*     _networkPassword;

    vector<shared_ptr<NetworkService>>    _registeredServices;
    vector<shared_ptr<NetworkClient>>     _registeredClients;
};

//* Base class for all incoming hosted services
class NetworkService : public ArduinoTask, public std::enable_shared_from_this<NetworkService>
{
public:
    class Client : public ArduinoTask, public std::enable_shared_from_this<NetworkService::Client>
    {
    public:
        Client() = delete;
        Client(shared_ptr<NetworkService> Parent, WiFiClient Client);
        ~Client();

        virtual void Begin() = 0;
        virtual void End() = 0;
        virtual void setup() override = 0;
        virtual void loop() override = 0;

        inline WiFiClient& GetClient() { return _client; }

    protected:
        WiFiClient                  _client;
        shared_ptr<NetworkService>  _parentService;
    };

public:
    NetworkService();
    ~NetworkService();

    void Begin(int Port);
    void End();
    virtual void setup() override;
    virtual void loop() override;
    inline int GetNumberOfClients() { return _registeredClients.size(); }

protected:
    virtual shared_ptr<Client> CreateClient(shared_ptr<NetworkService> Me, WiFiClient ClientToUse) = 0;

protected:
    WiFiServer      _server;

private:
    enum class State
    {
        StartServer,
        Connected,
    };

    bool            _firstTime;
    State           _state;
    Timer           _delayTimer;
    int             _status;
    int             _tcpPort;
    vector<shared_ptr<Client>> _registeredClients;
};

//* Outgoing client session base class implementation
class NetworkClient : public ArduinoTask, public std::enable_shared_from_this<NetworkClient>
{
public:
    NetworkClient();
    ~NetworkClient();
    void Begin();
    virtual void setup() override final;
    virtual void loop() override final;

protected:
    virtual void OnNetConnected() = 0;
    virtual void OnNetDisconnected() = 0;
    virtual void OnLoop() = 0;

private:
    enum class State
    {
        WaitForWiFi,
        DelayForWiFi,
        Connected,
    };

    shared_ptr<NetworkTask>     _parent;
    bool                        _firstTime;
    State                       _state;
    Timer                       _delayTimer;
    int                         _status;
};

class TcpClient : public NetworkClient
{
public:
    TcpClient();
    ~TcpClient();
};

class UdpClient : public NetworkClient
{
public:
    UdpClient();
    ~UdpClient();
};



//* Specific TELNET service bound to instances of ConsoleTask per session
class TelnetServer : public NetworkService
{
private:
    class Client : public NetworkService::Client
    {
    public:
        Client() = delete;
        Client(shared_ptr<NetworkService> Parent, WiFiClient Client);
        ~Client();
        virtual void Begin();
        virtual void End();
        virtual void setup() override;
        virtual void loop() override;

    private:
        shared_ptr<ConsoleTask> _console;
    };

public:
    TelnetServer();
    ~TelnetServer();
    virtual shared_ptr<NetworkService::Client> CreateClient(shared_ptr<NetworkService> Parent, WiFiClient ClientToUse);
};


//** Cross module references
extern class ConsoleTask        consoleTask;
extern class LedMatrixTask      matrixTask;
extern class WiFiJoinApTask     wifiJoinApTask;
extern class Logger             logger;
extern class FlashStore<BootRecord, PS_BootRecordBase> bootRecord;
extern class NetworkTask        network;
extern shared_ptr<TelnetServer> telnetServer;
