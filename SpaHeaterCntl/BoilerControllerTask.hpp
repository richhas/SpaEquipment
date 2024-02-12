/*
    BoilerControllerTask Definitions

    Copyright TinyBus 2024
*/
#pragma once

#include "SpaHeaterCntl.h"

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
    enum class StateMachineState
    {
        Halted,         // Heater is halted. No power to heater. Can be moved to Running state by calling Start().
        Running,        // Heater is running. Can be moved to Halted state by calling Stop(). Will move to Faulted state if a fault is detected.
        Faulted,        // Enters this state if a fault is detected. Can be moved to Halted state by calling Reset().
    };
    static constexpr const char* GetStateMachineStateDescription(StateMachineState state)
    {
        switch (state)
        {
            case StateMachineState::Halted:
                    return PSTR("Halted");
            case StateMachineState::Running:
                return PSTR("Running");
            case StateMachineState::Faulted:
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
                return PSTR("TSMissing");           // Sensor not found
            case FaultReason::TempSensorReadFailed:
                return PSTR("TSReadErr");           // Sensor read failed
            case FaultReason::CoProcCommError:
                return PSTR("CProcErr");            // Co-processor communication error
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

    // Boiler Modes - same as HA UI
    enum class BoilerMode
    {
        Off,
        Performance,
        Eco,
        Undefined
    };

    static constexpr const char* GetBoilerModeDescription(BoilerMode mode)
    {
        switch (mode)
        {
            case BoilerMode::Off:
                return PSTR("off");
            case BoilerMode::Performance:
                return PSTR("performance");
            case BoilerMode::Eco:
                return PSTR("eco");
            default:
                return PSTR("Unknown");
        }
    }

    static BoilerMode GetBoilerModeFromDescription(const char* mode)
    {
        if (strcmp_P(mode, PSTR("off")) == 0)
            return BoilerMode::Off;
        else if (strcmp_P(mode, PSTR("performance")) == 0)
            return BoilerMode::Performance;
        else if (strcmp_P(mode, PSTR("eco")) == 0)
            return BoilerMode::Eco;
        else
            return BoilerMode::Undefined;
    }

public:
    BoilerControllerTask();
    ~BoilerControllerTask();
    static void BoilerControllerThreadEntry(void *pvParameters);


    bool IsBusy();      // Returns true if the controller is busy processing a command (Start/Stop/Reset)

    void Start();
    void Stop();
    void Reset();

    // Safe forms of the above commands
    void StartIfSafe();
    void StopIfSafe();
    void ResetIfSafe();

    void SetMode(BoilerMode Mode);
    BoilerMode GetMode();

    inline const vector<uint64_t> &GetTempSensors() const { return _sensors; }
    FaultReason GetFaultReason();
    StateMachineState GetStateMachineState();
    uint32_t GetHeaterStateSequence();
    void GetTempertureState(TempertureState& State);
    
    void SetTempSensorIds(const TempSensorIds& SensorIds);
    void SetTargetTemps(const TargetTemps& TargetTemps);
    inline void GetTargetTemps(TargetTemps& Temps) { SnapshotTargetTemps(Temps); }
    inline Command GetCommand() { return SnapshotCommand(); }
    inline void GetTempSensorIds(TempSensorIds& SensorIds) { SnapshotTempSensors(SensorIds); }

    void ClearOneWireBusStats();
    void GetOneWireBusStats(OneWireBusStats& Stats);

    // Console display helpers
    static void ShowCurrentBoilerConfig(Stream&, int PostLineFeedCount = 2);
    static void ShowCurrentBoilerState(Stream&);

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
    void SafeSetStateMachineState(StateMachineState State);
    void SafeClearCommand();
    bool OneWireCoProcEnumLoop(array<DiscoveredTempSensor, 5>*& Results, uint8_t& ResultsSize);

    virtual void setup() override final;
    virtual void loop() override final;

private:
    static constexpr uint8_t    _heaterControlPin = 4;
    static constexpr uint8_t    _heaterActiveLedPin = 13;
    vector<uint64_t>            _sensors;
    TempSensorIds               _sensorIds; 
    StateMachineState           _state;
    TargetTemps                 _targetTemps;
    FaultReason                 _faultReason;
    TempertureState             _tempState;
    Command                     _command;
    OneWireBusStats             _oneWireStats;
    BoilerMode                  _boilerMode;
};

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
    float _setPoint;   // Target temperature in C
    float _hysteresis; // Hysteresis in C
    BoilerControllerTask::BoilerMode
        _mode; // Boiler mode

    inline bool IsConfigured()
    {
        return ((_setPoint >= 0.0) && (_hysteresis > 0.0));
    }
};
#pragma pack(pop)

//** Cross module references
extern class BoilerControllerTask boilerControllerTask;
extern class FlashStore<TempSensorsConfig, PS_TempSensorsConfigBase> tempSensorsConfig;
extern class FlashStore<BoilerConfig, PS_BoilerConfigBase> boilerConfig;
extern CmdLine::ProcessorDesc controlBoilerCmdProcessors[];
extern int const LengthOfControlBoilerCmdProcessors;
extern CmdLine::ProcessorDesc configBoilerCmdProcessors[];
extern int const LengthOfConfigBoilerCmdProcessors;
