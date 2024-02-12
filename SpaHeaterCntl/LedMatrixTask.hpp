// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// LedMatrixTask definitions

#pragma once
#include "SpaHeaterCntl.h"
#include "LedMatrixTask.hpp"



//* Task to maintain the UNO WiFi LED display; presenting a text string
class LedMatrixTask : public ArduinoTask
{
private:
    Stream &_output;
    uint16_t _scrollTimeInMS;
    ArduinoLEDMatrix _matrix;
    String _text;
    bool _doScrollDisplay;

public:
    LedMatrixTask() = delete;
    LedMatrixTask(Stream &Output, uint8_t ScrollTimeInMS);
    ~LedMatrixTask();

    virtual void setup() override;
    virtual void loop() override;

    void PutString(char *Text);
};

//** Cross module references
extern class LedMatrixTask matrixTask;
