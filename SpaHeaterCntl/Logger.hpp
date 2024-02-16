// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// System Logger definitions

#pragma once
#include "SpaHeaterCntl.hpp"
#include "Logger.hpp"

//* System Logger
class Logger
{
public:
    enum class RecType : uint8_t
    {
        Start = 0xFF, // Reserved: start of log marker record
        NtpRef = 0xFE, // Reserved: NTP reference time marker record
        Info = 1,
        Progress = 2,
        Warning = 3,
        Critical = 4,
    };

    Logger() = delete;
    Logger(Stream &ToStream);
    ~Logger();

    void Begin(uint32_t InstanceSeq);
    int Printf(Logger::RecType Type, const char *Format, ...);
    void SetFilter(Logger::RecType HighFilterType);

    static const char *ToString(Logger::RecType From);

private:
    Stream &_out;
    uint32_t _logSeq;
    uint32_t _instanceSeq;
    RecType _highFilterType; // Only log records with a type >= this
};

//** Cross module references
extern class Logger logger;
