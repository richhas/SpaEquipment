// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// HA_MQTT class definitions

#pragma once
#include "SpaHeaterCntl.h"
#include <ArduinoMqttClient.h>
#include <functional>
#include "MQTT_HA.hpp"

//* MQTT Client Task
class HA_MqttClient final : public ArduinoTask
{
public:
    virtual void setup() override final;
    virtual void loop() override final;
};

//** Cross module references
extern class HA_MqttClient haMqttClient;
extern CmdLine::ProcessorDesc haMqttCmdProcessors[];
extern int const LengthOfHaMqttCmdProcessors;