// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include "SpaHeaterCntl.h"


#pragma pack(push, 1) 
struct WiFiClientConfig
{
  uint8_t       _version;
  const int         CurrentVersion = 1;

  // Version 1 and > fields
  char          _ssid[32];
  char          _networkPassword[32];
};
#pragma pack(pop) 


ConsoleTask consoleTask(Serial);


void setup() 
{
    Serial.begin(9600);
    delay(1000);

    consoleTask.setup();

    // check for the WiFi module:
    if (WiFi.status() == WL_NO_MODULE) 
    {
      Serial.println("Communication with WiFi module failed!");
      $FailFast();
    }

    String fv = WiFi.firmwareVersion();
    if (fv < WIFI_FIRMWARE_LATEST_VERSION) 
    {
        Serial.println("Please upgrade the firmware");
    }

    /*
    Config<WiFiClientConfig> t1;
    if (t1.IsValid())
    {
      Serial.print("Config is valid: ");
      Serial.print(t1._configRec._version);
      Serial.println(" -- increasing version.");
      t1._configRec._version++;
    }
    else
    {
      Serial.println("Config is not valid - initializing...");
      t1._configRec._version = 1;
    }

    t1.Write();
    Serial.println("Done");
    */
}

void loop() 
{
    consoleTask.loop();
}
