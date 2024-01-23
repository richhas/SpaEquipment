/*
    Common Definitions 

    Copyright TinyBus 2020
*/
#pragma once

#include <Arduino.h>

//** Time/Timer support
class Timer
{
public:
    static const uint32_t FOREVER = UINT32_MAX;

    __inline Timer() : _alarmTime(0) {}
    __inline Timer(uint32_t AlarmInMs) { _alarmTime = millis() + AlarmInMs; }
    __inline void SetAlarm(uint32_t AlarmInMs) { (_alarmTime = (AlarmInMs == FOREVER) ? FOREVER : millis() + AlarmInMs); }
    __inline bool IsAlarmed() { return ((_alarmTime == FOREVER) ? false : (millis() >= _alarmTime)); }
    __inline uint32_t Remaining()
    {
        if (_alarmTime == FOREVER)
        {
            return FOREVER;
        }
        uint32_t now = millis();
        return ((millis() >= _alarmTime) ? 0 : (_alarmTime - now));
    }
private:
    uint32_t    _alarmTime;         // msecs
};

