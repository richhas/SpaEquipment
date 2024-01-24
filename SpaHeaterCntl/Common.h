/*
    Common Definitions 

    Copyright TinyBus 2020
*/
#pragma once

#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <memory.h>
#include <EEPROM.h>

//** Hard Fault primitives 
extern void FailFast(char* FileName, int LineNumber);
#define $Assert(c) if (!(c)) FailFast(__FILE__, __LINE__);
#define $FailFast() FailFast(__FILE__, __LINE__);

//* Common status codes
enum class Status : int16_t
{
    //* Non-error status' >= 0
    Success = 0,
    Busy,
    Cancelled,

    //* Error status' < 0
    Timeout = -1,
    MessageHeaderCRCFailure = -2,
    MessageTooLarge = -3,
    MessageBodyCRCFailure = -4,
};

//* Common support functions
extern int printf(Stream& ToStream, const char* Format, ...);

// Helper for 64-bit formatted *printf output
#define $PRIX64 "08X%08X"
constexpr uint32_t To$PRIX64(uint64_t v)
{
    return ((uint32_t)(v >> 32)) << 16 | ((uint32_t)v);
}


/**
 * @brief A class that represents a critical section.
 * 
 * This class provides a convenient way to enter and exit a critical section
 * in multi-threaded environments. The constructor enters the critical section,
 * and the destructor exits the critical section automatically when the object
 * goes out of scope.
 */
class CriticalSection
{
public:
    CriticalSection() { taskENTER_CRITICAL(); }
    ~CriticalSection() { taskEXIT_CRITICAL(); }
};

//** CRC Support
/*
class CRC
{
public:
    // static uint32_t CRC32(uint32_t* Buffer, uint16_t Words, uint32_t StartingCRC = DMAC_CRCDATAIN_RESETVALUE);
    static uint32_t CRC32(byte* Buffer, uint16_t Size, uint32_t StartingCRC = DMAC_CRCDATAIN_RESETVALUE);
    static uint16_t CRC16(byte* Buffer, uint16_t Size, uint16_t StartingCRC = DMAC_CRCDATAIN_RESETVALUE);

private:
    static void DoCRC(uint32_t AlgType, byte* Buffer, uint16_t Size, uint32_t StartingCRC);
};
*/

//** Time/Timer support
class Timer
{
public:
    static const uint32_t FOREVER = UINT32_MAX;

    __inline Timer() : _alarmTime(0) {}
    __inline Timer(uint32_t AlarmInMs) { _alarmTime = millis() + AlarmInMs; }
    __inline void SetAlarm(uint32_t AlarmInMs) { (_alarmTime = (AlarmInMs == FOREVER) ? FOREVER : millis() + AlarmInMs); }
    __inline bool IsAlarmed() { return ((_alarmTime == FOREVER) ? false : (millis() >= _alarmTime)); }
private:
    uint32_t    _alarmTime;         // msecs
};


//** Stream serializible interface
class ISerializable
{
public:
    virtual void ToStream(Stream&) = 0;
};

//** UUID/GUID struct
struct UUID
{
    union 
    {
        struct
        {
            uint64_t    _lowDWord;
            uint64_t    _highDWord;
        };
        uint32_t        _words[4];
    };
};

//** Generalized Arduino processing task class
class ArduinoTask
{
public:
    virtual void setup() = 0;
    virtual void loop() = 0;

protected:
#if SAMD51_SERIES
    UUID GetSystemID()
    {
        UUID result;

        result._words[0] = *((uint32_t*)0x008061FC);
        result._words[1] = *((uint32_t*)0x00806010);
        result._words[2] = *((uint32_t*)0x00806014);
        result._words[3] = *((uint32_t*)0x00806018);

        return result;
    }
#endif
};

/* EEPROM config support */
// The first bytes of the EEPROM are used to store the configuration for this device
//
#pragma pack(push, 1)
template <typename TBlk, uint16_t TBaseOfRecord> 
class FlashStore
{
private:
    union
    {
        struct
        {
            TBlk        _record;
            uint8_t     _onesComp[sizeof(TBlk)];           // 1's comp of the the above - correctness checks
        };

        uint8_t         _bytes[2 * sizeof(TBlk)];    
    };

private:
    void Fill()
    {
        uint8_t*    next = &_bytes[0];
        int         toDo = sizeof(*this);
        int         index = TBaseOfRecord;

        while (toDo > 0)
        {
            *next = EEPROM.read(index);
            next++;
            index++;
            toDo--;
        }
    }

    void Flush()
    {
        uint8_t*    next = &_bytes[0];
        int         toDo = sizeof(*this);
        int         index = TBaseOfRecord;

        while (toDo > 0)
        {
            EEPROM.write(index, *next);
            next++;
            index++;
            toDo--;
        }
    }

public:
    FlashStore()
    {
        memset(&_bytes[0], 0, sizeof(FlashStore::_bytes));
    }

    void Begin()
    {
        Fill();
    }

    TBlk& GetRecord() { return _record; }

    bool IsValid()
    {
        for (byte ix = 0; ix < sizeof(FlashStore::_onesComp); ix++)
        {
            uint8_t t = ~(_bytes[ix]);
            if (t != _onesComp[ix])
            {
                return false;
            }
        }

        return true;
    }

    void Write()
    {
        for (byte ix = 0; ix < sizeof(FlashStore::_onesComp); ix++)
        {
            _onesComp[ix] = ~(_bytes[ix]);
        }

        Flush();
    }

    void Erase()
    {
        memset(&_bytes[0], 0, sizeof(FlashStore::_bytes));
        Flush();
    }
};
#pragma pack(pop) 

