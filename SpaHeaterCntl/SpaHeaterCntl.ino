// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include "SpaHeaterCntl.hpp"

// Tasks to add:
//    Not WiFi dependent:
//      DiagLog store and fwd
//      NTP
//
//    TODO:
//      - Change log prints to use correct log levels
//      - Add a log level into system config
//      - Config:
//          - Make records CRC-checked
//          - Use end of 8K for circular log buffer
//      - NetworkTask - make independent of WiFi. Support ethernet and WiFi
//      - Make dual targeted - UNO R4 Minima (Ethernet) and UNO R4 Maxie (WiFi)
//      - Make libraries??
//



//** Main admin console commands
// Command line processors for main menu
CmdLine::Status ClearEEPROMProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    CmdStream.println("Starting EEPROM Erase...");
    wifiJoinApTask.EraseConfig();
    CmdStream.println("EEPROM Erase has completed");
    return CmdLine::Status::Ok;
}

CmdLine::Status SetLedDisplayProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 2)
    {
        return CmdLine::Status::TooManyParameters;
    }

    matrixTask.PutString((char *)(Args[1]));

    return CmdLine::Status::Ok;
}

CmdLine::Status SetRTCDateTime(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 3)
    {
        CmdStream.println("Missing parameter: usage: YYYY-MM-DD HH:MM:SS");
        return CmdLine::Status::UnexpectedParameterCount;
    }

    String dateTimeStr(Args[1]);
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

CmdLine::Status ShowRTCDateTime(Stream &CmdStream, int Argc, char const **Args, void *Context)
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
    ((ConsoleTask *)Context)->Push(configBoilerCmdProcessors[0], LengthOfConfigBoilerCmdProcessors, "BoilerConfig");
    return CmdLine::Status::Ok;
}

CmdLine::Status BoilerProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Push(controlBoilerCmdProcessors[0], LengthOfControlBoilerCmdProcessors, "BoilerControl");
    return CmdLine::Status::Ok;
}

CmdLine::Status ConfigHaMQTTProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Push(haMqttCmdProcessors[0], LengthOfHaMqttCmdProcessors, "ConfigMQTT");
    return CmdLine::Status::Ok;
}

CmdLine::ProcessorDesc consoleTaskCmdProcessors[] =
{
    {SetRTCDateTime, "setTime", "Set the RTC date and time. Format: 'YYYY-MM-DD HH:MM:SS'"},
    {ShowRTCDateTime, "showTime", "Show the current RTC date and time."},
    {ClearEEPROMProcessor, "clearEPROM", "Clear all of the EEPROM"},
    {SetLedDisplayProcessor, "ledDisplay", "Put tring to Led Matrix"},
    {SetWiFiConfigProcessor, "setWiFi", "Set the WiFi Config. Format: <SSID> <Net Password> <Admin Password>"},
    {DisconnectWiFiProcessor, "stopWiFi", "Disconnect WiFi"},
    {RebootProcessor, "reboot", "Reboot the R4"},
    {ShowTempSensorsProcessor, "showSensors", "Show the list of attached temperature sensors"},
    {ConfigBoilerProcessor, "configBoiler", "Start the config of the Boiler"},
    {BoilerProcessor, "boiler", "Start the Boiler console"},
    {ConfigHaMQTTProcessor, "configMQTT", "MQTT/HA related config menu"},
};
int const LengthOfConsoleTaskCmdProcessors = sizeof(consoleTaskCmdProcessors) / sizeof(consoleTaskCmdProcessors[0]);

//* Telnet Admin Console Task - hosts a TelnetServer and a ConsoleTask
class TelnetConsole final : public ArduinoTask
{
private:
    ConsoleTask _console;
    shared_ptr<Server> _server;
    shared_ptr<Client> _client;

public:
    TelnetConsole();
    ~TelnetConsole();
    virtual void setup();
    virtual void loop();
};

//* Telnet Admin Console Task implementation
TelnetConsole::TelnetConsole()
    : _server(nullptr),
      _client(nullptr)
{
}

TelnetConsole::~TelnetConsole()
{
}

void TelnetConsole::setup()
{
    logger.Printf(Logger::RecType::Progress, "TelnetConsole: Starting - listening on port 23...");
    _server = move(NetworkTask::CreateServer(23));
    $Assert(_server.get() != nullptr);
    _server->begin();
}

void TelnetConsole::loop()
{
    enum class State
    {
        StartServer,
        Connected,
    };
    static StateMachineState<State> state(State::StartServer);

    switch ((State)state)
    {
    case State::StartServer:
    {
        if (network.IsAvailable())
        {
            _client = move(NetworkTask::available(_server));
            if (_client.get() != nullptr)
            {
                state.ChangeState(State::Connected);
                return;
            }
        }
    }
    break;

    case State::Connected:
    {
        if (state.IsFirstTime())
        {
            logger.Printf(Logger::RecType::Info, "TelnetConsole: Client connected");
            _console.SetStream(*_client);
            _console.setup();
            _console.begin(consoleTaskCmdProcessors, LengthOfConsoleTaskCmdProcessors, "Main");
        }

        if (_client->connected())
        {
            _console.loop();
        }
        else
        {
            logger.Printf(Logger::RecType::Info, "TelnetConsole: Client disconnected");
            _client->stop();
            _client = nullptr;
            state.ChangeState(State::StartServer);
        }
    }
    break;

    default:
        $FailFast();
    }
}


//* System Instance (config) Record - In persistant storage
#pragma pack(push, 1)
struct BootRecord
{
    uint32_t BootCount;    // Number of times the system has been booted 
};
#pragma pack(pop)




//******************************************************************************************
//** Main logic for entire system
//******************************************************************************************
TaskHandle_t    mainThread;
TaskHandle_t    backgroundThread;
TelnetConsole   telnetConsole;
FlashStore<BootRecord, PS_BootRecordBase> bootRecord;
    static_assert(PS_BootRecordBlkSize >= sizeof(FlashStore<BootRecord, PS_BootRecordBase>));

void EnableRtcAfterPOR()
{
    $Assert(RTC.begin());

    // This strange, and time losing, sequence must be done to make the RTC start each POR
    // and thus retain its contents
    $Assert(!RTC.isRunning());
    RTCTime now;
    RTC.getTime(now);
    RTC.setTimeIfNotRunning(now);
}

//** POR Entry point
void setup()
{
    EnableRtcAfterPOR();        // must be done right after POR to minimize loss of time

    Serial.begin(250000);
    delay(1000);

//    modem.debug(Serial, 0);

    matrixTask.setup();
    matrixTask.PutString("S00");

    auto const status = xTaskCreate
    (
        MainThreadEntry,
        static_cast<const char*>("Loop Thread"),
        (1024 + 500) / 4,           /* usStackDepth in words */
        nullptr,                    /* pvParameters */
        1,                          /* uxPriority */
        &mainThread                 /* pxCreatedTask */
    );

    if (status != pdPASS) 
    {
        Serial.println("Failed to create 'main' thread");
        $FailFast();
    }

    matrixTask.PutString("S01");
    vTaskStartScheduler();
    $FailFast();
}

void MainThreadEntry(void *pvParameters)
{
    Serial.println("*** Main Thread Started ***");

    FinishStart();

    while (true)
    {
        loop();
        taskYIELD();
    }
}

void FinishStart()
{
    // Get out stateless boot time from the config record and increment it; given to the logger
    matrixTask.PutString("S02");
    bootRecord.Begin();
    if (!bootRecord.IsValid())
    {
        Serial.println("bootRecord NOT valid - auto creating");
        // Do auto setup and write
        bootRecord.GetRecord().BootCount = 0;
        bootRecord.Write();
        bootRecord.Begin();
        $Assert(bootRecord.IsValid());
    }
    else
    {
        bootRecord.GetRecord().BootCount++;
        bootRecord.Write();
        bootRecord.Begin();
        $Assert(bootRecord.IsValid());
    }

    matrixTask.PutString("S03");
    logger.Begin(bootRecord.GetRecord().BootCount);
    // logger.SetFilter(Logger::RecType::Progress);         // TODO: Walk through and set the RecType for things that are progress info to be Progress

    //** Logger used for all output from this point on
    matrixTask.PutString("S04");
    tempSensorsConfig.Begin();

    matrixTask.PutString("S05");
    boilerConfig.Begin();
    if (boilerConfig.IsValid() == false)
    {
        boilerConfig.GetRecord()._setPoint = $FtoC(74.0);
        boilerConfig.GetRecord()._hysteresis = 0.75;
        boilerConfig.GetRecord()._mode = BoilerControllerTask::BoilerMode::Off;
        boilerConfig.Write();
        boilerConfig.Begin();
        $Assert(boilerConfig.IsValid());
    }

    matrixTask.PutString("S06");
    auto const status = xTaskCreate(
        BoilerControllerTask::BoilerControllerThreadEntry,
        static_cast<const char *>("Loop Thread"),
        (1024) / 4,                                 /* usStackDepth in words */
        nullptr,                                    /* pvParameters */
        2,                                          /* uxPriority */
        &backgroundThread                           /* pxCreatedTask */
    );

    if (status != pdPASS)
    {
        logger.Printf(Logger::RecType::Critical, "Failed to create 'background' thread");
        $FailFast();
    }

    matrixTask.PutString("S07");
    consoleTask.setup();
    consoleTask.begin(consoleTaskCmdProcessors, LengthOfConsoleTaskCmdProcessors, "Main");
    matrixTask.PutString("S08");

    matrixTask.PutString("S09");
    network.setup();
    matrixTask.PutString("S10");
    network.Begin();
    matrixTask.PutString("S11");
    haMqttClient.setup();
    matrixTask.PutString("S12");
    telnetConsole.setup();
    matrixTask.PutString("S13");
}


//** foreground thread loop
void loop() 
{
    consoleTask.loop();
    telnetConsole.loop();

    //* Auto start the Boiler State Machine when we have a valid config for it
    static bool firstHeaterStateMachineStarted = false;
    if (!firstHeaterStateMachineStarted)
    {
        $Assert(boilerControllerTask.GetStateMachineState() == BoilerControllerTask::StateMachineState::Halted);

        // If Boiler State Machine is never been started, start it iff we have a valid config for it
        if (boilerConfig.IsValid() && boilerConfig.GetRecord().IsConfigured() &&
            tempSensorsConfig.IsValid() && tempSensorsConfig.GetRecord().IsConfigured())
        {
            logger.Printf(Logger::RecType::Progress, "Main: Autostarting Boiler State Machine");
            firstHeaterStateMachineStarted = true;

            // Set all needed to prime the boiler state machine
            boilerControllerTask.SetAllBoilerParametersFromConfig();
            boilerControllerTask.Start();
        }
    }

    network.loop();     // give network a chance to do its thing
    haMqttClient.loop();
}
