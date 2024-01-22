// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// BoilerControllerTask implementation

#include "SpaHeaterCntl.h"



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
    : _ds(_oneWireBusPin)
{
}

BoilerControllerTask::~BoilerControllerTask()
{
    $FailFast();
}

void BoilerControllerTask::setup()
{
    logger.Printf(Logger::RecType::Info, "*** BoilerControllerTask Thread Active ***\n");

    pinMode(_heaterControlPin, OUTPUT); // Make sure the heater is turned off to start with
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
        printf(Serial, " -- (%" $PRIX64 ")", To$PRIX64(*((uint64_t *)(&addr[0]))));
        Serial.println();

        _sensors.push_back(*((uint64_t *)(&addr[0])));
    }
    logger.Printf(Logger::RecType::Info, "BoilerControllerTask: Start bus enumeration - COMPLETE\n");
}

CmdLine::Status ShowTempSensorsProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    auto const sensors = boilerControllerTask.GetTempSensors();

    for (auto const &sensor : sensors)
    {
        for (uint8_t *byte = ((uint8_t *)(&sensor)); byte < ((uint8_t *)(&sensor)) + sizeof(sensor); ++byte)
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
    // logger.Printf(Logger::RecType::Info, "BoilerControllerTask: loop\n");

    switch (_state)
    {
    case OnHold:
    {
        // TODO: Add code for OnHold state
    }
    break;

    case WaitForValidConfig:
    {
        // TODO: Add code for OnHold state
    }
    break;

    case Active:
    {
        // TODO: Add code for OnHold state
    }
    break;

    default:
    {
        // TODO: Add code for OnHold state
    }
    break;
    }
}
