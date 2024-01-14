// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// ConsoleTask implementation

#include "SpaHeaterCntl.h"

// WIFI Network enumeration

void printEncryptionType(int thisType) 
{
    // read the encryption type and print out the name:
    switch (thisType) 
    {
        case ENC_TYPE_WEP:
            Serial.print("WEP");
            break;
        case ENC_TYPE_WPA:
            Serial.print("WPA");
            break;
        case ENC_TYPE_WPA2:
            Serial.print("WPA2");
            break;
        case ENC_TYPE_WPA3:
            Serial.print("WPA3");
            break;  
        case ENC_TYPE_NONE:
            Serial.print("None");
            break;
        case ENC_TYPE_AUTO:
            Serial.print("Auto");
            break;
        case ENC_TYPE_UNKNOWN:
        default:
          Serial.print("Unknown");
          break;
    }
}

void print2Digits(byte thisByte) 
{
    if (thisByte < 0xF) 
    {
        Serial.print("0");
    }
    Serial.print(thisByte, HEX);
}

void printMacAddress(byte mac[]) 
{
    for (int i = 5; i >= 0; i--) 
    {
        if (mac[i] < 16) 
        {
            Serial.print("0");
        }
            Serial.print(mac[i], HEX);
        if (i > 0) 
        {
            Serial.print(":");
        }
    }
    Serial.println();
}

void listNetworks() 
{
    // scan for nearby networks:
    Serial.println("** Scan Networks **");
    int numSsid = WiFi.scanNetworks();
    if (numSsid == -1)
    {
        Serial.println("Couldn't get a WiFi connection");
        $FailFast();
    }

    // print the list of networks seen:
    Serial.print("number of available networks: ");
    Serial.println(numSsid);

    // print the network number and name for each network found:
    for (int thisNet = 0; thisNet < numSsid; thisNet++) 
    {
        Serial.print(thisNet + 1);
        Serial.print(") ");
        Serial.print("Signal: ");
        Serial.print(WiFi.RSSI(thisNet));
        Serial.print(" dBm");
        Serial.print("\tChannel: ");
        Serial.print(WiFi.channel(thisNet));

        byte bssid[6];
        Serial.print("\t\tBSSID: ");
        printMacAddress(WiFi.BSSID(thisNet, bssid));
        Serial.print("\tEncryption: ");
        printEncryptionType(WiFi.encryptionType(thisNet));
        Serial.print("\t\tSSID: ");
        Serial.println(WiFi.SSID(thisNet));
        Serial.flush();
    }
    Serial.println();
}

// Command line processors
CmdLine::Status ClearEEPROMProcessor(Stream& CmdStream, int Argc, char const** Args, void* Context)
{
    CmdStream.println("Starting EEPROM Erase...");
    wifiJoinApTask.EraseConfig();
    CmdStream.println("EEPROM Erase has completed");
    return CmdLine::Status::Ok;
}

CmdLine::Status ListNetsProcessor(Stream& CmdStream, int Argc, char const** Args, void* Context)
{
    listNetworks();
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

CmdLine::ProcessorDesc  consoleTaskCmdProcessors[] =
{
    {ClearEEPROMProcessor, "clearEPROM", "Clear all of the EEPROM"},
    {ListNetsProcessor, "listNets", "Discover and list all wifi networks"},
    {SetLedDisplayProcessor, "ledDisplay", "Put tring to Led Matrix"},
    {DumpProcessor, "dump", "Dump internal state"},
};

ConsoleTask::ConsoleTask(Stream& Output)
    :   _output(Output)
{  
}

ConsoleTask::~ConsoleTask() 
{ 
    $FailFast(); 
}

void ConsoleTask::setup()
{
    _output.println("ConsoleTask is Active");
    _cmdLine.begin(_output, &consoleTaskCmdProcessors[0], sizeof(consoleTaskCmdProcessors) / sizeof(CmdLine::ProcessorDesc));
}

void ConsoleTask::loop() 
{
    _cmdLine.IsReady();
}

