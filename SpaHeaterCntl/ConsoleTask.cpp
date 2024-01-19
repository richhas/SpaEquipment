// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// ConsoleTask implementation

#include "SpaHeaterCntl.h"

// WIFI Network enumeration

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
};

ConsoleTask::ConsoleTask(Stream& Output)
    :   _output(Output)
{  
}

ConsoleTask::~ConsoleTask() 
{ 
    // $FailFast(); 
}

void ConsoleTask::setup()
{
    logger.Printf(Logger::RecType::Info, "ConsoleTask is Active");
    _cmdLine.begin(_output, &consoleTaskCmdProcessors[0], sizeof(consoleTaskCmdProcessors) / sizeof(CmdLine::ProcessorDesc));
}

void ConsoleTask::loop() 
{
    _cmdLine.IsReady();
}

