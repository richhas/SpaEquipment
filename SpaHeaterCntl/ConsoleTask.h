// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// ConsoleTask definitions

#pragma once

#include "SpaHeaterCntl.h"

class ConsoleTask : public ArduinoTask
{
private:
    Stream&       _output;
    CmdLine       _cmdLine;

public:
    ConsoleTask() = delete;
    ConsoleTask(Stream& Output);
    ~ConsoleTask();

    virtual void setup();
    virtual void loop();
};
