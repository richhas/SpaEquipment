// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// System Logger implementation

#include "SpaHeaterCntl.h"

Logger::Logger(Stream &ToStream)
    : _out(ToStream),
      _logSeq(0),
      _instanceSeq(0xFFFFFFFF)
{
    _lock = xSemaphoreCreateBinary();
    $Assert(_lock != nullptr);
    xSemaphoreGive(_lock);
}

Logger::~Logger()
{
    $FailFast();
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
    char buffer[256];
    va_list args;

    va_start(args, Format);

    while (xSemaphoreTake(_lock, portMAX_DELAY) != pdTRUE)
    {
        vTaskDelay(1);
    }

    int size = vsnprintf(&buffer[0], sizeof(buffer), Format, args);
    _out.print(ToString(Type));
    _out.print(":");
    _out.print(_instanceSeq);
    _out.print(":");
    _out.print(_logSeq);
    _out.print(":");
    _out.print(millis());
    _out.print(":");
    _out.println(&buffer[0]);
    _logSeq++;

    xSemaphoreGive(_lock);
    va_end(args);

    return size;
}
