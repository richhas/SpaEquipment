// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// System Logger implementation

#include "SpaHeaterCntl.hpp"


Logger          logger(Serial);


Logger::Logger(Stream &ToStream)
    : _out(ToStream),
      _logSeq(0),
      _instanceSeq(0xFFFFFFFF),
      _highFilterType(Logger::RecType::Info)
{
}

Logger::~Logger()
{
    $FailFast();
}

void Logger::SetFilter(Logger::RecType HighFilterType)
{
    _highFilterType = HighFilterType;
}   

const char* Logger::ToString(Logger::RecType From)
{
    switch (From)
    {
        case RecType::Start:
            return "SLOG";
            break;

        case RecType::Info:
            return "INFO";
            break;
        
        case RecType::Progress:
            return "PROG";
            break;

        case RecType::Warning:
            return "WARN";
            break;

        case RecType::Critical:
            return "CRIT";
            break;

        default:
            $FailFast();
    }
}

void Logger::Begin(uint32_t InstanceSeq)
{
    _instanceSeq = InstanceSeq;
    RTCTime currentTime;
    $Assert(RTC.getTime(currentTime));

    Printf(RecType::Start, "%s", currentTime.toString().c_str());
}

int Logger::Printf(Logger::RecType Type, const char *Format, ...)
{
    if (Type < _highFilterType)
    {
        return 0;
    }

    int size;
    va_list args;

    va_start(args, Format);

    {
        auto handle = sharedPrintfBuffer.GetHandle();
        char* buffer = (char *)handle.GetBuffer();

        size = vsnprintf(buffer, handle.GetSize(), Format, args);
        _out.print(ToString(Type));
        _out.print(":");
        _out.print(_instanceSeq);
        _out.print(":");
        _out.print(_logSeq);
        _out.print(":");
        _out.print(millis());
        _out.print(":");
        _out.println(buffer);
        _logSeq++;
    }

    va_end(args);

    return size;
}
