/*
    Common Support Implementations 

    Copyright TinyBus 2020
*/
#include <Arduino.h>
#include "common.hpp"

#include <cstdarg>
#include <cstdio>


/**
 * @brief Causes the program to fail fast and enter an infinite loop.
 * 
 * This function is used to indicate a critical error in the program and halt its execution.
 * It prints the file name and line number where the error occurred, sets the built-in LED pin as an output,
 * and enters an infinite loop, toggling the LED state at approx 10 times/sec.
 * 
 * @param FileName The name of the file where the error occurred.
 * @param LineNumber The line number where the error occurred.
 */
void __attribute__ ((noinline)) FailFast(const char* FileName, int LineNumber)
{
    Serial.print("\n\r**** FAIL FAST ----- at line: ");
    Serial.print(LineNumber);
    Serial.print(" in file: '");
    Serial.print(FileName);
    Serial.print("'");
    Serial.flush();

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 0);
    noInterrupts();

    // Fast blink the LED to indicate a failure - forever
    while (true) 
    {
        static volatile uint8_t      delayCount;

        delayCount = 50;
        while (delayCount-- > 0) delayMicroseconds(1000);
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        //__BKPT(0xFF);                     // uncomment if using a hardware debugger
    }
}


//* Common support functions
SharedBuffer<256> sharedPrintfBuffer;

int printf(Stream &ToStream, const char *Format, ...)
{
    int size;
    va_list args;

    va_start(args, Format);
    {   // take exclusive access to sharedPrintfBuffer - note: dtor of handle releases the lock
        auto handle = sharedPrintfBuffer.GetHandle();
        char* buffer = (char*)handle.GetBuffer();

        size = vsnprintf(buffer, handle.GetSize(), Format, args);
        ToStream.print(buffer);
    }
    va_end(args);

    return size;
}
