// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include "SpaHeaterCntl.h"

ConsoleTask     consoleTask(Serial);
LedMatrixTask   matrixTask(Serial, 50);
WiFiJoinApTask  wifiJoinApTask(Serial);


void setup() 
{
    matrixTask.setup();
    matrixTask.PutString("S00");

    Serial.begin(9600);
    delay(1000);

    matrixTask.PutString("S01");
    consoleTask.setup();
    matrixTask.PutString("S02");
    wifiJoinApTask.setup();
    matrixTask.PutString("S03");

    /*
    FlashStore<WiFiClientConfig> t1;
    if (t1.IsValid())
    {
      Serial.print("Config is valid: ");
      Serial.print(t1.GetRecord()._version);
      Serial.println(" -- increasing version.");
      t1.GetRecord()._version++;
    }
    else
    {
      Serial.println("Config is not valid - initializing...");
      t1.GetRecord()._version = 1;
    }

    t1.Write();
    Serial.println("Done");
    */
}

void loop() 
{
    consoleTask.loop();
    matrixTask.loop();
    wifiJoinApTask.loop();
}
