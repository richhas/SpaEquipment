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
extern void FailFast(const char* FileName, int LineNumber);
#define $Assert(c) if (!(c)) FailFast(__FILE__, __LINE__);
#define $FailFast() FailFast(__FILE__, __LINE__);

/**
 * @brief A class that represents a critical section.
 * 
 * This class provides a convenient way to enter and exit a critical section
 * in multi-threaded environments. The constructor enters the critical section,
 * and the destructor exits the critical section automatically when the object
 * goes out of scope.
 *
 * The CriticalSection class uses the FreeRTOS taskENTER_CRITICAL and taskEXIT_CRITICAL
 * NOTE: This class is not reentrant. There will be lockups if the same thread tries to 
 * enter the critical section twice.
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

    private:
        bool _locked;
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

template <int TSize>
uint8_t SharedBuffer<TSize>::_buffer[TSize] = {0};

template <int TSize>
SemaphoreHandle_t SharedBuffer<TSize>::_lock = NULL;

template <int TSize>
SharedBuffer<TSize>::SharedBuffer()
{
}

template <int TSize>
SharedBuffer<TSize>::~SharedBuffer()
{
}

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
 * Handle has two modes of operation: with and without locks depedning on the scheduler state.
 * If the scheduler is running, the constructor takes the semaphore lock. Otherwise, it does not and
 * the implementation is not thread-safe. This is useful for initialization code that runs before
 * the scheduler is started.
 * 
 * @tparam TSize The size of the buffer.
 */
template <int TSize>
SharedBuffer<TSize>::Handle::Handle()
{
    static volatile bool initialized = false;

    if (!initialized)
    {
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {        
            CriticalSection cs;
            if (!initialized)
            {
                SharedBuffer<TSize>::_lock = xSemaphoreCreateBinary();
                xSemaphoreGive(SharedBuffer<TSize>::_lock); // give the semaphore so it is available first time
                initialized = true;
            }
        }
        else
        {   // scheduler not running yet, just return a handle without locks
            // Use with care!
            _locked = false;
            return;
        }
    }

    $Assert(SharedBuffer<TSize>::_lock != NULL);
    $Assert(xSemaphoreTake(SharedBuffer<TSize>::_lock, portMAX_DELAY) == pdTRUE);
    _locked = true;
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
    if (_locked)
    {
        $Assert(SharedBuffer<TSize>::_lock != NULL);
        $Assert(xSemaphoreGive(SharedBuffer<TSize>::_lock) == pdTRUE);
    }
}


//* Common support functions

//* Thread-safe shared buffers
extern SharedBuffer<256> sharedPrintfBuffer;
extern int printf(Stream& ToStream, const char* Format, ...);

//* Helper for 64-bit formatted *printf output
#define $PRIX64 "08X%08X"
#define To$PRIX64(v) ((uint32_t)(v >> 32)),((uint32_t)v)

//* Variable arguments access support
//
// Usage: 
//
//      void MyFunc(void* NotVarArg, ...)
//      {
//          uint32_t*   args = VarArgsBase(NotVarArg);  // first var arg
//              -- or --
//          uint32_t (*args)[0] = VarArgsBase(&NotVarArg);
//
constexpr uint32_t (* VarArgsBase(void* BaseMinusOne)) [0]
{
    return ((uint32_t (*)[0])(((uint32_t*)BaseMinusOne) + 1));
}

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

/** EEPROM config support */
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


//** Generalized Arduino processing task class
class ArduinoTask
{
public:
    virtual void setup() = 0;
    virtual void loop() = 0;
};


//** State machine support
/**
 * @brief Represents the current state of a given state machine.
 * 
 * This class template is used to define the current state variable of a state machine. It enforces 
 * that the state type is an enum class. The class provides methods to change the state, check if it 
 * is the first time a current state has been entered, and convert the state to its underlying enum value.
 * 
 * @tparam TStatesEnum The enum type representing the states.
 */
template <typename TStatesEnum>
class StateMachineState
{
    static_assert(std::is_enum<TStatesEnum>::value, "TStatesEnum must be an enum class");

public:
    StateMachineState() = delete;
    StateMachineState(const StateMachineState&) = delete;
    StateMachineState(const StateMachineState&&) = delete;
    StateMachineState &operator=(const StateMachineState &) = delete;
    StateMachineState &operator=(const StateMachineState&&) = delete;
    
    inline StateMachineState(TStatesEnum FirstState) : _state(FirstState), _firstTime(true) {}

    inline void ChangeState(TStatesEnum To)
    {
        _state = To;
        _firstTime = true;
    }

    inline explicit operator TStatesEnum() const
    {
        return _state;
    }

    inline bool IsFirstTime()  // Oneshot per change of state (ChangeState() call)
    {
        bool firstTime = _firstTime;
        _firstTime = false;
        return firstTime;
    }

private:
    TStatesEnum _state;
    bool _firstTime;
};

//** Simple fixed size Stack implementation 
//
template <typename T, int TSize>
class Stack
{
private:
    T       _stack[TSize];
    int     _top;

public:
    inline Stack() : _top(0) {}

    inline void Push(const T& Value)
    {
        if (_top >= TSize)
        {
            $FailFast();
        }

        _stack[_top++] = Value;
    }

    inline void Pop()
    {
        $Assert(_top > 0);
        _top--;
    }

    inline T& Top()
    {
        $Assert(_top > 0);
        return _stack[_top - 1];
    }

    inline bool IsEmpty() { return _top == 0; }
    inline bool IsFull() { return _top == TSize; }
    int Size() { return _top; }
};




