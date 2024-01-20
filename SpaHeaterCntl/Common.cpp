/*
    Common Support Implementations 

    Copyright TinyBus 2020
*/
#include <Arduino.h>
#include "common.h"

#include <cstdarg>
#include <cstdio>


void __attribute__ ((noinline)) FailFast(char* FileName, int LineNumber)
{
    Serial.print("\n\r**** FAIL FAST ----- at line: ");
    Serial.print(LineNumber);
    Serial.print(" in file: '");
    Serial.print(FileName);
    Serial.print("'");
    while (true) 
    {
        //__BKPT(0xFF);
    }
}

// CRC 16/32 Support
/*
#if SAMD51_SERIES
    void CRC::DoCRC(uint32_t AlgType, byte* Buffer, uint16_t Size, uint32_t StartingCRC)
    {
        bool dmaWasEnabled = DMAC->CTRL.bit.DMAENABLE;

        DMAC->CTRL.bit.DMAENABLE = 0;                   // Make sure DMA is disabled
        DMAC->CRCCTRL.reg = DMAC_CRCCTRL_RESETVALUE;    // Reset CRC

        DMAC->CRCDATAIN.reg = StartingCRC;  // Reset the CRC engine data-in register
        DMAC->CRCCHKSUM.reg = StartingCRC;  // Reset the CRC engine checksum register

        DMAC->CRCCTRL.reg = 
            DMAC_CRCCTRL_CRCBEATSIZE(DMAC_CRCCTRL_CRCBEATSIZE_BYTE_Val) |   // byte processing
            DMAC_CRCCTRL_CRCPOLY(AlgType)                               |   // Alg Type
            DMAC_CRCCTRL_CRCSRC(DMAC_CRCCTRL_CRCSRC_IO_Val)             |   // IO Interface selected (non-DMA)
            DMAC_CRCCTRL_CRCMODE(DMAC_CRCCTRL_CRCMODE_DEFAULT_Val);         // Default operating mode

        DMAC->CTRL.bit.DMAENABLE = 1;       // Enable the CRC engine

        while (Size > 0)
        {
            DMAC->CRCDATAIN.reg = (uint32_t)(*Buffer);
            while(!DMAC->CRCSTATUS.bit.CRCBUSY);          // Wait for the CRC engine to complete    
            DMAC->CRCSTATUS.bit.CRCBUSY = 1;              // Clear the CRCBUSY flag
            Size--;
            Buffer++;
        }

        DMAC->CTRL.bit.DMAENABLE = dmaWasEnabled;           // Restore DMA enable on entry
    }

    uint32_t CRC::CRC32(byte* Buffer, uint16_t Size, uint32_t StartingCRC)
    {
        CRC::DoCRC(DMAC_CRCCTRL_CRCPOLY_CRC32_Val, Buffer, Size, StartingCRC);
        uint32_t result = DMAC->CRCCHKSUM.reg;
        return result;
    }

    uint16_t CRC::CRC16(byte* Buffer, uint16_t Size, uint16_t StartingCRC)
    {
        CRC::DoCRC(DMAC_CRCCTRL_CRCPOLY_CRC16_Val, Buffer, Size, StartingCRC);
        uint16_t result = (uint16_t)DMAC->CRCCHKSUM.reg;
        return result;
    }

#else
    #error "Target build processor is not known"
#endif
*/

//* Common support functions
int printf(Stream& ToStream, const char* Format, ...)
{
    char    buffer[256];
    va_list args;

    va_start(args, Format);
    int size = vsnprintf(&buffer[0], sizeof(buffer), Format, args);
    ToStream.print(&buffer[0]);
    va_end(args);

    return size;
}

