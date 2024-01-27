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


/**
 * @brief Template class for a shared buffer with singleton behavior.
 * 
 * The SharedBuffer class provides a template implementation for a shared buffer with singleton behavior.
 * It allows multiple instances of the buffer to be created, each with a specified size (TSize).
 * The buffer is accessed through a Handle object, which provides a pointer to the buffer.
 * The buffer is protected by a semaphore lock to ensure thread-safe access.
 * 
 * @tparam TSize The size of the buffer.
 */
template <int TSize>
class SharedBuffer
{
private:
    static uint8_t _buffer[TSize]; // The shared buffer
    static SemaphoreHandle_t _lock; // Semaphore lock for thread-safe access

public:
    /**
     * @brief Handle class for accessing the shared buffer.
     * 
     * The Handle class provides a way to access the shared buffer.
     * It encapsulates the buffer pointer and provides a method to get the buffer size.
     * The Handle object automatically unlocks the buffer when it is destroyed.
     */
    class Handle
    {
    public:
        /**
         * @brief Get the pointer to the shared buffer.
         * 
         * @return uint8_t* Pointer to the shared buffer.
         */
        static inline uint8_t *GetBuffer() { return &_buffer[0]; }

        /**
         * @brief Get the size of the shared buffer.
         * 
         * @return constexpr int The size of the shared buffer.
         */
        constexpr int GetSize() { return TSize; }

        Handle();
        ~Handle();
    };

    /**
     * @brief Constructor for the SharedBuffer class.
     */
    SharedBuffer();

    /**
     * @brief Destructor for the SharedBuffer class.
     */
    ~SharedBuffer();

    /**
     * @brief Get a handle to the shared buffer.
     * 
     * @return Handle A handle to the shared buffer.
     */
    static Handle GetHandle();

    /**
     * @brief Get the size of the shared buffer.
     * 
     * @return constexpr int The size of the shared buffer.
     */
    static constexpr int GetSize() { return TSize; }
};

/**
 * @brief Definition of the shared buffer.
 * 
 * The shared buffer is defined as a static array of size TSize.
 * Each instance of the SharedBuffer class shares the same buffer.
 *
 * The buffer is initialized to zero.
 * 
 * @tparam TSize The size of the buffer.
 */
template <int TSize>
uint8_t SharedBuffer<TSize>::_buffer[TSize] = {0};

/**
 * @brief Definition of the semaphore lock.
 * 
 * The semaphore lock is defined as a static member of the SharedBuffer class.
 * It is used to protect the shared buffer from concurrent access.
 * 
 * @tparam TSize The size of the buffer.
 */
template <int TSize>
SemaphoreHandle_t SharedBuffer<TSize>::_lock;

/**
 * @brief Constructor for the SharedBuffer class.
 * 
 * The constructor initializes the semaphore lock if it has not been initialized before.
 * It uses a static volatile flag to ensure that the initialization is performed only once.
 * The constructor also takes the semaphore lock to ensure exclusive access to the shared buffer.
 * 
 * @tparam TSize The size of the buffer.
 */
template <int TSize>
SharedBuffer<TSize>::SharedBuffer()
{
}

/**
 * @brief Destructor for the SharedBuffer class.
 * 
 * The destructor releases the semaphore lock to allow other threads to access the shared buffer.
 * 
 * @tparam TSize The size of the buffer.
 */
template <int TSize>
SharedBuffer<TSize>::~SharedBuffer()
{
}

/**
 * @brief Get a handle to the shared buffer.
 * 
 * This static method returns a handle to the shared buffer.
 * 
 * @tparam TSize The size of the buffer.
 * @return Handle A handle to the shared buffer.
 */
template <int TSize>
typename SharedBuffer<TSize>::Handle SharedBuffer<TSize>::GetHandle()
{
    return SharedBuffer<TSize>::Handle();
}

/**
 * @brief Constructor for the Handle class.
 * 
 * The constructor initializes the semaphore lock if it has not been initialized before.
 * It uses a static volatile flag to ensure that the initialization is performed only once.
 * The constructor also takes the semaphore lock to ensure exclusive access to the shared buffer.
 * 
 * @tparam TSize The size of the buffer.
 */
template <int TSize>
SharedBuffer<TSize>::Handle::Handle()
{
    static volatile bool initialized = false;

    if (!initialized)
    {
        CriticalSection cs;
        if (!initialized)
        {
            $Assert(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED);
            SharedBuffer<TSize>::_lock = xSemaphoreCreateBinary();
            xSemaphoreGive(SharedBuffer<TSize>::_lock); // give the semaphore so it is available first time
            initialized = true;
        }
    }

    $Assert(SharedBuffer<TSize>::_lock != NULL);
    $Assert(xSemaphoreTake(SharedBuffer<TSize>::_lock, portMAX_DELAY) == pdTRUE);
}

/**
 * @brief Destructor for the Handle class.
 * 
 * The destructor releases the semaphore lock to allow other threads to access the shared buffer.
 * 
 * @tparam TSize The size of the buffer.
 */
template <int TSize>
SharedBuffer<TSize>::Handle::~Handle()
{
    $Assert(SharedBuffer<TSize>::_lock != NULL);
    $Assert(xSemaphoreGive(SharedBuffer<TSize>::_lock) == pdTRUE);
}


//* Common support functions

//* Thread-safe shared buffers
extern SharedBuffer<256> sharedPrintfBuffer;

extern int printf(Stream& ToStream, const char* Format, ...);

// Helper for 64-bit formatted *printf output
#define $PRIX64 "08X%08X"
#define To$PRIX64(v) ((uint32_t)(v >> 32)),((uint32_t)v)


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

