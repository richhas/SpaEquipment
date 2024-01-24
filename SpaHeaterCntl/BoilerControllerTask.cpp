/**
 * @file BoilerControllerTask.cpp
 * @brief Implementation of the BoilerControllerTask class.
 * 
 * This file contains the implementation of the BoilerControllerTask class, which is responsible
 * for controlling the spa heater. The class provides methods for setting and getting the heater
 * state, fault reason, temperature state, target temperatures, and other related parameters.
 * It also includes a thread entry function that runs the main loop of the task.
 *
 * The BoilerControllerTask class manages the control of the spa heater in the Maxie HA system 2024.
 * It is responsible for reading temperature values from various sensors, controlling the heater state,
 * and responding to commands from the foreground task. The class uses a state machine to handle different
 * states such as Halted, Running, and Faulted. It also provides methods for setting and getting the
 * heater state, fault reason, temperature state, target temperatures, and other related parameters.
 * The main loop of the task is executed in the BoilerControllerThreadEntry function, which runs continuously
 * and performs the necessary actions based on the current state and commands received.
 *
 * The implementation includes critical sections to ensure thread safety when accessing shared data.
 * It uses a one-wire bus to communicate with temperature sensors and provides methods for reading temperature
 * values from these sensors. The class also includes methods for setting and getting the target temperatures,
 * as well as methods for starting, stopping, and resetting the heater. Additionally, it provides methods for
 * snapshotting the current state of temperature sensors, target temperatures, and command, which can be used
 * by the foreground task for monitoring and control purposes.
 *
 * Note: This implementation assumes the use of a specific hardware configuration and may require modification
 * for different systems or platforms.
 *
 * SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
 * All rights reserved.
 */

#include "SpaHeaterCntl.h"


// BoilerControllerTask freeRTOS task entry function
void BoilerControllerTask::BoilerControllerThreadEntry(void *pvParameters)
{
    boilerControllerTask.setup();
    while (true)
    {
        boilerControllerTask.loop();
        vTaskDelay(pdMS_TO_TICKS(1000));
        digitalWrite(_heaterActiveLedPin, !digitalRead(_heaterActiveLedPin));
    }
}

/**
 * @brief Initializes the BoilerControllerTask.
 * This function is called during setup to initialize the task.
 */
void BoilerControllerTask::setup()
{
    logger.Printf(Logger::RecType::Info, "*** BoilerControllerTask Thread Active ***\n");

    pinMode(_heaterControlPin, OUTPUT); // Make sure the heater is turned off to start with
    digitalWrite(_heaterControlPin, false);

    pinMode(_heaterActiveLedPin, OUTPUT);
    digitalWrite(_heaterActiveLedPin, false);

    // initialize all state visible to the foreground task
    _state = HeaterState::Halted;
    _command = Command::Idle;
    _faultReason = FaultReason::None;

    memset(&_tempState, 0, sizeof(_tempState));
    _tempState._sequence = 1;

    ClearOneWireBusStats();

    // discover the temperature sensors on the one wire bus for use be forgraound task (e.g. configures the sensors)
    logger.Printf(Logger::RecType::Info, "BoilerControllerTask: Start bus enumeration\n");
    while (_ds.selectNext())
    {
        uint8_t addr[8];

        _ds.getAddress(addr);
        Serial.print("Address:");
        for (uint8_t i = 0; i < 8; i++)
        {
            Serial.print(" ");
            Serial.print(addr[i]);
        }
        printf(Serial, " -- (%" $PRIX64 ")", To$PRIX64(*((uint64_t *)(&addr[0]))));
        Serial.println();

        _sensors.push_back(*((uint64_t *)(&addr[0])));
    }
    logger.Printf(Logger::RecType::Info, "BoilerControllerTask: Start bus enumeration - COMPLETE\n");
}

/*
 * The `loop` method is part of the `BoilerControllerTask` class, which controls a spa heater.
 *
 * The method starts by checking for commands from the foreground task. It then enters a switch statement
 * that handles different heater states: Halted, Running, and Faulted.
 *
 * In the Halted state, the heater is turned off. If a Start command is received, the heater state is changed to Running.
 *
 * In the Running state, the method handles reading temperatures from sensors and controlling the heater.
 * If a Stop command is received, the heater is turned off and the state is changed back to Halted.
 *
 * The first time the heater enters the Running state, it snapshots the current state of temperature sensors,
 * target temperatures, and temperature state. If the target temperatures are different from the current ones,
 * they are updated. The method then starts reading temperatures immediately.
 *
 * Inside the Running state, there is a nested state machine with two states: ReadTemps and ControlHeater.
 *
 * In the ReadTemps state, the method reads temperatures from the ambient, boiler in, and boiler out sensors.
 * If reading from the boiler in sensor fails, the heater is turned off, the state is changed to Faulted,
 * and a critical error message is logged. If reading from the boiler out or ambient sensor fails, a warning message is logged.
 *
 * If the temperatures have changed, they are updated in the local copy and the foreground task is notified.
 * The method then sets an alarm to read temperatures again in 4 seconds and changes the state to ControlHeater.
 *
 * In the ControlHeater state, the method computes the hard on and off temperature limits.
 * If the boiler in temperature is above the hard off limit, the heater is turned off.
 * If the boiler in temperature is below the hard on limit, the heater is turned on.
 * If the heater state has changed, it is updated in the local copy and the foreground task is notified.
 * If the alarm set in the ReadTemps state goes off, the state is changed back to ReadTemps.
 *
 * In the Faulted state, if a Reset command is received, the heater is turned off and the state is changed back to Halted.
 *
 * The purpose of this method is to manage the different states of the boiler heater system, as such:
 *
 * The implementation is a state machine design in for a boiler controller task.
 * The state machine is used to manage the different states of a boiler heater system.
 * The states include Halted, Running, and Faulted:
 *
 *      Halted:     This is the initial state of the heater. In this state, the heater is turned off.
 *                  If a Start command is received, the heater state is changed to Running.
 *
 *      Running:    In this state, the heater is active. The system checks for a Stop command.
 *                  If received, the heater is turned off and the state is changed back to Halted.
 *                  If not, the system enters a nested state machine with two states: ReadTemps and ControlHeater.
 *
 *          ReadTemps:     In this sub-state, the system reads temperatures from the ambient, boiler in,
 *                         and boiler out sensors. If the boiler in sensor fails to read, the heater is turned off,
 *                         the state is changed to Faulted, and a critical error message is logged.
 *                         If the boiler out or ambient sensor fails to read, a warning message is logged.
 *                         If the temperatures have changed, they are updated in the local copy and the foreground task is notified.
 *                         The system then sets an alarm to read temperatures again in 4 seconds and changes the state to ControlHeater.
 *
 *          ControlHeater: In this state, the system computes the hard on and off temperature limits.
 *                         If the boiler in temperature is above the hard off limit, the heater is turned off.
 *                         If the boiler in temperature is below the hard on limit, the heater is turned on.
 *                         If the heater state has changed, it is updated in the local copy and the foreground task is notified.
 *                         If the alarm set in the ReadTemps state goes off, the state is changed back to ReadTemps.
 *
 *      Faulted:    This state is entered when a critical error occurs, such as failing to read the boiler in temperature.
 *                  In this state, the heater is turned off. If a Reset command is received, the error is cleared and the
 *                  state is changed back to Halted.
 *
 * The purpose of this state machine design is to manage the different states of the boiler heater system,
 * handle transitions between states, and perform the appropriate actions based on the current state and incoming commands.
 * This design makes the code more organized, easier to understand, and easier to maintain.
 */
void BoilerControllerTask::loop()
{
    static bool firstTimeInRunningState = true;
    static TempSensorIds sensors;
    static TargetTemps targetTemps;
    static TempertureState tempState;

    // Each time we loop we need to snapshot the current state
    BoilerControllerTask::Command command = SnapshotCommand();
    SnapshotTempSensors(sensors);
    SnapshotTargetTemps(targetTemps);
    SnapshotTempState(tempState);

    // Auto update the target temps if they are different from the current target temps
    if (targetTemps._setPoint != tempState._setPoint ||
        targetTemps._hysteresis != tempState._hysteresis)
    {
        { CriticalSection cs;
            _tempState._sequence++; // Increment the sequence number so foreground task
                                    // knows the target temp has changed
            _tempState._setPoint = targetTemps._setPoint;
            _tempState._hysteresis = targetTemps._hysteresis;
        }

        // And keep our local copy of the set point and hysteresis up to date
        tempState._setPoint = targetTemps._setPoint;
        tempState._hysteresis = targetTemps._hysteresis;
    }

    switch (_state)
    {
    case HeaterState::Halted:
    {
        digitalWrite(_heaterControlPin, false); // Make sure the heater is turned off

        if (command == Command::Start)
        {
            firstTimeInRunningState = true;
            SafeClearCommand();                       // acknowledge the command
            SafeSetHeaterState(HeaterState::Running); // Go to the Running state
        }
    }
    break;

    case HeaterState::Running:
    {
        enum class State
        {
            ReadTemps,
            ControlHeater,
        };

        static State state;
        static Timer nextReadTimer;

        if (command == Command::Stop)
        {
            // The forground task has requested that we stop
            SafeClearCommand();                      // acknowledge the command
            SafeSetHeaterState(HeaterState::Halted); // Go back to the Halted state
            digitalWrite(_heaterControlPin, false);  // Make sure the heater is turned off
            return;
        }

        if (firstTimeInRunningState)
        {
            firstTimeInRunningState = false;

            // First time in the Running state - initialize our inner state machine
            nextReadTimer.SetAlarm(0); // Read temps immediately
            state = State::ReadTemps;  // Start by reading the temps
        }

        switch (state)
        {
        case State::ReadTemps:
        {
            float ambiantTemp = tempState._ambiantTemp;
            float boilerInTemp = tempState._boilerInTemp;
            float boilerOutTemp = tempState._boilerOutTemp;

            static uint8_t sensorIndexToRead = 0;

            bool ambiantTempRead = true;
            bool boilerInTempRead = true;
            bool boilerOutTempRead = true;

            // Read the temps from the sensors in a round robin fashion
            switch (sensorIndexToRead)
            {
            case 0:
                ambiantTempRead = ReadTemp(sensors._ambiantTempSensorId, ambiantTemp);
                break;
            
            case 1:
                boilerInTempRead = ReadTemp(sensors._boilerInTempSensorId, boilerInTemp);
                break;

            case 2:
                boilerOutTempRead = ReadTemp(sensors._boilerOutTempSensorId, boilerOutTemp);
                break;
            }

            sensorIndexToRead = (sensorIndexToRead + 1) % 3;

            if (!boilerInTempRead)
            {
                SafeSetFaultReason(FaultReason::TempSensorReadFailed);
                digitalWrite(_heaterControlPin, false);   // Make sure the heater is turned off
                SafeSetHeaterState(HeaterState::Faulted); // Go to the Faulted state

                logger.Printf(Logger::RecType::Critical, "BoilerControllerTask: Failed to read boiler in temp sensor\n");
                return;
            }

            if (!boilerOutTempRead)
            {
                logger.Printf(Logger::RecType::Warning, "BoilerControllerTask: Failed to read boiler out temp sensor\n");
            }

            if (!ambiantTempRead)
            {
                logger.Printf(Logger::RecType::Warning, "BoilerControllerTask: Failed to read ambiant temp sensor\n");
            }

            if (ambiantTemp != tempState._ambiantTemp ||
                boilerInTemp != tempState._boilerInTemp ||
                boilerOutTemp != tempState._boilerOutTemp)

            { CriticalSection cs;
                // The temps have changed - update the shared state for the foreground task's access
                _tempState._sequence++; // Increment the sequence number so foreground task knows the temps have changed
                _tempState._ambiantTemp = ambiantTemp;
                _tempState._boilerInTemp = boilerInTemp;
                _tempState._boilerOutTemp = boilerOutTemp;
                _tempState._setPoint = targetTemps._setPoint;
                _tempState._hysteresis = targetTemps._hysteresis;
                _tempState._heaterOn = digitalRead(_heaterControlPin);
            }

            // Keep out local copy of the temps up to date
            tempState._ambiantTemp = ambiantTemp;
            tempState._boilerInTemp = boilerInTemp;
            tempState._boilerOutTemp = boilerOutTemp;

            nextReadTimer.SetAlarm(1333);   // Cause all sensors to be read again in 4 seconds
            state = State::ControlHeater;
        }
        break;

        case State::ControlHeater:
        {
            // Compute the hard on and off temperature limits
            float const hardOffTemp = tempState._setPoint + tempState._hysteresis;
            float const hardOnTemp = tempState._setPoint - tempState._hysteresis;

            if (tempState._boilerInTemp > hardOffTemp)
            {
                // The boiler in temp is above the hard off limit - turn off the heater
                digitalWrite(_heaterControlPin, false);
            }
            else if (tempState._boilerInTemp < hardOnTemp)
            {
                // The boiler in temp is below the hard on limit - turn on the heater
                digitalWrite(_heaterControlPin, true);
            }

            if (digitalRead(_heaterControlPin) != tempState._heaterOn)
            {
                // The heater state has changed - update our local copy
                tempState._heaterOn = digitalRead(_heaterControlPin);

                { CriticalSection cs;
                    // Update the temp state for the foreground task's access
                    _tempState._sequence++; // Increment the sequence number so foreground task knows the heater state has changed
                    _tempState._heaterOn = tempState._heaterOn;
                }
            }

            if (nextReadTimer.IsAlarmed())
            {
                // Time to read the temps again
                state = State::ReadTemps;
            }
        }
        break;

        default:
            $FailFast();
        }
    }
    break;

    case HeaterState::Faulted:
    {
        digitalWrite(_heaterControlPin, false); // Make sure the heater is turned off
        if (command == Command::Reset)
        {
            SafeClearCommand();                      // acknowledge the command
            SafeSetHeaterState(HeaterState::Halted); // Go back to the Halted state
        }
    }
    break;

    default:
        break;
    }
}

//** BoilerControllerTask constructor and destructor
BoilerControllerTask::BoilerControllerTask()
    : _ds(_oneWireBusPin)
{
}

BoilerControllerTask::~BoilerControllerTask()
{
    $FailFast();
}

// Support for reading temperatures from the sensors
bool BoilerControllerTask::ReadTemp(uint64_t SensorId, float &Temp)
{
    auto startTime = millis();
    if (!_ds.select((uint8_t *)(&SensorId)))
    {
        SafeSetFaultReason(FaultReason::TempSensorNotFound);
        return false;
    }

    Temp = _ds.getTempC();
    auto endTime = millis();
    auto elapsed = endTime - startTime;

    { CriticalSection cs;
        _oneWireStats._totalReadCount++;
        _oneWireStats._totalReadTimeInMS += elapsed;
        if (elapsed > _oneWireStats._maxReadTimeInMS)
            _oneWireStats._maxReadTimeInMS = elapsed;
        if (elapsed < _oneWireStats._minReadTimeInMS)
            _oneWireStats._minReadTimeInMS = elapsed;
    }


    return true;
}

//** Getters and setters for the various parameters - these methods are thread safe
BoilerControllerTask::FaultReason BoilerControllerTask::GetFaultReason()
{
    CriticalSection cs;
    {
        return _faultReason;
    }
}

void BoilerControllerTask::SafeSetFaultReason(BoilerControllerTask::FaultReason Reason)
{
    CriticalSection cs;
    {
        _faultReason = Reason;
    }
}

BoilerControllerTask::HeaterState BoilerControllerTask::GetHeaterState()
{
    CriticalSection cs;
    {
        return _state;
    }
}

void BoilerControllerTask::SafeSetHeaterState(BoilerControllerTask::HeaterState State)
{
    CriticalSection cs;
    {
        _state = State;
    }
}

uint32_t BoilerControllerTask::GetHeaterStateSequence()
{
    CriticalSection cs;
    {
        return _tempState._sequence;
    }
}

void BoilerControllerTask::GetTempertureState(TempertureState& State)
{
    CriticalSection cs;
    {
        State = _tempState;
    }
}

void BoilerControllerTask::SnapshotTempState(TempertureState &State)
{
    CriticalSection cs;
    {
        State = _tempState;
    }
}

void BoilerControllerTask::SetTempSensorIds(const TempSensorIds& SensorIds)
{
    CriticalSection cs;
    {
        _sensorIds = SensorIds;
    }
}

void BoilerControllerTask::SnapshotTempSensors(TempSensorIds& SensorIds)
{
    CriticalSection cs;
    {
        SensorIds = _sensorIds;
    }
}

void BoilerControllerTask::SetTargetTemps(const TargetTemps& Temps)
{
    CriticalSection cs;
    {
        _targetTemps = Temps;
    }
}

void BoilerControllerTask::SnapshotTargetTemps(TargetTemps& Temps)
{
    CriticalSection cs;
    {
        Temps = _targetTemps;
    }
}

BoilerControllerTask::Command BoilerControllerTask::SnapshotCommand()
{
    CriticalSection cs;
    {
        return _command;
    }
}

void BoilerControllerTask::SafeClearCommand()
{
    CriticalSection cs;
    {
        _command = Command::Idle;
    }
}

//** Forground task command interface methods - these methods are thread safe
bool BoilerControllerTask::IsBusy()         // Returns true if the task is busy processing a command
{
    CriticalSection cs;
    {
        return _command != Command::Idle;
    }
}

void BoilerControllerTask::Start()          // Starts the heater - only valid if the task is in the Halted state
{
    CriticalSection cs;
    {
        $Assert(_command == Command::Idle);
        $Assert(_state == HeaterState::Halted);
        _command = Command::Start;
    }
}

void BoilerControllerTask::Stop()           // Stops the heater - only valid if the task is in the Running state
{
    CriticalSection cs;
    {
        $Assert(_command == Command::Idle);
        $Assert(_state == HeaterState::Running);
        _command = Command::Stop;
    }
}

void BoilerControllerTask::Reset()          // Resets the heater - only valid if the task is in the Faulted state
{
    CriticalSection cs;
    {
        $Assert(_command == Command::Idle);
        $Assert(_state == HeaterState::Faulted);
        _command = Command::Reset;
    }
}

//** Public data structure display methods
void BoilerControllerTask::DisplayTemperatureState(Stream &output, const TempertureState &state, const char *prependString)
{
    printf(output, "%sTemperature State:\n", prependString);
    printf(output, "%s    Sequence: %u\n", prependString, state._sequence);
    printf(output, "%s    Ambient Temperature: %.2fC (%.2fF)\n", prependString, state._ambiantTemp, $CtoF(state._ambiantTemp));
    printf(output, "%s    Boiler In Temperature: %.2f (%.2fF)\n", prependString, state._boilerInTemp, $CtoF(state._boilerInTemp));
    printf(output, "%s    Boiler Out Temperature: %.2f (%.2fF)\n", prependString, state._boilerOutTemp, $CtoF(state._boilerOutTemp));
    printf(output, "%s    Set Point: %.2f (%.2fF)\n", prependString, state._setPoint, $CtoF(state._setPoint));
    printf(output, "%s    Hysteresis: %.2f\n", prependString, state._hysteresis);
    printf(output, "%s    Heater On: %s\n", prependString, state._heaterOn ? "true" : "false");
}

void BoilerControllerTask::DisplayTempSensorIds(Stream &output, const TempSensorIds &ids, const char *prependString)
{
    printf(output, "%sTemperature Sensor IDs:\n", prependString);
    printf(output, "%s    Ambient Temperature Sensor ID: %" $PRIX64 "\n", prependString, To$PRIX64(ids._ambiantTempSensorId));
    printf(output, "%s    Boiler In Temperature Sensor ID: %" $PRIX64 "\n", prependString, To$PRIX64(ids._boilerInTempSensorId));
    printf(output, "%s    Boiler Out Temperature Sensor ID: %" $PRIX64 "\n", prependString, To$PRIX64(ids._boilerOutTempSensorId));
}

void BoilerControllerTask::DisplayTargetTemps(Stream &output, const TargetTemps &temps, const char *prependString)
{
    printf(output, "%sTarget Temperatures:\n", prependString);
    printf(output, "%s    Set Point: %.2fC (%.2fF)\n", prependString, temps._setPoint, $CtoF(temps._setPoint));
    printf(output, "%s    Hysteresis: %.2f\n", prependString, temps._hysteresis);
}
