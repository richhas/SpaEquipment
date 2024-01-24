// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// ConsoleTask implementation

#include "SpaHeaterCntl.h"

// Command line processors
CmdLine::Status ClearEEPROMProcessor(Stream& CmdStream, int Argc, char const** Args, void* Context)
{
    CmdStream.println("Starting EEPROM Erase...");
    wifiJoinApTask.EraseConfig();
    CmdStream.println("EEPROM Erase has completed");
    return CmdLine::Status::Ok;
}

CmdLine::Status SetLedDisplayProcessor(Stream& CmdStream, int Argc, char const** Args, void* Context)
{
    if (Argc != 2)
    {
        return CmdLine::Status::TooManyParameters;
    }

    matrixTask.PutString((char*)(Args[1]));

    return CmdLine::Status::Ok;
}

CmdLine::Status DumpProcessor(Stream& CmdStream, int Argc, char const** Args, void* Context)
{
    if (Argc > 2)
    {
        return CmdLine::Status::TooManyParameters;
    }

    if (strcmp(Args[1], "wificonfig") == 0)
    {
        wifiJoinApTask.DumpConfig(CmdStream);
        CmdStream.println();
    }
    return CmdLine::Status::Ok;
}

CmdLine::Status SetRTCDateTime(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 3)
    {
        CmdStream.println("Missing parameter: usage: YYYY-MM-DD HH:MM:SS");
        return CmdLine::Status::UnexpectedParameterCount;
    }

    String    dateTimeStr(Args[1]);
    dateTimeStr += " ";
    dateTimeStr += Args[2];

    struct tm timeToSet;
    memset(&timeToSet, 0, sizeof(struct tm));
    if (strptime(dateTimeStr.c_str(), "%Y-%m-%d %H:%M:%S", &timeToSet) == NULL) 
    {
        CmdStream.println("Failed to parse date and time");
        return CmdLine::Status::CommandFailed;    
    }

    RTCTime newTime(timeToSet);
    if (!RTC.setTime(newTime))
    {
        CmdStream.println("RTC.setTime() failed!");
        return CmdLine::Status::CommandFailed;    
    }

    RTCTime currentTime;
    if (!RTC.getTime(currentTime))
    {
        CmdStream.println("RTC.getTime() failed!");
        return CmdLine::Status::CommandFailed;    
    }
    printf(CmdStream, "RTC Date and Time have been set. Currently: %s\n", currentTime.toString().c_str());

    return CmdLine::Status::Ok;
}

CmdLine::Status ShowRTCDateTime(Stream& CmdStream, int Argc, char const** Args, void* Context)
{
    if (Argc > 1)
    {
        return CmdLine::Status::TooManyParameters;
    }

    RTCTime currentTime;
    if (!RTC.getTime(currentTime))
    {
        CmdStream.println("RTC.getTime() failed!");
        return CmdLine::Status::CommandFailed;    
    }
    printf(CmdStream, "Current RTC Date and Time are: %s\n", currentTime.toString().c_str());

    return CmdLine::Status::Ok;
}

CmdLine::Status SetWiFiConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 4)
    {
        return CmdLine::Status::UnexpectedParameterCount;
    }

    wifiJoinApTask.SetConfig(Args[1], Args[2], Args[3]);

    return CmdLine::Status::Ok;
}

CmdLine::Status DisconnectWiFiProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    WiFi.disconnect();
    return CmdLine::Status::Ok;
}

CmdLine::Status ShowTelnetInfoProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    printf(CmdStream, "There are %u clients\n", telnetServer->GetNumberOfClients());
    return CmdLine::Status::Ok;
}

CmdLine::Status RebootProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    printf(CmdStream, "***rebooting***\n");
    CmdStream.flush();
    delay(1000);
    NVIC_SystemReset();
    return CmdLine::Status::Ok;
}

CmdLine::Status ShowTempSensorsProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    auto const sensors = boilerControllerTask.GetTempSensors();

    for (auto const &sensor : sensors)
    {
        for (uint8_t *byte = ((uint8_t *)(&sensor)); byte < ((uint8_t *)(&sensor)) + sizeof(sensor); ++byte)
        {
            CmdStream.print(" ");
            CmdStream.print(*byte);
        }
        CmdStream.println();
    }

    return CmdLine::Status::Ok;
}

CmdLine::Status ConfigBoilerProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->StartBoilerConfig();
}

CmdLine::Status BoilerProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->StartBoilerControl();
}

extern CmdLine::Status StartTcpProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context);
extern CmdLine::Status StopTcpProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context);

CmdLine::ProcessorDesc consoleTaskCmdProcessors[] =
{
    {SetRTCDateTime, "setTime", "Set the RTC date and time. Format: 'YYYY-MM-DD HH:MM:SS'"},
    {ShowRTCDateTime, "showTime", "Show the current RTC date and time."},
    {ClearEEPROMProcessor, "clearEPROM", "Clear all of the EEPROM"},
    {SetLedDisplayProcessor, "ledDisplay", "Put tring to Led Matrix"},
    {DumpProcessor, "dump", "Dump internal state"},
    {SetWiFiConfigProcessor, "setWiFi", "Set the WiFi Config. Format: <SSID> <Net Password> <Admin Password>"},
    {DisconnectWiFiProcessor, "stopWiFi", "Disconnect WiFi"},
    {ShowTelnetInfoProcessor, "showTelnet", "Show telnet info"},
    {RebootProcessor, "reboot", "Reboot the R4"},
    {StartTcpProcessor, "startTCP", "Start TCP Client Test"},
    {StopTcpProcessor, "stopTCP", "Stop TCP Client Test"},
    {ShowTempSensorsProcessor, "showSensors", "Show the list of attached temperature sensors"},
    {ConfigBoilerProcessor, "configBoiler", "Start the config of the Boiler"},
    {BoilerProcessor, "boiler", "Start the Boiler console"},
};

CmdLine::Status ExitBoilerConfigProcessor(Stream &CmdStream, int Argc, char const** Args, void* Context)
{
    ((ConsoleTask*)Context)->EndBoilerConfig();
    return CmdLine::Status::Ok;
}

CmdLine::Status ShowBoilerConfigProcessor(Stream &CmdStream, int Argc, char const** Args, void* Context)
{
    return CmdLine::Status::Ok;
}

CmdLine::Status AssignTempConfigProcessor(Stream &CmdStream, int Argc, char const** Args, void* Context)
{
    if (Argc != 3)
    {
        return CmdLine::Status::UnexpectedParameterCount;
    }

    int sensorNumber = atoi(Args[1]);
    if ((sensorNumber < 1) || (sensorNumber > boilerControllerTask.GetTempSensors().size()))
    {
        CmdStream.println("Invalid sensor number");
        return CmdLine::Status::CommandFailed;
    }

    auto const sensorId = boilerControllerTask.GetTempSensors()[sensorNumber - 1];
    
    if (strcmp(Args[2], "ambiant") == 0)
    {
        if ((sensorId == tempSensorsConfig.GetRecord()._boilerInTempSensorId) || 
            (sensorId == tempSensorsConfig.GetRecord()._boilerOutTempSensorId))
        {
            CmdStream.println("Sensor is already assigned to boilerIn or boilerOut");
            return CmdLine::Status::CommandFailed;
        }

        tempSensorsConfig.GetRecord()._ambiantTempSensorId = sensorId;
        tempSensorsConfig.Write();
    }
    else if (strcmp(Args[2], "boilerIn") == 0)
    {
        if ((sensorId == tempSensorsConfig.GetRecord()._ambiantTempSensorId) || 
            (sensorId == tempSensorsConfig.GetRecord()._boilerOutTempSensorId))
        {
            CmdStream.println("Sensor is already assigned to ambiant or boilerOut");
            return CmdLine::Status::CommandFailed;
        }

        tempSensorsConfig.GetRecord()._boilerInTempSensorId = sensorId;
        tempSensorsConfig.Write();
    }
    else if (strcmp(Args[2], "boilerOut") == 0)
    {
        if ((sensorId == tempSensorsConfig.GetRecord()._ambiantTempSensorId) || 
            (sensorId == tempSensorsConfig.GetRecord()._boilerInTempSensorId))
        {
            CmdStream.println("Sensor is already assigned to ambiant or boilerIn");
            return CmdLine::Status::CommandFailed;
        }

        tempSensorsConfig.GetRecord()._boilerOutTempSensorId = sensorId;
        tempSensorsConfig.Write();
    }
    else
    {
        CmdStream.println("Invalid sensor function");
        return CmdLine::Status::CommandFailed;
    }
    return CmdLine::Status::Ok;
}

CmdLine::Status EraseTempConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    tempSensorsConfig.Erase();
    return CmdLine::Status::Ok;
}

CmdLine::Status SetBoilerTargetTempInFConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 2)
    {
        return CmdLine::Status::UnexpectedParameterCount;
    }

    float temp = atof(Args[1]);
    if ((temp < 0.0) || (temp > 212.0))
    {
        CmdStream.println("Invalid temperature");
        return CmdLine::Status::CommandFailed;
    }

    boilerConfig.GetRecord()._setPoint = $FtoC(temp);
    boilerConfig.Write();

    return CmdLine::Status::Ok;
}

CmdLine::Status SetBoilerTargetTempInCConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 2)
    {
        return CmdLine::Status::UnexpectedParameterCount;
    }

    float temp = atof(Args[1]);
    if ((temp < 0.0) || (temp > 100.0))
    {
        CmdStream.println("Invalid temperature");
        return CmdLine::Status::CommandFailed;
    }

    boilerConfig.GetRecord()._setPoint = temp;
    boilerConfig.Write();

    return CmdLine::Status::Ok;
}

CmdLine::Status SetBoilerHysteresisConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 2)
    {
        return CmdLine::Status::UnexpectedParameterCount;
    }

    float hysterisis = atof(Args[1]);
    if ((hysterisis < 0) || (hysterisis > 100))
    {
        CmdStream.println("Invalid hysteresis");
        return CmdLine::Status::CommandFailed;
    }

    boilerConfig.GetRecord()._hysteresis = hysterisis;
    boilerConfig.Write();

    return CmdLine::Status::Ok;
}


CmdLine::ProcessorDesc configBoilerCmdProcessors[] =
    {
        {ExitBoilerConfigProcessor, "exit", "Exit the config of the boiler"},
        {ShowBoilerConfigProcessor, "show", "Show current boiler config and detected sensor list"},
        {AssignTempConfigProcessor, "assign", "Assign sensor to function. Format: assign <sensor number> 'ambiant'|'boilerIn'|'boilerOut'"},
        {EraseTempConfigProcessor, "erase", "Erase the boiler's temperture sensor assignment config"},
        {SetBoilerTargetTempInFConfigProcessor, "setTempF", "Set the boiler's target temperature in degrees F. Format: setTempF <temp>"},
        {SetBoilerTargetTempInCConfigProcessor, "setTempC", "Set the boiler's target temperature in degrees C. Format: setTempC <temp>"},
        {SetBoilerHysteresisConfigProcessor, "setHysteresis", "Set the boiler's hysteresis. Format: setHysteresis <hysteresis>"},
};

CmdLine::Status ExitBoilerControlProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->EndBoilerControl();
    return CmdLine::Status::Ok;
}

CmdLine::Status ShowBoilerControlProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    return CmdLine::Status::Ok;
}

CmdLine::Status SetBoilerParamsControlProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 1)
    {
        printf(CmdStream, "Set boiler temp. Usage: setTemp <target> <hystersis>");
        return CmdLine::Status::UnexpectedParameterCount;
    }

    if (!boilerConfig.IsValid() || !tempSensorsConfig.IsValid())
    {
        CmdStream.println("Boiler config is not valid");
        return CmdLine::Status::CommandFailed;
    }

    SetAllBoilerParametersFromConfig(); 

    return CmdLine::Status::Ok;
}

CmdLine::Status StartBoilerControlProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 1)
    {
        printf(CmdStream, "Start boiler. Usage: start");
        return CmdLine::Status::UnexpectedParameterCount;
    }

    if (boilerControllerTask.IsBusy() || (boilerControllerTask.GetHeaterState() != BoilerControllerTask::HeaterState::Halted))
    {
        CmdStream.println("Boiler is not ready");
        return CmdLine::Status::CommandFailed;
    }

    boilerControllerTask.Start();

    return CmdLine::Status::Ok;
}

CmdLine::Status StopBoilerControlProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 1)
    {
        printf(CmdStream, "Stop boiler. Usage: stop");
        return CmdLine::Status::UnexpectedParameterCount;
    }

    if (boilerControllerTask.IsBusy() || (boilerControllerTask.GetHeaterState() != BoilerControllerTask::HeaterState::Running))
    {
        CmdStream.println("Boiler is not ready");
        return CmdLine::Status::CommandFailed;
    }

    boilerControllerTask.Stop();

    return CmdLine::Status::Ok;
}

CmdLine::Status ResetBoilerControlProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 1)
    {
        printf(CmdStream, "Reset boiler. Usage: reset");
        return CmdLine::Status::UnexpectedParameterCount;
    }

    if (boilerControllerTask.IsBusy() || (boilerControllerTask.GetHeaterState() != BoilerControllerTask::HeaterState::Faulted))
    {
        CmdStream.println("Boiler is not ready");
        return CmdLine::Status::CommandFailed;
    }

    boilerControllerTask.Reset();

    return CmdLine::Status::Ok;
}

CmdLine::Status ClearOneWireStatsControlProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 1)
    {
        printf(CmdStream, "Clear OneWire Bus Stats. Usage: clearOWStats");
        return CmdLine::Status::UnexpectedParameterCount;
    }

    boilerControllerTask.ClearOneWireBusStats();

    return CmdLine::Status::Ok;
}

CmdLine::ProcessorDesc controlBoilerCmdProcessors[] =
    {
        {AssignTempConfigProcessor, "assign", "Assign sensor to function. Format: assign <sensor number> 'ambiant'|'boilerIn'|'boilerOut'"},
        {SetBoilerTargetTempInFConfigProcessor, "setTempF", "Set the boiler's target temperature config in degrees F. Format: setTempF <temp>"},
        {SetBoilerTargetTempInCConfigProcessor, "setTempC", "Set the boiler's target temperature config in degrees C. Format: setTempC <temp>"},
        {SetBoilerHysteresisConfigProcessor, "setHysteresis", "Set the boiler's hysteresis in config. Format: setHysteresis <hysteresis>"},
        {SetBoilerParamsControlProcessor, "setBoilerConfig", "Set boiler's parameters from its config. Usage: setBoilerConfig"},
        {StartBoilerControlProcessor, "Start", "Start the boiler state machine - only if it is Faulted or Stopped. Usage: setBoilerConfig"},
        {StopBoilerControlProcessor, "Stop", "Stop the boiler state machine - only if it is Running. Usage: setBoilerConfig"},
        {ResetBoilerControlProcessor, "Reset", "Reset the boiler state machine - only if it is Faulted. Usage: setBoilerConfig"},
        {ShowBoilerControlProcessor, "show", "Show current boiler state"},
        {ClearOneWireStatsControlProcessor, "clearOWStats", "Clear the OneWire Bus Stats"},
        {ExitBoilerControlProcessor, "exit", "Exit the control of the boiler"},
};

ConsoleTask::ConsoleTask(Stream &StreamToUse)
    : _stream(StreamToUse),
      _state(State::EnterMainMenu)
{  
}

ConsoleTask::~ConsoleTask() 
{ 
}

void ConsoleTask::setup()
{
    logger.Printf(Logger::RecType::Info, "ConsoleTask is Active");
}

void ConsoleTask::loop() 
{
    switch (_state)
    {
        case State::EnterMainMenu:
        {
            _cmdLine.begin(
                _stream,
                &consoleTaskCmdProcessors[0],
                sizeof(consoleTaskCmdProcessors) / sizeof(CmdLine::ProcessorDesc),
                "Main",
                this);

            _cmdLine.ShowHelp();
            _stream.println();
            _stream.println();

            _state = State::MainMenu;
        }
        break;

        case State::MainMenu:
        {
            if (_cmdLine.IsReady())
            {
                //printf(_output, "\rCommand Executed\n");
            }
        }
        break;

        case State::EnterBoilerConfig:
        {
            _cmdLine.end();
            _cmdLine.begin(
                _stream,
                &configBoilerCmdProcessors[0],
                sizeof(configBoilerCmdProcessors) / sizeof(CmdLine::ProcessorDesc),
                "BoilerConfig",
                this);

            _cmdLine.ShowHelp();
            _stream.println();
            _stream.println();
            ShowCurrentBoilerConfig();

            _state = State::BoilerConfig;
        }
        break;

        case State::BoilerConfig:
        {
            if (_cmdLine.IsReady())
            {
                if (_state == State::BoilerConfig)
                {
                    _stream.println();
                    _stream.println();
                    ShowCurrentBoilerConfig();
                }
                else
                {
                    // Leaving state
                    _cmdLine.end();
                }
            }
        }
        break;

        case State::EnterBoilerControl:
        {
            _cmdLine.end();
            _cmdLine.begin(
                _stream,
                &controlBoilerCmdProcessors[0],
                sizeof(controlBoilerCmdProcessors) / sizeof(CmdLine::ProcessorDesc),
                "BoilerControl",
                this);

            _cmdLine.ShowHelp();
            _stream.println();
            _stream.println();
            ShowCurrentBoilerState();

            _state = State::BoilerControl;
        }
        break;

        case State::BoilerControl:
        {
            if (_cmdLine.IsReady())
            {
                if (_state == State::BoilerControl)
                {
                    _stream.println();
                    _stream.println();
                    ShowCurrentBoilerState();
                }
                else
                {
                    // Leaving state
                    _cmdLine.end();
                }
            }
        }
        break;

        default:
        {
            $FailFast();
        }
        break;
    }
}

void ConsoleTask::StartBoilerConfig()
{
    _state = State::EnterBoilerConfig;
}

void ConsoleTask::EndBoilerConfig()
{
    _state = State::EnterMainMenu;
}

void ConsoleTask::ShowCurrentBoilerConfig(int PostLineFeedCount)
{
    printf(_stream, "Temp Sensors Configured:\n");
    if (tempSensorsConfig.IsValid())
    {
        if (tempSensorsConfig.GetRecord().IsSensorIdValid(tempSensorsConfig.GetRecord()._ambiantTempSensorId))
        {
            printf(_stream, "   Ambiant Temp Sensor: %" $PRIX64 "\n", To$PRIX64(tempSensorsConfig.GetRecord()._ambiantTempSensorId));
        }
        else
        {
            printf(_stream, "   Ambiant Temp Sensor: Not Configured\n");
        }

        if (tempSensorsConfig.GetRecord().IsSensorIdValid(tempSensorsConfig.GetRecord()._boilerInTempSensorId))
        {
            printf(_stream, "   Boiler In Temp Sensor: %" $PRIX64 "\n", To$PRIX64(tempSensorsConfig.GetRecord()._boilerInTempSensorId));
        }
        else
        {
            printf(_stream, "   Boiler In Temp Sensor: Not Configured\n");
        }

        if (tempSensorsConfig.GetRecord().IsSensorIdValid(tempSensorsConfig.GetRecord()._boilerOutTempSensorId))
        {
            printf(_stream, "   Boiler Out Temp Sensor: %" $PRIX64 "\n", To$PRIX64(tempSensorsConfig.GetRecord()._boilerOutTempSensorId));
        }
        else
        {
            printf(_stream, "   Boiler Out Temp Sensor: Not Configured\n");
        }

        if (tempSensorsConfig.GetRecord().IsConfigured())
        {
            printf(_stream, "   Fully Configured\n");
        }
        else
        {
            printf(_stream, "   Not Fully Configured\n");
        }
    }
    else
    {
        printf(_stream, "   No Temp Sensors Configured\n");
    }

    printf(_stream, "Temp Sensors Discovered:\n");
    auto const sensors = boilerControllerTask.GetTempSensors();
    int i = 0;

    for (auto const &sensor : sensors)
    {
        i++;
        printf(_stream, "   %d) %" $PRIX64 "  --  ", i, To$PRIX64(sensor));
        for (uint8_t *byte = ((uint8_t *)(&sensor)); byte < ((uint8_t *)(&sensor)) + sizeof(sensor); ++byte)
        {
            printf(_stream, " %d", *byte);
        }
        _stream.println();
    }

    printf(_stream, "Boiler Config:\n");

    float       temp = boilerConfig.GetRecord()._setPoint;
    float       hysterisis = boilerConfig.GetRecord()._hysteresis;
    float       lowTemp = temp - hysterisis;
    float       highTemp = temp + hysterisis;

    printf(_stream, "   Set Point: %3.2fC (%3.2fF)\n", temp, $CtoF(temp));
    printf(_stream, "   Hysteresis: %2.2f (%3.2f-%3.2fC %3.2f-%3.2fF)\n", hysterisis, lowTemp, highTemp, $CtoF(lowTemp), $CtoF(highTemp));

    while (PostLineFeedCount-- > 0)
    {
        _stream.println();
    }
}

void ConsoleTask::StartBoilerControl()
{
    _state = State::EnterBoilerControl;
}

void ConsoleTask::EndBoilerControl()
{
    _state = State::EnterMainMenu;
}

void ConsoleTask::ShowCurrentBoilerState()
{
    ShowCurrentBoilerConfig(0);

    printf(_stream, "Boiler State: \n    HeaterState: %s\n    fReason: %s\n    Command: %s\n",
           BoilerControllerTask::GetHeaterStateDescription(boilerControllerTask.GetHeaterState()),
           BoilerControllerTask::GetFaultReasonDescription(boilerControllerTask.GetFaultReason()),
           BoilerControllerTask::GetCommandDescription(boilerControllerTask.GetCommand()));

    /* get and display the: TemperatureState, TargetTemps, and TempSensorIds */
    BoilerControllerTask::TempertureState tempState;
    boilerControllerTask.GetTempertureState(tempState);
    BoilerControllerTask::DisplayTemperatureState(_stream, tempState, "    ");

    BoilerControllerTask::TargetTemps targetTemps;
    boilerControllerTask.GetTargetTemps(targetTemps);
    BoilerControllerTask::DisplayTargetTemps(_stream, targetTemps, "    ");

    BoilerControllerTask::TempSensorIds tempSensorIds;
    boilerControllerTask.GetTempSensorIds(tempSensorIds);
    BoilerControllerTask::DisplayTempSensorIds(_stream, tempSensorIds, "    ");

    printf(_stream, "    OneWireBusStats:\n");
    BoilerControllerTask::OneWireBusStats busStats;
    boilerControllerTask.GetOneWireBusStats(busStats);
    BoilerControllerTask::DisplayOneWireBusStats(_stream, busStats, "        ");

    _stream.println();
    _stream.println();
}