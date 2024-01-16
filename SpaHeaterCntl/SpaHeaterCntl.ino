// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include "SpaHeaterCntl.h"

ConsoleTask     consoleTask(Serial);
LedMatrixTask   matrixTask(Serial, 50);
WiFiJoinApTask  wifiJoinApTask(Serial, "SpaHeaterAP", "123456789");

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



#pragma pack(push, 1)
struct BootRecord
{
    uint32_t        BootCount;
};
#pragma pack(pop)

FlashStore<BootRecord, PS_BootRecordBase>   bootRecord;
Logger                                      logger(Serial);


void setup()
{
    matrixTask.setup();
    matrixTask.PutString("S00");

    Serial.begin(9600);
    delay(1000);

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

    matrixTask.PutString("S01");
    consoleTask.setup();
    matrixTask.PutString("S02");
    wifiJoinApTask.setup();
    matrixTask.PutString("S03");
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
        $Assert(wifiJoinApTask.IsConfigured());

        static bool     firstTime = true;

        if (firstTime)
        {
            // First time we've had the wifi config completed - let wifi dependent Tasks set up
            firstTime = true;
        }

        // Give each wifi dependent Task time
    }
}
