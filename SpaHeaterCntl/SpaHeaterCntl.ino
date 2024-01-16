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


class Logger
{
    public:
        Logger() = delete;
        Logger(Stream& ToStream);
        ~Logger();

        void Begin(uint32_t InstanceSeq);
        int Printf(const char* Format, ...);

    private:
        Stream&     _out;
        uint32_t    _logSeq;
        uint32_t    _instanceSeq;
};

Logger::Logger(Stream &ToStream)
    : _out(ToStream),
      _logSeq(0),
      _instanceSeq(0xFFFFFFFF)
{
}

Logger::~Logger()
{
    $FailFast();
}

void Logger::Begin(uint32_t InstanceSeq)
{
    _instanceSeq = InstanceSeq;
    RTCTime currentTime;
    $Assert(RTC.getTime(currentTime));

    Printf("%s: STARTLOG", currentTime.toString().c_str());
}

int Logger::Printf(const char *Format, ...)
{
    char buffer[256];
    va_list args;

    va_start(args, Format);
    int size = vsnprintf(&buffer[0], sizeof(buffer), Format, args);
    _out.print(_instanceSeq);
    _out.print(":");
    _out.print(_logSeq);
    _out.print(":");
    _out.print(millis());
    _out.print(":");
    _out.print(&buffer[0]);
    _logSeq++;
    va_end(args);

    return size;
}

#pragma pack(push, 1)
struct BootRecord
{
    uint32_t    BootCount;
};
#pragma pack(pop)

FlashStore<BootRecord, PS_BootRecordBase> bootRecord;
Logger      logger(Serial);


void setup()
{
    matrixTask.setup();
    matrixTask.PutString("S00");

    Serial.begin(9600);
    delay(1000);

    bootRecord.PrintDebug();

    Serial.println("Before: bootRecord.Begin()");
    bootRecord.Begin();
    Serial.println("After: bootRecord.Begin()");
    if (!bootRecord.IsValid())
    {
        Serial.println("bootRecord NOT valid");
        // Do auto setup and write
        bootRecord.GetRecord().BootCount = 0;
        bootRecord.Write();
        bootRecord.Begin();
        $Assert(bootRecord.IsValid());
    }
    else
    {
        Serial.println("bootRecord IS valid");
/*        
        bootRecord.GetRecord().BootCount++;
        bootRecord.Write();
        bootRecord.Begin();
        $Assert(bootRecord.IsValid());
*/        
    }

/*
    RTC.begin();
    logger.Begin(bootRecord.GetRecord().BootCount);
*/    

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
