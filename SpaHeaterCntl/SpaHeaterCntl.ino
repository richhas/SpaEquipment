// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include "SpaHeaterCntl.hpp"

static_assert(configTOTAL_HEAP_SIZE == 0x1400, "freeRTOS heap is not the correct size");
// See: C:\Users\richhas.MAXIE\AppData\Local\Arduino15\packages\arduino\hardware\renesas_uno\1.0.5\variants\UNO*\defines.txt

// Tasks to add:
//    Not WiFi dependent:
//      DiagLog store and fwd
//
//    TODO:
//      - Change log prints to use correct log levels
//      - Add a log level into system config
//      - Config:
//          - Use end of 8K for circular log buffer
//      - NetworkTask - make independent of WiFi. Support ethernet and WiFi
//      - Make dual targeted - UNO R4 Minima (Ethernet) and UNO R4 Maxie (WiFi)
//         - ARDUINO_UNOR4_WIFI vs ARDUINO_UNOR4_MINIMA
//      - Make libraries??
//      - Add NTP config
//

#if !defined(ARDUINO_UNOR4_WIFI)
#error "This is the wrong board for this code"
#endif

//** Telnet Admin Console Task definitions - hosts a TelnetServer and a ConsoleTask
class TelnetConsole final : public ArduinoTask
{
private:
    ConsoleTask _console;
    shared_ptr<Server> _server;
    shared_ptr<Client> _client;

public:
    TelnetConsole();
    ~TelnetConsole();

protected:
    virtual void setup();
    virtual void loop();
};

//** System Instance (config) Record definition - In persistant storage
#pragma pack(push, 1)
struct BootRecord
{
    uint32_t BootCount;    // Number of times the system has been booted 
};
#pragma pack(pop)


//*** Global state objects
TaskHandle_t    mainThread;
TaskHandle_t    backgroundThread;
TelnetConsole   telnetConsole;
FlashStore<BootRecord, PS_BootRecordBase> bootRecord;
    static_assert(PS_BootRecordBlkSize >= sizeof(FlashStore<BootRecord, PS_BootRecordBase>));

//* Percounter for the overall foreground system loop
PerfCounter perfCounterForMainLoop(uSecSystemClock);


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
        static Timer timer;
        if (state.IsFirstTime())
        {
            timer.SetAlarm(1000);
        }

        if (timer.IsAlarmed())
        {
            timer.SetAlarm(1000);
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
    }
    break;

    case State::Connected:
    {
        if (state.IsFirstTime())
        {
            logger.Printf(Logger::RecType::Info, "TelnetConsole: Client connected");
            _console.SetStream(*_client);
            _console.Setup();
            _console.begin(consoleTaskCmdProcessors, LengthOfConsoleTaskCmdProcessors, "Main");
        }

        if (_client->connected())
        {
            _console.Loop();
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

//** Main admin console commands
// Command line processors for main menu
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

CmdLine::Status RebootProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    printf(CmdStream, "***rebooting***\n");
    CmdStream.flush();
    delay(1000);
    NVIC_SystemReset();
    return CmdLine::Status::Ok;
}

CmdLine::Status BoilerProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Push(controlBoilerCmdProcessors[0], LengthOfControlBoilerCmdProcessors, "BoilerControl");
    return CmdLine::Status::Ok;
}

CmdLine::Status HaMQTTProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Push(haMqttCmdProcessors[0], LengthOfHaMqttCmdProcessors, "MQTT-HA");
    return CmdLine::Status::Ok;
}

CmdLine::Status StartNetworkCmdProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Push(networkTaskCmdProcessors[0], LengthOfNetworkTaskCmdProcessors, "Network");
    return CmdLine::Status::Ok;
}

CmdLine::Status ShowPerfCounters(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    printf(CmdStream, "Perf Counter: Overall (main) loop:\n");
    perfCounterForMainLoop.Print(CmdStream, 4);

    printf(CmdStream, "Perf Counter: Network loop:\n");
    network.GetPerfCounter().Print(CmdStream, 4);

    printf(CmdStream, "Perf Counter: MQTT loop:\n");
    haMqttClient.GetPerfCounter().Print(CmdStream, 4);

    printf(CmdStream, "Perf Counter: Console loop:\n");
    consoleTask.GetPerfCounter().Print(CmdStream, 4);

    printf(CmdStream, "Perf Counter: Telnet Console loop:\n");
    telnetConsole.GetPerfCounter().Print(CmdStream, 4);

    printf(CmdStream, "Perf Counter: NTP Client loop:\n");
    ntpClient.GetPerfCounter().Print(CmdStream, 4);

    printf(CmdStream, "Perf Counter: Boiler Background Task loop:\n");
    boilerControllerTask.GetPerfCounter().Print(CmdStream, 4);

    return CmdLine::Status::Ok;
}

CmdLine::Status ResetPerfCounters(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    perfCounterForMainLoop.Reset();
    network.GetPerfCounter().Reset();
    haMqttClient.GetPerfCounter().Reset();
    consoleTask.GetPerfCounter().Reset();
    telnetConsole.GetPerfCounter().Reset();
    ntpClient.GetPerfCounter().Reset();
    boilerControllerTask.GetPerfCounter().Reset();
    return CmdLine::Status::Ok;
}

CmdLine::ProcessorDesc consoleTaskCmdProcessors[] =
{
    {SetRTCDateTime, "setTime", "Set the RTC date and time. Format: 'YYYY-MM-DD HH:MM:SS'"},
    {ShowRTCDateTime, "showTime", "Show the current RTC date and time."},
    {RebootProcessor, "reboot", "Reboot the R4"},
    {BoilerProcessor, "boiler", "Boiler related menu"},
    {HaMQTTProcessor, "mqtt", "MQTT/HA related menu"},
    {StartNetworkCmdProcessor, "network", "Network related menu"},
    {ShowPerfCounters, "perf", "Show performance counters"},
    {ResetPerfCounters, "perfReset", "Reset performance counters"},
};
int const LengthOfConsoleTaskCmdProcessors = sizeof(consoleTaskCmdProcessors) / sizeof(consoleTaskCmdProcessors[0]);





//******************************************************************************************
//** Main logic for entire system
//******************************************************************************************

//** POR Entry point
void setup()
{
    // must be done right after POR to minimize loss of time
    $Assert(RTC.begin());

    // This strange, and time losing, sequence must be done to make the RTC start each POR
    // and thus retain its contents
    $Assert(!RTC.isRunning());
    RTCTime now;
    RTC.getTime(now);
    RTC.setTimeIfNotRunning(now);

    // Start our uSecSystemClock
    uSecSystemClock.Reset();

    Serial.begin(250000);
    delay(1000);


    //    modem.debug(Serial, 0);

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

    vTaskStartScheduler();
    $FailFast();
}

void MainThreadEntry(void *pvParameters)
{
    Serial.println("*** Main Thread Started ***");

    FinishStart();

    // Start the main loop PerfCounter
    perfCounterForMainLoop.Reset();

    while (true)
    {
        perfCounterForMainLoop.Start();     // Start the perf counter sample for the overall main loop
        loop();
        perfCounterForMainLoop.Stop();    // Stop the perf counter and Accumulate the sample

        taskYIELD();
    }
}

void FinishStart()
{
    // Get out stateless boot time from the config record and increment it; given to the logger
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

    logger.Begin(bootRecord.GetRecord().BootCount);
    // logger.SetFilter(Logger::RecType::Progress);         // TODO: Walk through and set the RecType for things that are progress info to be Progress

    //** Logger used for all output from this point on
    tempSensorsConfig.Begin();

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

    consoleTask.Setup();
    consoleTask.begin(consoleTaskCmdProcessors, LengthOfConsoleTaskCmdProcessors, "Main");
    network.Setup();
    network.Begin();
    haMqttClient.Setup();
    telnetConsole.Setup();
    ntpClient.Setup();
}

//byte padBuffer[1900 + 3072];     // with rtos heap of 5K (configTOTAL_HEAP_SIZE == 0x1400)

//** foreground thread loop
void loop() 
{
    //memset(padBuffer, 0, sizeof(padBuffer));
    //padBuffer;
    consoleTask.Loop();
    telnetConsole.Loop();

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

    network.Loop();     // give network a chance to do its thing
    haMqttClient.Loop();
    ntpClient.Loop();
}
