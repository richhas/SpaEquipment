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

    static Timer ledTimer(1000);
    while (true)
    {
        boilerControllerTask.loop();
        vTaskDelay(pdMS_TO_TICKS(50));

        if (ledTimer.IsAlarmed())
        {
            ledTimer.SetAlarm(1000);
            digitalWrite(_heaterActiveLedPin, !digitalRead(_heaterActiveLedPin));
        }
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

    // discover the temperature sensors on the one wire bus for use by forground task (e.g. configures the sensors)
    logger.Printf(Logger::RecType::Info, "BoilerControllerTask: Start bus enumeration\n");
    
    array<DiscoveredTempSensor, 5>* results;
    uint8_t resultsSize;

    while (!OneWireCoProcEnumLoop(results, resultsSize));

    for (int ix = 0; ix < resultsSize; ix++)
    {
        logger.Printf(Logger::RecType::Info, "BoilerControllerTask: OneWireCoProcEnumLoop: Sensor ID: %" $PRIX64 "\n", 
                                             To$PRIX64(results->at(ix)._id));

        _sensors.push_back(results->at(ix)._id);
    }
    logger.Printf(Logger::RecType::Info, "BoilerControllerTask: Start bus enumeration - COMPLETE\n");
}


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

    //* Main state machine for the BoilerControllerTask
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
                logger.Printf(Logger::RecType::Info, "BoilerControllerTask: HeaterState::Halted: Command::Start\n");
            }
        }
        break;

        case HeaterState::Running:
        {
            enum class State
            {
                StartCycle,
                ControlHeater,
            };
            static State state;     // Current state of the (inner) Running state machine

            static Timer coEnumTimeoutTimer;                                    // Alarms if the co-processor enumeration takes too long - Faults the system
            static Timer boilerInTempReadTimeoutTimer;                          // Alarms if the boiler in temp sensor read takes too long - Faults the system
            static Timer boilerOutTempReadTimeoutTimer;                         // Alarms if the boiler out temp sensor read takes too long - Logs a warning
            static Timer ambiantTempReadTimeoutTimer;                           // Alarms if the ambiant temp sensor read takes too long - Logs a warning
            static constexpr uint32_t coEnumTimeoutInMS = 3 * 1000 * 60;        // 3 minutes      - Faults the system
            static constexpr uint32_t boilerInTempReadTimeoutInMS = 10 * 1000;  // 10 seconds     - Faults the system
            static constexpr uint32_t boilerOutTempReadTimeoutInMS = 10 * 1000; // 10 seconds
            static constexpr uint32_t ambiantTempReadTimeoutInMS = 10 * 1000;   // 10 seconds
            static uint32_t startOfEnumTimeInMS;                                // Time in MS when the enumeration started
            static bool haveReadTempsAtLeastOnce;                               // True if we have read the temps at least once in a cycle

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
                //printf(Serial, "BoilerControllerTask: Running: firstTime logic\n");
                firstTimeInRunningState = false;

                // First time in the Running state - initialize our inner state machine
                state = State::StartCycle;
            }

            // Inner state machine for the Running state
            switch (state)
            {
                case State::StartCycle:
                {
                    //printf(Serial, "BoilerControllerTask: Running: StartCycle; Changing to ControlHeater\n");
                    logger.Printf(Logger::RecType::Info, "BoilerControllerTask: HeaterState::Running:\n");

                    // Start of the Running cycle
                    coEnumTimeoutTimer.SetAlarm(coEnumTimeoutInMS);
                    boilerInTempReadTimeoutTimer.SetAlarm(boilerInTempReadTimeoutInMS);
                    boilerOutTempReadTimeoutTimer.SetAlarm(boilerOutTempReadTimeoutInMS);
                    ambiantTempReadTimeoutTimer.SetAlarm(ambiantTempReadTimeoutInMS);
                    startOfEnumTimeInMS = millis();   // Capture the start time of the enumeration cycle

                    haveReadTempsAtLeastOnce = false;
                    state = State::ControlHeater;
                }
                break;

                case State::ControlHeater:
                {
                    // Default current temps to the last known temps
                    float ambiantTemp = tempState._ambiantTemp;
                    float boilerInTemp = tempState._boilerInTemp;
                    float boilerOutTemp = tempState._boilerOutTemp;

                    // First we try and absord an enum from the co-processor
                    array<DiscoveredTempSensor, 5>* results;
                    uint8_t resultsSize;

                    if (OneWireCoProcEnumLoop(results, resultsSize))
                    {
                        // We have a completed CoProc enumeration - reconcile the results with our configured sensors; capture the temps as found
                        haveReadTempsAtLeastOnce = true;

                        // Compute the duration of the enumeration cycle and update the shared stats
                        uint32_t durationInMS = millis() - startOfEnumTimeInMS; // Compute the duration of the enumeration cycle
                        { CriticalSection cs;
                            _oneWireStats._totalEnumCount++;
                            _oneWireStats._totalEnumTimeInMS += durationInMS;
                            if (durationInMS > _oneWireStats._maxEnumTimeInMS)
                                _oneWireStats._maxEnumTimeInMS = durationInMS;
                            if (durationInMS < _oneWireStats._minEnumTimeInMS)
                                _oneWireStats._minEnumTimeInMS = durationInMS;
                        }

                        coEnumTimeoutTimer.SetAlarm(coEnumTimeoutInMS);     // Reset the timeout timer for the next enumeration cycle
                        startOfEnumTimeInMS = millis();                     // Capture the start time of this next enumeration cycle

                        // for each sensor we have in sensors (our configured set) we need to find if there is a matching sensor in results. 
                        // If so, we need to: 1) update that sensor's last known temps (above), and 2) reset that sensor's timeout timer. 
                        for (int ix = 0; ix < resultsSize; ix++)
                        {
                            if (results->at(ix)._id == sensors._ambiantTempSensorId)
                            {
                                ambiantTemp = results->at(ix)._temp;
                                ambiantTempReadTimeoutTimer.SetAlarm(ambiantTempReadTimeoutInMS);
                            }
                            else if (results->at(ix)._id == sensors._boilerInTempSensorId)
                            {
                                boilerInTemp = results->at(ix)._temp;
                                boilerInTempReadTimeoutTimer.SetAlarm(boilerInTempReadTimeoutInMS);
                            }
                            else if (results->at(ix)._id == sensors._boilerOutTempSensorId)
                            {
                                boilerOutTemp = results->at(ix)._temp;
                                boilerOutTempReadTimeoutTimer.SetAlarm(boilerOutTempReadTimeoutInMS);
                            }
                            else
                            {
                                // This is a sensor we don't know about - log a warning
                                logger.Printf(Logger::RecType::Warning, "BoilerControllerTask: OneWireCoProcEnumLoop: Unknown sensor ID: %" $PRIX64 "\n", 
                                             To$PRIX64(results->at(ix)._id));
                            }
                        }
                    }

                    // Detect any coProc and/or sensors realted timeouts and fault the system if necessary; log accordingly
                    if (coEnumTimeoutTimer.IsAlarmed())
                    {
                        // The co-processor enumeration has taken too long - fault the system
                        digitalWrite(_heaterControlPin, false); // Make sure the heater is turned off
                        SafeSetFaultReason(FaultReason::CoProcCommError);

                        //printf(Serial, "BoilerControllerTask: OneWireCoProcEnumLoop: CoProc Timeout\n");
                        SafeSetHeaterState(HeaterState::Faulted); // Go to the Faulted state
                        return;
                    }

                    if (boilerInTempReadTimeoutTimer.IsAlarmed())
                    {
                        // The boiler in temp sensor read has taken too long - fault the system
                        digitalWrite(_heaterControlPin, false); // Make sure the heater is turned off
                        SafeSetFaultReason(FaultReason::TempSensorReadFailed);

                        //printf(Serial, "BoilerControllerTask: BoilerInTempReadTimeoutTimer: Timeout\n");
                        SafeSetHeaterState(HeaterState::Faulted); // Go to the Faulted state
                        return;
                    }

                    if (boilerOutTempReadTimeoutTimer.IsAlarmed())
                    {
                        // The boiler out temp sensor read has taken too long - log a warning
                        //printf(Serial, "BoilerControllerTask: BoilerOutTempReadTimeoutTimer: Timeout\n");
                    }

                    if (ambiantTempReadTimeoutTimer.IsAlarmed())
                    {
                        // The ambiant temp sensor read has taken too long - log a warning
                        //printf(Serial, "BoilerControllerTask: AmbiantTempReadTimeoutTimer: Timeout\n");
                    }

                    // Check for any changes in the temps or heater status and update the shared state if necessary
                    if (ambiantTemp != tempState._ambiantTemp || boilerInTemp != tempState._boilerInTemp || boilerOutTemp != tempState._boilerOutTemp)
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

                    // Keep our local copy of the temps up to date
                    tempState._ambiantTemp = ambiantTemp;
                    tempState._boilerInTemp = boilerInTemp;
                    tempState._boilerOutTemp = boilerOutTemp;

                    if (!haveReadTempsAtLeastOnce)
                    {
                        digitalWrite(_heaterControlPin, false); // Make sure the heater is turned off until we have read the temps at least once
                    }
                    else
                    {
                        // Don't allow the following to happen until the temps have been read at least once; specifically in boilerInTemp

                        // Now assert control over the heater based on current know temps
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
                    }

                    // Check for any changes in the heater status and update the shared state if necessary
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
                }
                break;

                default:
                {
                    $FailFast();
                }
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
        {
            $FailFast();
        }
    }
}

//** BoilerControllerTask constructor and destructor
BoilerControllerTask::BoilerControllerTask()
{
}

BoilerControllerTask::~BoilerControllerTask()
{
    $FailFast();
}

// Support for reading temperatures from the sensors - this is done via the OneWireCoProc attached to Serial1
/**
 * @brief Performs the enumeration for a co-processor connected via OneWire protocol.
 * 
 * @param[out] Results Pointer to an array of DiscoveredTempSensor objects to store the enumeration results.
 * @param[out] ResultsSize Reference to an integer to store the size of the enumeration results.
 * @return true if the co-processor loop has completed the enumeration cycle, 
 *         false if it is still enumerating - keep calling.
 */
bool BoilerControllerTask::OneWireCoProcEnumLoop(array<DiscoveredTempSensor, 5>*& Results, uint8_t& ResultsSize) 
{
    static bool firstTime = true;
    enum class State
    {
        StartCycle,     // Start of the enumeration cycle
        HuntForEnum,    // Hunt for the start of the enumeration
        Enumerate,      // Enumerate the sensors
    };
    static State state;

    static array<DiscoveredTempSensor, 5> sensors;  // Max of 5 sensors
    static uint8_t      sensorIndex;
    static char         buffer[33];
    static uint8_t      bufferIndex;

    if (firstTime)
    {
        // Start of the enumeration cycle
        firstTime = false;
        Serial1.begin(9600);
        Serial1.setTimeout(0);                  // Just a polled environment - don't wait for anything
        state = State::StartCycle;
    }

    Results = nullptr;
    ResultsSize = 0;

    switch (state)
    {
        case State::StartCycle:
        {
            bufferIndex = 0;
            sensorIndex = 0;

            {   // Drain any data in the serial buffer
                byte        tossBuffer[32];

                while (Serial1.readBytes(tossBuffer, sizeof(tossBuffer)) > 0) {}
            }

            state = State::HuntForEnum;
            return false;
        }
        break;

        case State::HuntForEnum:
        {
            while (Serial1.available())
            {
                char c = Serial1.read();
                if (c == '\n')                  // just drop the '\n' and wait for the '\r'
                    return false;

                if (c == '\r')
                {
                    // End of line - check for the start of the enumeration
                    buffer[bufferIndex] = 0;
                    if ((bufferIndex == 6) && (memcmp(buffer, "ESTART", 6) == 0))
                    {
                        // Start of the enumeration
                        sensorIndex = 0;
                        bufferIndex = 0;
                        state = State::Enumerate;
                        return false;
                    }
                    else
                    {
                        // Not the start of the enumeration - start over
                        bufferIndex = 0;
                        return false;
                    }
                }
                else
                {
                    // accumulate the character in the buffer as long as there is room
                    if (bufferIndex < (sizeof(buffer) - 1))
                    {
                        buffer[bufferIndex++] = c;
                    }
                    else
                    {
                        // Buffer overflow - start over
                        { CriticalSection cs;
                            _oneWireStats._totalBufferOverflowErrors++;
                        }
                        state = State::StartCycle;
                        return false;
                    }
                }
            }
        }
        break;

        case State::Enumerate:
        {
            while (Serial1.available())
            {
                char c = Serial1.read();
                if (c == '\n') // just drop the '\n' and wait for the '\r'
                    return false;

                if (c == '\r')
                {
                    // End of line - check for the start of the enumeration
                    buffer[bufferIndex] = 0;
                    if ((bufferIndex == 5) && (memcmp(buffer, "ESTOP", 5) == 0))
                    {
                        // End of the enumeration
                        Results = &sensors;
                        ResultsSize = sensorIndex;
                        
                        state = State::StartCycle;
                        return true;
                    }
                    else
                    {
                        // Determine if the received line is a valid sensor state description
                        // Valid format: IIIIIIIIIIIIIIII;MM;RR;T<\0>
                        //               012345678901234567890123456789
                        // Where: IIIIIIIIIIIIIIII is the 64 bit sensor ID - in HEX Ascii
                        //        MM is the sensor model type - in HEX Ascii
                        //        RR is the sensor resolution - in HEX Ascii
                        //        T is the temperature - in floating point Ascii at least 1 char (i.e. '0')

                        // Check for the correct length and basic format
                        if ((bufferIndex < 24)  || (buffer[16] != ';') || (buffer[19] != ';') || (buffer[22] != ';'))
                        {
                            // Invalid format - start over
                            { CriticalSection cs;
                                _oneWireStats._totalFormatErrors++;
                            }
                            state = State::StartCycle;
                            return false;
                        }

                        if (sensorIndex < (sensors.size() - 1))
                        {
                            // There is room for another sensor - increment the index and get ready for the next line
                            // Extract all the fields for sensors[sensorIndex] from the validated buffer; last field is the temperature
                            // and is variable length
                            $Assert(sensorIndex < 3);
                            sensors[sensorIndex]._id = strtoull(&buffer[0], NULL, 16);
                            sensors[sensorIndex]._temp = strtod(&buffer[23], NULL);

                            sensorIndex++;
                            bufferIndex = 0;
                            state = State::Enumerate;
                            return false;
                        }
                        else
                        {
                            // No more room for another sensor - start over
                            { CriticalSection cs;
                                _oneWireStats._totalSensorCountOverflowErrors++;
                            }
                            state = State::StartCycle;
                        }
                        return false;
                    }
                }
                else
                {
                    // accumulate the character in the buffer as long as there is room
                    if (bufferIndex < (sizeof(buffer) - 1))
                    {
                        buffer[bufferIndex++] = c;
                    }
                    else
                    {
                        // Buffer overflow - start over
                        { CriticalSection cs;
                            _oneWireStats._totalBufferOverflowErrors++;
                        }
                        state = State::StartCycle;
                        return false;
                    }
                }
            }
        }
        break;

        default:
        {
            $FailFast();
        }
    }

    return false;
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

void BoilerControllerTask::GetOneWireBusStats(OneWireBusStats& Stats)
{
    { CriticalSection cs;
        Stats = _oneWireStats;
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

void BoilerControllerTask::ClearOneWireBusStats() 
{ 
    { CriticalSection cs; 
        _oneWireStats = 
        { 
            ._totalEnumCount = 0, 
            ._totalEnumTimeInMS = 0, 
            ._maxEnumTimeInMS = 0, 
            ._minEnumTimeInMS = 0xFFFFFFFF,
            ._totalBufferOverflowErrors = 0,
            ._totalFormatErrors = 0,
            ._totalSensorCountOverflowErrors = 0
        };
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

void BoilerControllerTask::DisplayOneWireBusStats(Stream& output, const OneWireBusStats& stats, const char* prependString)
{
    printf(output, PSTR("%sTotalEnumCount: %u\n"), prependString, stats._totalEnumCount);
    printf(output, PSTR("%sTotalEnumTimeInMS: %u\n"), prependString, stats._totalEnumTimeInMS);
    printf(output, PSTR("%sAvgEnumTimeInMS: %u\n"), prependString, stats._totalEnumTimeInMS / stats._totalEnumCount);
    printf(output, PSTR("%sMaxEnumTimeInMS: %u\n"), prependString, stats._maxEnumTimeInMS);
    printf(output, PSTR("%sMinEnumTimeInMS: %u\n"), prependString, stats._minEnumTimeInMS);
    printf(output, PSTR("%sTotalBufferOverflowErrors: %u\n"), prependString, stats._totalBufferOverflowErrors);
    printf(output, PSTR("%sTotalFormatErrors: %u\n"), prependString, stats._totalFormatErrors);
    printf(output, PSTR("%sTotalSensorCountOverflowErrors: %u\n"), prependString, stats._totalSensorCountOverflowErrors);
}

