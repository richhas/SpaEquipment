// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#pragma once

#include <Arduino.h>
#include <RTC.h>
#include <ArduinoGraphics.h>
#include <Arduino_LED_Matrix.h>
#include <time.h>
#include <vector>
#include <stack>
#include <Arduino_FreeRTOS.h>

using namespace std;

#include "common.hpp"
#include "clilib.hpp"
#include "FlashStore.hpp"
#include "BoilerControllerTask.hpp"
// #include "WiFiJoinApTask.hpp"
#include "ConsoleTask.hpp"
#include "Logger.hpp"
#include "Network.hpp"
#include "LedMatrixTask.hpp"
#include "MQTT_HA.hpp"




//** Common helpers
/**
 * Converts temperature from Fahrenheit to Celsius.
 *
 * @param F The temperature in Fahrenheit.
 * @return The temperature in Celsius.
 */
constexpr float $FtoC(float F) 
{
    return (F - 32.0) * 5.0 / 9.0;
}

/**
 * Converts temperature from Celsius to Fahrenheit.
 *
 * @param C The temperature in Celsius.
 * @return The temperature in Fahrenheit.
 */
constexpr float $CtoF(float C) 
{
    return (C * 9.0 / 5.0) + 32.0;
}

/**
 * Converts a temperature Celsius difference to a Fahrenheit difference.
 */
constexpr float $CDiffToF(float C)
{
    return C * 9.0 / 5.0;
}

/**
 * Converts a temperature Fahrenheit difference to a Celsius difference.
 */
constexpr float $FDiffToC(float F)
{
    return F * 5.0 / 9.0;
}



//** Cross module references
extern void SetAllBoilerParametersFromConfig();
extern CmdLine::ProcessorDesc consoleTaskCmdProcessors[];
extern int const LengthOfConsoleTaskCmdProcessors;