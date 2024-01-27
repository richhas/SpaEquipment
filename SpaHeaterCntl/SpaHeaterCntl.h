// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#pragma once

#include <Arduino.h>
#include <RTC.h>
#include <ArduinoGraphics.h>
#include <Arduino_LED_Matrix.h>
#include <time.h>
#include <vector>
#include <Arduino_FreeRTOS.h>


using namespace std;

#include "Common.h"
#include "clilib.h"
#include "AnsiTerm.h"
#include "Eventing.h"
#include <WiFi.h>
#include <WiFiS3.h>
#include <avr/pgmspace.h> // Include the library for code space resident strings




//** Persistant storage partitions (8k max)
constexpr uint16_t PS_NetworkConfigBase = 0;
constexpr uint16_t PS_NetworkConfigBlkSize = 256;
constexpr uint16_t PS_BootRecordBase = PS_NetworkConfigBase + PS_NetworkConfigBlkSize;
constexpr uint16_t PS_BootRecordBlkSize = 32;
constexpr uint16_t PS_TempSensorsConfigBase = PS_BootRecordBase + PS_BootRecordBlkSize;
constexpr uint16_t PS_TempSensorsConfigBlkSize = 128;
constexpr uint16_t PS_MQTTBrokerConfigBase = PS_TempSensorsConfigBase + PS_TempSensorsConfigBlkSize;
constexpr uint16_t PS_MQTTBrokerConfigBlkSize = 256;
constexpr uint16_t PS_BoilerConfigBase = PS_MQTTBrokerConfigBase + PS_MQTTBrokerConfigBlkSize;
constexpr uint16_t PS_BoilerConfigBlkSize = 256;
constexpr uint16_t PS_TotalConfigSize = PS_BoilerConfigBase + PS_BoilerConfigBlkSize;

constexpr uint16_t PS_TotalDiagStoreSize = (8 * 1024) - PS_TotalConfigSize;
constexpr uint16_t PS_DiagStoreBase = PS_TotalDiagStoreSize;


//** Common helpers
/**
 * Converts temperature from Fahrenheit to Celsius.
 *
 * @param F The temperature in Fahrenheit.
 * @return The temperature in Celsius.
 */
constexpr float $FtoC(float F) 
{
    return (F - 32.0) * 5.0 / 9.0;
}

/**
 * Converts temperature from Celsius to Fahrenheit.
 *
 * @param C The temperature in Celsius.
 * @return The temperature in Fahrenheit.
 */
constexpr float $CtoF(float C) 
{
    return (C * 9.0 / 5.0) + 32.0;
}


//** All Task Types used

//* Admin console Task that can be redirected to any Stream
class ConsoleTask : public ArduinoTask
{
public:
    ConsoleTask() = delete;
    ConsoleTask(Stream& StreamToUse);
    ~ConsoleTask();

    virtual void setup();
    virtual void loop();

    void StartBoilerConfig();
    void EndBoilerConfig();
    void StartBoilerControl();
    void EndBoilerControl();

private:
    friend CmdLine::Status ShowBoilerConfigProcessor(Stream&, int, char const**, void*);
    void ShowCurrentBoilerConfig(int PostLineFeedCount = 2);

    friend CmdLine::Status ShowBoilerControlProcessor(Stream&, int, char const**, void*);
    void ShowCurrentBoilerState();

private:
    Stream&         _stream;
    CmdLine         _cmdLine;

    enum class State
    {
        MainMenu,
        EnterBoilerConfig,
        BoilerConfig,
        EnterBoilerControl,
        BoilerControl,
        EnterMainMenu,
    };

    State           _state;
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



/*
 * The BoilerControllerTask class is part of the Maxie HA system 2024 developed by TinyBus.
 * It controls a spa heater and communicates with temperature sensors via a one-wire bus.
 * 
 * The class provides methods for reading temperature values from these sensors.
 * It also includes methods for setting and getting the target temperatures,
 * starting, stopping, and resetting the heater.
 * 
 * Additionally, it provides methods for snapshotting the current state of temperature sensors,
 * target temperatures, and command, which can be used by the foreground task for monitoring and control purposes.
 * 
 * The BoilerControllerThreadEntry function is the entry point for the boiler controller task.
 * It first calls the setup method of the boilerControllerTask object, then enters an infinite loop
 * where it repeatedly calls the loop method of the boilerControllerTask object, waits for 1 second,
 * and toggles the state of the _heaterActiveLedPin.
 * 
 * The BoilerControllerTask class has a constructor that initializes the _ds member variable with _oneWireBusPin,
 * and a destructor that calls the $FailFast function, presumably to halt the system in case of a critical failure.
 * 
 * The ReadTemp method reads the temperature from a sensor with a given ID. If the sensor cannot be selected
 * (presumably because it's not connected or not responding), it sets a fault reason to TempSensorNotFound and returns false.
 * Otherwise, it reads the temperature from the sensor and returns true.
 * 
 * The GetFaultReason method returns the current fault reason. It uses a CriticalSection object to ensure thread safety
 * when accessing shared data. The body of this method is not shown in the provided code.
 */
class BoilerControllerTask final : public ArduinoTask
{
public:
    // Overall current as seen by the boiler controller task
    struct TempertureState
    {
        uint32_t    _sequence;              // incrmented on each state change
        float       _ambiantTemp;
        float       _boilerInTemp;
        float       _boilerOutTemp;
        float       _setPoint;
        float       _hysteresis;
        bool        _heaterOn;
    };
    static void DisplayTemperatureState(Stream& output, const TempertureState& state, const char* prependString = "");

    // Sensor IDs for the ambiant, boiler in, and boiler out temperature sensors
    struct TempSensorIds
    {
        uint64_t    _ambiantTempSensorId;
        uint64_t    _boilerInTempSensorId;
        uint64_t    _boilerOutTempSensorId;
    };
    static void DisplayTempSensorIds(Stream& output, const TempSensorIds& ids, const char* prependString = "");

    // Target temperatures for the boiler (in C) and hysteresis (in C).
    // Effective target temperature (setPoint) and the hard on and off thresholds.
    struct TargetTemps
    {
        float       _setPoint;
        float       _hysteresis;
    };
    static void DisplayTargetTemps(Stream& output, const TargetTemps& temps, const char* prependString = "");

    // States of the heater
    enum class HeaterState
    {
        Halted,         // Heater is halted. No power to heater. Can be moved to Running state by calling Start().
        Running,        // Heater is running. Can be moved to Halted state by calling Stop(). Will move to Faulted state if a fault is detected.
        Faulted,        // Enters this state if a fault is detected. Can be moved to Halted state by calling Reset().
    };
    static constexpr const char* GetHeaterStateDescription(HeaterState state)
    {
        switch (state)
        {
            case HeaterState::Halted:
                    return PSTR("Halted");
            case HeaterState::Running:
                return PSTR("Running");
            case HeaterState::Faulted:
                return PSTR("Faulted");
            default:
                return PSTR("Unknown");
        }
    }
    
    // Reasons for entering the Faulted state
    enum class FaultReason
    {
        None,
        TempSensorNotFound,
        TempSensorReadFailed,
        CoProcCommError,
    };
    static constexpr const char* GetFaultReasonDescription(FaultReason reason)
    {
        switch (reason)
        {
            case FaultReason::None:
                return PSTR("None");
            case FaultReason::TempSensorNotFound:
                return PSTR("TempSensorNotFound");
            case FaultReason::TempSensorReadFailed:
                return PSTR("TempSensorReadFailed");
            case FaultReason::CoProcCommError:
                return PSTR("CoProcCommError");
            default:
                return PSTR("Unknown");
        }
    }

    // Commands that can be issued to the controller
    enum class Command
    {
        Start,
        Stop,
        Reset,
        Idle
    };
    static constexpr const char* GetCommandDescription(Command command) 
    {
        switch (command) 
        {
            case Command::Start:
                return PSTR("Start");
            case Command::Stop:
                return PSTR("Stop");
            case Command::Reset:
                return PSTR("Reset");
            case Command::Idle:
                return PSTR("Idle");
            default:
                return PSTR("Unknown");
        }
    }

    // Diagnostic performance counter for the one-wire bus
    struct OneWireBusStats
    {
        uint32_t    _totalEnumCount;
        uint32_t    _totalEnumTimeInMS;
        uint32_t    _maxEnumTimeInMS;
        uint32_t    _minEnumTimeInMS;
        uint32_t    _totalBufferOverflowErrors;
        uint32_t    _totalFormatErrors;
        uint32_t    _totalSensorCountOverflowErrors;
    };
    static void DisplayOneWireBusStats(Stream& output, const OneWireBusStats& stats, const char* prependString = "");

public:
    BoilerControllerTask();
    ~BoilerControllerTask();

    bool IsBusy();      // Returns true if the controller is busy processing a command (Start/Stop/Reset)
    void Start();
    void Stop();
    void Reset();

    inline const vector<uint64_t> &GetTempSensors() const { return _sensors; }
    FaultReason GetFaultReason();
    HeaterState GetHeaterState();
    uint32_t GetHeaterStateSequence();
    void GetTempertureState(TempertureState& State);
    
    void SetTempSensorIds(const TempSensorIds& SensorIds);
    void SetTargetTemps(const TargetTemps& TargetTemps);

    inline void GetTargetTemps(TargetTemps& Temps) { SnapshotTargetTemps(Temps); }
    inline Command GetCommand() { return SnapshotCommand(); }
    inline void GetTempSensorIds(TempSensorIds& SensorIds) { SnapshotTempSensors(SensorIds); }

    void ClearOneWireBusStats();
    void GetOneWireBusStats(OneWireBusStats& Stats);


    static void BoilerControllerThreadEntry(void *pvParameters);

private:
    friend void setup();
    struct DiscoveredTempSensor
    {
        uint64_t _id;
        float _temp;
    };    

private:
    void SnapshotTempSensors(TempSensorIds& SensorIds);
    void SnapshotTargetTemps(TargetTemps& Temps);
    void SnapshotTempState(TempertureState& State);
    Command SnapshotCommand();
    void SafeSetFaultReason(FaultReason Reason);
    void SafeSetHeaterState(HeaterState State);
    void SafeClearCommand();
    bool OneWireCoProcEnumLoop(array<DiscoveredTempSensor, 5>*& Results, uint8_t& ResultsSize);

    virtual void setup() override final;
    virtual void loop() override final;

private:
    static constexpr uint8_t    _heaterControlPin = 4;
    static constexpr uint8_t    _heaterActiveLedPin = 13;
    vector<uint64_t>            _sensors;
    TempSensorIds               _sensorIds; 
    HeaterState                 _state;
    TargetTemps                 _targetTemps;
    FaultReason                 _faultReason;
    TempertureState             _tempState;
    Command                     _command;
    OneWireBusStats             _oneWireStats;
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
    static constexpr uint64_t InvalidSensorId = 0x0000000000000000;

    uint64_t _ambiantTempSensorId;
    uint64_t _boilerInTempSensorId;
    uint64_t _boilerOutTempSensorId;

    static bool IsSensorIdValid(uint64_t SensorId)
    {
        return (SensorId != InvalidSensorId);
    }

    inline bool IsConfigured()
    {
        return (IsSensorIdValid(_ambiantTempSensorId) && IsSensorIdValid(_boilerInTempSensorId) && IsSensorIdValid(_boilerOutTempSensorId));
    }  
};
#pragma pack(pop)



//* Boiler Config Record - In persistant storage
#pragma pack(push, 1)
struct BoilerConfig
{
    float       _setPoint;      // Target temperature in C
    float       _hysteresis;    // Hysteresis in C

    inline bool IsConfigured()
    {
        return ((_setPoint >= 0.0) && (_hysteresis > 0.0));
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
extern class FlashStore<TempSensorsConfig, PS_TempSensorsConfigBase> tempSensorsConfig;
extern class BoilerControllerTask boilerControllerTask;
extern class FlashStore<BoilerConfig, PS_BoilerConfigBase> boilerConfig;
extern void SetAllBoilerParametersFromConfig();