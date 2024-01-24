// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include <ArduinoMqttClient.h>
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
//      NetworkMonitor
//
//    NetworkMonitor dependent:
//      AdminTelnetServer
//      MqttClient

//  Overall network core of the system.
//
//  Implements state machine for:
//      1) Establishing WIFI client membership
//      2) Creating and maintinence of client and server objects that will track the available of the network
//          2.1) Each of these objects will receive UP/DOWN callbacks
//          2.2) Each will be an ArduinoTask derivation and will thus be setup() (once) and given time for their 
//               state machine via loop()


//*******************************MQTT Workbench******************************************************
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
const char topic[]  = "arduino/simple";

const long interval = 1000;
unsigned long previousMillis = 0;

int xCount = 0;

void tryMQTT()
{
  mqttClient.setUsernamePassword("mqttuser", "mqttpassword");

  if (!mqttClient.connect(IPAddress("192.168.3.48"), 1883)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1);
  }

  Serial.println("You're connected to the MQTT broker!");
  Serial.println();

}

void StartTelnet()
{
    logger.Printf(Logger::RecType::Info, "TELNET (Admin console) starting");
    telnetServer = make_shared<TelnetServer>();
    $Assert(telnetServer != nullptr);
    telnetServer->setup();
    telnetServer->Begin(23);
}

shared_ptr<TcpClient> testTcpClient;


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

    Serial.begin(9600);
    delay(1000);

    matrixTask.setup();
    matrixTask.PutString("S00");

    auto const status = xTaskCreate
    (
        MainThreadEntry,
        static_cast<const char*>("Loop Thread"),
        (1024) / 4,                 /* usStackDepth in words */
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

    matrixTask.PutString("S04");
    tempSensorsConfig.Begin();

    matrixTask.PutString("S05");
    boilerConfig.Begin();
    if (boilerConfig.IsValid() == false)
    {
        boilerConfig.GetRecord()._setPoint = 38.0;
        boilerConfig.GetRecord()._hysteresis = 0.75;
        boilerConfig.Write();
        boilerConfig.Begin();
        $Assert(boilerConfig.IsValid());
    }

    matrixTask.PutString("S06");
    auto const status = xTaskCreate(
        BoilerControllerTask::BoilerControllerThreadEntry,
        static_cast<const char *>("Loop Thread"),
    (1024) / 4,                                     /* usStackDepth in words */
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
}

CmdLine::Status StartTcpProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    printf(CmdStream, "***Start TCP***\n");
    testTcpClient = make_shared<TcpClient>(IPAddress("192.168.3.48"), 1883); // test connect to MQTT in test HA
    testTcpClient->Begin();
    return CmdLine::Status::Ok;
}

CmdLine::Status StopTcpProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    printf(CmdStream, "***Stop TCP***\n");
    testTcpClient->End();
    testTcpClient.reset();
    return CmdLine::Status::Ok;
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
        $Assert(boilerControllerTask.GetHeaterState() == BoilerControllerTask::HeaterState::Halted);

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
    MonitorBoiler();
    
    
    /*
        if ((doMQTT == false) && (WiFi.status() == WL_CONNECTED))
        {
            tryMQTT();
            doMQTT = true;
        }
    */

    /*
        if (doMQTT == true)
        {
            // call poll() regularly to allow the library to send MQTT keep alives which
            // avoids being disconnected by the broker
            mqttClient.poll();

            // to avoid having delays in loop, we'll use the strategy from BlinkWithoutDelay
            // see: File -> Examples -> 02.Digital -> BlinkWithoutDelay for more info
            unsigned long currentMillis = millis();
            
            if (currentMillis - previousMillis >= interval) {
                // save the last time a message was sent
                previousMillis = currentMillis;

                Serial.print("Sending message to topic: ");
                Serial.println(topic);
                Serial.print("hello ");
                Serial.println(xCount);

                // send message, the Print interface can be used to set the message contents
                mqttClient.beginMessage(topic);
                mqttClient.print("hello ");
                mqttClient.print(xCount);
                mqttClient.endMessage();

                Serial.println();

                xCount++;
            }
        }
    */        
}
