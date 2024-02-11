// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include "SpaHeaterCntl.h"




ConsoleTask     consoleTask(Serial);
LedMatrixTask   matrixTask(Serial, 50);
WiFiJoinApTask  wifiJoinApTask(Serial, "SpaHeaterAP", "123456789");
Logger          logger(Serial);
FlashStore<BootRecord, PS_BootRecordBase> bootRecord;
    static_assert(PS_BootRecordBlkSize >= sizeof(FlashStore<BootRecord, PS_BootRecordBase>));
FlashStore<TempSensorsConfig, PS_TempSensorsConfigBase> tempSensorsConfig;
    static_assert(PS_TempSensorsConfigBlkSize >= sizeof(FlashStore<TempSensorsConfig, PS_TempSensorsConfigBase>));
FlashStore<BoilerConfig, PS_BoilerConfigBase> boilerConfig;
    static_assert(PS_BoilerConfigBlkSize >= sizeof(FlashStore<BoilerConfig, PS_BoilerConfigBase>));

NetworkTask     network;
shared_ptr<TelnetServer> telnetServer;


// Tasks to add:
//    Not WiFi dependent:
//      Thermo
//      Boiler
//      DiagLog
//
//    WiFi dependent:
//
//    TODO:
//      - Change log prints to use correct log levels
//      - Add a log level into system config
//      - Add MQTT related config console commands
//

void StartTelnet()
{
    logger.Printf(Logger::RecType::Info, "TELNET (Admin console) starting");
    telnetServer = make_shared<TelnetServer>();
    $Assert(telnetServer != nullptr);
    telnetServer->setup();
    telnetServer->Begin(23);
}

shared_ptr<TcpClient> testTcpClient;


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

CmdLine::Status DumpProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
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
    ((ConsoleTask *)Context)->Push(configBoilerCmdProcessors[0], LengthOfConfigBoilerCmdProcessors, "BoilerConfig", Context);

    //    ((ConsoleTask *)Context)->StartBoilerConfig();
    return CmdLine::Status::Ok;
}

CmdLine::Status BoilerProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Push(controlBoilerCmdProcessors[0], LengthOfControlBoilerCmdProcessors, "BoilerControl", Context);

    //    ((ConsoleTask *)Context)->StartBoilerControl();
    return CmdLine::Status::Ok;
}

bool TcpIsRunning = false;

CmdLine::Status StartTcpProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (TcpIsRunning)
    {
        printf(CmdStream, "***TCP already running***\n");
        return CmdLine::Status::Ok;
    }

    TcpIsRunning = true;
    printf(CmdStream, "***Start TCP***\n");
    testTcpClient = make_shared<TcpClient>(IPAddress("192.168.3.48"), 1883); // test connect to MQTT in test HA
    testTcpClient->Begin();
    return CmdLine::Status::Ok;
}

CmdLine::Status StopTcpProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (!TcpIsRunning)
    {
        printf(CmdStream, "***TCP not running***\n");
        return CmdLine::Status::Ok;
    }
    TcpIsRunning = false;
    printf(CmdStream, "***Stop TCP***\n");
    testTcpClient->End();
    testTcpClient.reset();
    return CmdLine::Status::Ok;
}

CmdLine::Status ConfigHaMQTTProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Push(haMqttCmdProcessors[0], LengthOfHaMqttCmdProcessors, "ConfigMQTT", Context);
    return CmdLine::Status::Ok;
}

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
        {ConfigHaMQTTProcessor, "configMQTT", "MQTT/HA related config menu"},
};
int const LengthOfConsoleTaskCmdProcessors = sizeof(consoleTaskCmdProcessors) / sizeof(consoleTaskCmdProcessors[0]);


//******************************************************************************************
BoilerControllerTask boilerControllerTask;
TaskHandle_t mainThread;
TaskHandle_t backgroundThread;

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
        Serial.println("Failed to create 'background' thread");
        $FailFast();
    }

    matrixTask.PutString("S07");
    consoleTask.setup();
    matrixTask.PutString("S08");

    matrixTask.PutString("S09");
    wifiJoinApTask.setup();
    matrixTask.PutString("S10");
    network.setup();
    matrixTask.PutString("S11");
    haMqttClient.setup();
    matrixTask.PutString("S12");
}

void SetAllBoilerParametersFromConfig()
{
    BoilerControllerTask::TargetTemps temps;
    temps._setPoint = boilerConfig.GetRecord()._setPoint;
    temps._hysteresis = boilerConfig.GetRecord()._hysteresis;

    BoilerControllerTask::TempSensorIds sensorIds;
    sensorIds._ambiantTempSensorId = tempSensorsConfig.GetRecord()._ambiantTempSensorId;
    sensorIds._boilerInTempSensorId = tempSensorsConfig.GetRecord()._boilerInTempSensorId;
    sensorIds._boilerOutTempSensorId = tempSensorsConfig.GetRecord()._boilerOutTempSensorId;

    boilerControllerTask.SetTargetTemps(temps);
    boilerControllerTask.SetTempSensorIds(sensorIds);
    boilerControllerTask.SetMode(boilerConfig.GetRecord()._mode);
}

void MonitorBoiler()
{
    static uint32_t lastSeq = 0;
    static Timer timer(1000);
    static BoilerControllerTask::TempertureState state;

    if (timer.IsAlarmed())
    {
        auto const seq = boilerControllerTask.GetHeaterStateSequence();
        if (seq != lastSeq)
        {
            if (seq != 0)
            {
                BoilerControllerTask::TempertureState newState;
                boilerControllerTask.GetTempertureState(newState);

                if (newState._boilerInTemp != state._boilerInTemp)
                    printf(Serial, "Main: Changed: Boiler In Temp: %f\n", newState._boilerInTemp);
                if (newState._boilerOutTemp != state._boilerOutTemp)
                    printf(Serial, "Main: Changed: Boiler Out Temp: %f\n", newState._boilerOutTemp);
                if (newState._ambiantTemp != state._ambiantTemp)
                    printf(Serial, "Main: Changed: Ambiant Temp: %f\n", newState._ambiantTemp);
               if (newState._setPoint != state._setPoint)
                    printf(Serial, "Main: Changed: Set Point: %f\n", newState._setPoint);          
                if (newState._hysteresis != state._hysteresis)
                    printf(Serial, "Main: Changed: Hysteresis: %f\n", newState._hysteresis);
                if (newState._heaterOn != state._heaterOn)
                    printf(Serial, "Main: Changed: Heater On: %s\n", newState._heaterOn ? "true" : "false");

                state = newState;
            }
            else
            {
                boilerControllerTask.GetTempertureState(state);
            }
            lastSeq = seq;
        }

        timer.SetAlarm(1000);
    }   

}

bool doMQTT = false;

void loop() 
{
    consoleTask.loop();
    matrixTask.loop();
    wifiJoinApTask.loop();

    static bool firstHeaterStateMachineStarted = false;
    if (!firstHeaterStateMachineStarted)
    {
        $Assert(boilerControllerTask.GetStateMachineState() == BoilerControllerTask::StateMachineState::Halted);

        // If Boiler State Machine is never been started, start it iff we have a valid config for it
        if (boilerConfig.IsValid() && boilerConfig.GetRecord().IsConfigured() &&
            tempSensorsConfig.IsValid() && tempSensorsConfig.GetRecord().IsConfigured())
        {
            logger.Printf(Logger::RecType::Info, "Main: Autostarting Boiler State Machine");
            firstHeaterStateMachineStarted = true;

            // Set all needed to prime the boiler state machine
            SetAllBoilerParametersFromConfig();
            boilerControllerTask.Start();
        }
    }

    // Network dependent Tasks will get started and given time only after we kow we have a valid
    // wifi config
    if (!wifiJoinApTask.IsCompleted())
        return;
    $Assert(wifiJoinApTask.IsConfigured()); // should be true by now

    //** Only Network dependent Tasks will get started and given time only after we kow we have a valid wifi config
    static bool firstTime = true;

    if (firstTime)
    {
        // First time we've had the wifi config completed - let wifi dependent Tasks set up
        firstTime = false;

        const char* ssid;
        const char* password;

        wifiJoinApTask.GetNetworkConfig(ssid, password);
        network.Begin(ssid, password);

        StartTelnet();
    }

    network.loop();     // give network a chance to do its thing
    // MonitorBoiler();
    haMqttClient.loop();
}
