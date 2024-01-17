// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include "SpaHeaterCntl.h"

ConsoleTask     consoleTask(Serial);
LedMatrixTask   matrixTask(Serial, 50);
WiFiJoinApTask  wifiJoinApTask(Serial, "SpaHeaterAP", "123456789");
Logger          logger(Serial);
FlashStore<BootRecord, PS_BootRecordBase> bootRecord;

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
class NetworkTask : public ArduinoTask
{
public:
    NetworkTask() {}
    ~NetworkTask() { $FailFast(); }

    void Begin(const char* SSID, const char* NetPassword);

    virtual void setup();
    virtual void loop();

private:
    static String& AppendHEX(uint8_t Byte, String& AddTo)
    {
        static char hexDigit[] = "0123456789ABCDEF";

        AddTo += hexDigit[((Byte & 0xF0)>> 4)];
        AddTo += hexDigit[(Byte & 0x0F)];

        return AddTo;
    }
    static String MacToString(uint8_t* Mac)
    {
        String result = "";
        for (int i = 5; i >= 0; i--)
        {
            AppendHEX(Mac[i], result);
            if (i > 0)
            {
                result += ":";
            }
        }
        
        return result;
    }

private:
    const char*     _ssid;
    const char*     _networkPassword;
};

void NetworkTask::Begin(const char* SSID, const char* NetPassword)
{
    _ssid = SSID;
    _networkPassword = NetPassword;
    logger.Printf(Logger::RecType::Info, "NetworkTask Started for '%s'", &_ssid[0]);
}

void NetworkTask::setup()
{
}

void NetworkTask::loop()
{
    enum class State
    {
        StartWiFiBegin,
        DelayAfterWiFiBegin,
        Connected,
    };

    static bool     firstTime = true;
    static State    state;
    static Timer    delayTimer;
    static int      status;

    if (firstTime)
    {
        firstTime = false;
        status = WL_IDLE_STATUS;
        state = State::StartWiFiBegin;
    }

    switch (state)
    {
        case State::StartWiFiBegin:
        {
            if (status != WL_CONNECTED)
            {
                logger.Printf(Logger::RecType::Info, "NetworkTask: Attempting to connect to WPA SSID: '%s'", _ssid);
                status = WiFi.begin(_ssid, _networkPassword);

                // wait 10 secs for connection
                delayTimer.SetAlarm(10000);
                state = State::DelayAfterWiFiBegin;
                return;
            }

            uint8_t     mac[6];
            String      ipAddr = WiFi.localIP().toString();

            WiFi.macAddress(&mac[0]);
            String      ourMAC = MacToString(mac);

            WiFi.BSSID(&mac[0]);
            String      apBSSID = MacToString(mac);

            logger.Printf(Logger::RecType::Info, "NetworkTask: Connected to SSID: '%s' @ %s (MAC: %s); AP BSSID: %s", 
                _ssid, 
                ipAddr.c_str(),
                ourMAC.c_str(),
                apBSSID.c_str());

            state = State::Connected;
        }
        break;

        case State::DelayAfterWiFiBegin:
        {
            if (delayTimer.IsAlarmed())
            {
                state = State::StartWiFiBegin;
            }
        }
        break;

        case State::Connected:
        {
            status = WiFi.status();

            if (status != WL_CONNECTED)
            {
                logger.Printf(Logger::RecType::Info, "NetworkTask: Disconnected");
                state = State::StartWiFiBegin;
                return;
            }
        }
        break;

        default:
            $FailFast();
    }
}


NetworkTask     network;



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
        }

        // Give each wifi dependent Task time
        network.loop();
    }
}
