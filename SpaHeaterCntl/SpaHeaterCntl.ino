// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include <Arduino_FreeRTOS.h>
#include <ArduinoMqttClient.h>
#include "SpaHeaterCntl.h"




ConsoleTask     consoleTask(Serial);
LedMatrixTask   matrixTask(Serial, 50);
WiFiJoinApTask  wifiJoinApTask(Serial, "SpaHeaterAP", "123456789");
Logger          logger(Serial);
FlashStore<BootRecord, PS_BootRecordBase> bootRecord;
    static_assert(PS_BootRecordBlkSize >= sizeof(FlashStore<BootRecord, PS_BootRecordBase>));
FlashStore<TempSensosConfig, PS_TempSensorsConfigBase> tempSensorsConfig;
    static_assert(PS_TempSensorsConfigBlkSize >= sizeof(FlashStore<TempSensosConfig, PS_TempSensorsConfigBase>));
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


//*******************************BoilerControllerTask Workbench******************************************************
#include <DS18B20.h>

//*** Boiler Controller
class BoilerControllerTask final : public ArduinoTask
{
public:
    BoilerControllerTask();
    ~BoilerControllerTask();

    virtual void setup() override final;
    virtual void loop() override final;
    inline const vector<uint64_t>& GetTempSensors() const
    {
        return _sensors;
    }
    static void BoilerControllerThreadEntry(void *pvParameters);

private:
    static const uint8_t    _heaterControlPin = 13;
    static const uint8_t    _oneWireBusPin = 2;
    DS18B20                 _ds;
    vector<uint64_t>        _sensors;

    enum State
    {
        OnHold,
        WaitForValidConfig,
        Active
    };

    State   _state;
};

void BoilerControllerTask::BoilerControllerThreadEntry(void *pvParameters)
{
    boilerControllerTask.setup();
    while (true)
    {
        boilerControllerTask.loop();
        vTaskDelay(pdMS_TO_TICKS(1000));
        digitalWrite(_heaterControlPin, !digitalRead(_heaterControlPin));
    }
}

BoilerControllerTask::BoilerControllerTask()
    :   _ds(_oneWireBusPin)
{
}

BoilerControllerTask::~BoilerControllerTask()
{
    $FailFast();
}

void BoilerControllerTask::setup()
{
    logger.Printf(Logger::RecType::Info, "*** BoilerControllerTask Thread Active ***\n");

    pinMode(_heaterControlPin, OUTPUT);  // Make sure the heater is turned off to start with
    digitalWrite(_heaterControlPin, false);

    _state = OnHold;

    logger.Printf(Logger::RecType::Info, "BoilerControllerTask: Start bus enumeration\n");
    while (_ds.selectNext())
    {
        uint8_t addr[8];

        _ds.getAddress(addr);
        Serial.print("Address:");
        for (uint8_t i = 0; i < 8; i++) 
        {
            Serial.print(" ");
            Serial.print(addr[i]);
        }
        Serial.println();

        _sensors.push_back(*((uint64_t*)(&addr[0])));
    }
    logger.Printf(Logger::RecType::Info, "BoilerControllerTask: Start bus enumeration - COMPLETE\n");
}

CmdLine::Status ShowTempSensorsProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    auto const sensors = boilerControllerTask.GetTempSensors();

    for (auto const& sensor : sensors)
    {
        for (uint8_t* byte = ((uint8_t*)(&sensor)); byte < ((uint8_t*)(&sensor)) + sizeof(sensor); ++byte)
        {
            CmdStream.print(" ");
            CmdStream.print(*byte);
        }
        CmdStream.println();
    }

    return CmdLine::Status::Ok;
}

void BoilerControllerTask::loop()
{
    switch (_state)
    {
        case OnHold:
            // TODO: Add code for OnHold state
            break;

        case WaitForValidConfig:
            // TODO: Add code for WaitForValidConfig state
            break;

        case Active:
            // TODO: Add code for Active state
            break;

        default:
            // TODO: Add code for default state
            break;
    }
}

//******************************************************************************************
BoilerControllerTask boilerControllerTask;
TaskHandle_t mainThread;
TaskHandle_t backgroundThread;

void setup()
{
    //* First thing - re-enable the RTC VBATT power switch so not to reset it if a Reset occurs to the UNO
    //  past this point
/*    
    R_SYSTEM->PRCR = 0xA502 | (R_SYSTEM->PRCR & ~0x02);         // Must write enable VBTCR1
    R_SYSTEM->VBTCR1 = R_SYSTEM->VBTCR1 & ~0x01;                // Turn off BPWSWSTP
    R_SYSTEM->PRCR = 0xA500 | (R_SYSTEM->PRCR & ~0x02);         // Write protect VBTCR1
*/
    Serial.begin(9600);
    delay(1000);

    matrixTask.setup();
    matrixTask.PutString("S00");

    auto const status = xTaskCreate
    (
        MainThreadEntry,
        static_cast<const char*>("Loop Thread"),
        (1024 + 256) / 4,   /* usStackDepth in words */      // allow for a sprintf stack buffer
        nullptr,   /* pvParameters */
        1,         /* uxPriority */
        &mainThread /* pxCreatedTask */
    );

    if (status != pdPASS) 
    {
        Serial.println("Failed to create 'main' thread");
        $FailFast();
    }

    matrixTask.PutString("S01");
    vTaskStartScheduler();
    $FailFast();
    while (true) {}
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


    $Assert(RTC.begin());
    logger.Begin(bootRecord.GetRecord().BootCount);

    matrixTask.PutString("S03");
    auto const status = xTaskCreate(
        BoilerControllerTask::BoilerControllerThreadEntry,
        static_cast<const char *>("Loop Thread"),
        (1024 + 256) / 4, /* usStackDepth in words */ // allow for a sprintf stack buffer
        nullptr,                                      /* pvParameters */
        2,                                            /* uxPriority */
        &backgroundThread                             /* pxCreatedTask */
    );

    if (status != pdPASS)
    {
        Serial.println("Failed to create 'background' thread");
        $FailFast();
    }

    matrixTask.PutString("S04");
    consoleTask.setup();
    matrixTask.PutString("S05");

    matrixTask.PutString("S06");
    wifiJoinApTask.setup();
    matrixTask.PutString("S07");
    network.setup();
    matrixTask.PutString("S08");
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

bool doMQTT = false;

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

/*
        if ((doMQTT == false) && (WiFi.status() == WL_CONNECTED))
        {
            tryMQTT();
            doMQTT = true;
        }
*/

        // Give each wifi dependent Task time
        network.loop();
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
}
