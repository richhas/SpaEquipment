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
    const uint16_t    PS_TempSensorsConfigBlkSize = 128;
const uint16_t        PS_MQTTBrokerConfigBase = PS_TempSensorsConfigBase + PS_TempSensorsConfigBlkSize;
    const uint16_t    PS_MQTTBrokerConfigBlkSize = 256;
const uint16_t        PS_BoilerConfigBase = PS_MQTTBrokerConfigBase + PS_MQTTBrokerConfigBlkSize;
    const uint16_t    PS_BoilerConfigBlkSize = 256;
const uint16_t        PS_TotalConfigSize = PS_BoilerConfigBase + PS_BoilerConfigBlkSize;

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

//* Temperture Sensor Config Record - In persistant storage
#pragma pack(push, 1)
struct TempSensorsConfig
{
    static constexpr uint64_t InvalidSensorId = 0xFFFFFFFFFFFFFFFF;

    uint64_t _ambiantTempSensorId;
    uint64_t _boilerInTempSensorId;
    uint64_t _boilerOutTempSensorId;

    static bool IsSensorIdValid(uint64_t SensorId)
    {
        return (SensorId != InvalidSensorId);
    }    
};
#pragma pack(pop)



//** Network related definitions
class NetworkService;
class NetworkClient;

//* Main Network Task - maintains WiFi connection and hosts outgoing Client and incoming Services
class NetworkTask final : public ArduinoTask
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
        virtual void setup() override final     { OnSetup(); }
        virtual void loop() override final      { OnLoop(); }

        inline WiFiClient& GetClient() { return _client; }

    protected:
        virtual void OnSetup()  {};
        virtual void OnLoop()   {};

    protected:
        WiFiClient                  _client;
        shared_ptr<NetworkService>  _parentService;
    };

public:
    NetworkService();
    ~NetworkService();

    void Begin(int Port);
    void End();
    virtual void setup() override final;
    virtual void loop() override final;
    inline int GetNumberOfClients() { return _registeredClients.size(); }

protected:
    virtual shared_ptr<Client> CreateClient(shared_ptr<NetworkService> Me, WiFiClient ClientToUse) = 0;
    virtual void OnSetup() {}
    virtual void OnLoop()  {}

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
//      - derived classes must implement the OnConnected/OnDisconnected/OnLoop/OnSetup methods
class NetworkClient : public ArduinoTask, public std::enable_shared_from_this<NetworkClient>
{
public:
    NetworkClient();
    ~NetworkClient();
    virtual void setup() override final;
    virtual void loop() override final;

    void Begin();
    void End();

protected:
    // Optional overrides for derived classes
    virtual void OnNetConnected() = 0;
    virtual void OnNetDisconnected() = 0;
    virtual void OnLoop() = 0;
    virtual void OnSetup() = 0;

private:
    enum class State
    {
        WaitForWiFi,
        DelayForWiFi,
        Connected,
    };

    bool                        _firstTime;
    State                       _state;
    Timer                       _delayTimer;
    int                         _status;
};

//* Outgoing TCP client session - maintains a single connection to a remote server
class TcpClient : public NetworkClient
{
public:
    TcpClient() = delete;
    TcpClient(IPAddress ServerIP, int ServerPort);
    ~TcpClient();

protected:
    virtual void OnNetConnected() override final;
    virtual void OnNetDisconnected() override final;
    virtual void OnLoop() override final;
    virtual void OnSetup() override final;

protected:
    virtual void OnConnected();
    virtual void OnDisconnected();
    virtual void OnDoProcess();

protected:
    WiFiClient          _client;    

private:
    IPAddress           _serverIP;
    int                 _serverPort;
    bool                _netIsUp;

    enum class State
    {
        WaitForNetUp,
        DelayBeforeConnectAttempt,
        Connected,
    };

    bool                _firstTime;
    State               _state;
    Timer               _reconnectTimer;
};

class UdpClient : public NetworkClient
{
public:
    UdpClient();
    ~UdpClient();
};



//* Specific TELNET service bound to instances of ConsoleTask per session
class TelnetServer final : public NetworkService
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
    
    protected:
        virtual void OnSetup() override;
        virtual void OnLoop() override;

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
extern class FlashStore<TempSensosConfig, PS_TempSensorsConfigBase> tempSensorsConfig;
extern class BoilerControllerTask boilerControllerTask;