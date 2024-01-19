// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include "SpaHeaterCntl.h"

ConsoleTask     consoleTask(Serial);
LedMatrixTask   matrixTask(Serial, 50);
WiFiJoinApTask  wifiJoinApTask(Serial, "SpaHeaterAP", "123456789");
Logger          logger(Serial);
FlashStore<BootRecord, PS_BootRecordBase> bootRecord;
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


// TODO: Add NetworkClient imp



void StartTelnet()
{
    logger.Printf(Logger::RecType::Info, "TELNET (Admin cobsole) starting");
    telnetServer = make_shared<TelnetServer>();
    $Assert(telnetServer != nullptr);
    telnetServer->setup();
    telnetServer->Begin(23);
}


void setup()
{
    matrixTask.setup();
    matrixTask.PutString("S00");

    Serial.begin(9600);
    delay(1000);

    matrixTask.PutString("S01");
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

    RTC.begin();
    logger.Begin(bootRecord.GetRecord().BootCount);

    matrixTask.PutString("S02");
    consoleTask.setup();
    matrixTask.PutString("S03");
    wifiJoinApTask.setup();
    matrixTask.PutString("S04");
    network.setup();
    matrixTask.PutString("S05");
}

void loop() 
{
    consoleTask.loop();
    matrixTask.loop();
    wifiJoinApTask.loop();

    // Network dependent Tasks will get started and given time only after we kow we have a valid
    // wifi config
    if (wifiJoinApTask.IsCompleted())
    {
        static bool firstTime = true;

        $Assert(wifiJoinApTask.IsConfigured());

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

        // Give each wifi dependent Task time
        network.loop();
    }
}
