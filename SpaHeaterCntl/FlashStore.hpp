/*
    Definitions for Flash Storage on Arduino UNO R4 and similar boards

    Copyright TinyBus 2024
*/
#pragma once

#include "common.hpp"
#include <memory.h>
#include <EEPROM.h>
#include <Arduino_CRC32.h>

/** EEPROM config support */
//** Persistant storage partitions (8k max)
constexpr uint16_t PS_WiFiConfigBase = 0;
constexpr uint16_t PS_WiFiConfigBlkSize = 256;
constexpr uint16_t PS_BootRecordBase = PS_WiFiConfigBase + PS_WiFiConfigBlkSize;
constexpr uint16_t PS_BootRecordBlkSize = 32;
constexpr uint16_t PS_TempSensorsConfigBase = PS_BootRecordBase + PS_BootRecordBlkSize;
constexpr uint16_t PS_TempSensorsConfigBlkSize = 64;
constexpr uint16_t PS_MQTTBrokerConfigBase = PS_TempSensorsConfigBase + PS_TempSensorsConfigBlkSize;
constexpr uint16_t PS_MQTTBrokerConfigBlkSize = 256;
constexpr uint16_t PS_BoilerConfigBase = PS_MQTTBrokerConfigBase + PS_MQTTBrokerConfigBlkSize;
constexpr uint16_t PS_BoilerConfigBlkSize = 64;
constexpr uint16_t PS_NetworkConfigBase = PS_BoilerConfigBase + PS_BoilerConfigBlkSize;
constexpr uint16_t PS_NetworkConfigBlkSize = 64;

constexpr uint16_t PS_TotalConfigSize = PS_NetworkConfigBase + PS_NetworkConfigBlkSize;

constexpr uint16_t PS_TotalDiagStoreSize = (8 * 1024) - PS_TotalConfigSize;
constexpr uint16_t PS_DiagStoreBase = PS_TotalDiagStoreSize;


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
            uint32_t    _crc;                           // CRC of the above
        };

        uint8_t         _bytes[sizeof(TBlk) + sizeof(uint32_t)];    
    };

private:
    uint32_t ComputeCRC()
    {
        Arduino_CRC32 crc;
        return crc.calc((uint8_t*)&_record, sizeof(_record));
    }

    void Fill()
    {
        EEPROM.get(TBaseOfRecord, *this);
    }

    void Flush()
    {
        EEPROM.put(TBaseOfRecord, *this);
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
        uint32_t crc = ComputeCRC();
        return (crc == _crc);
    }

    void Write()
    {
        _crc = ComputeCRC();
        Flush();
    }

    void Erase()
    {
        memset(&_bytes[0], 0, sizeof(FlashStore::_bytes));
        Flush();
    }
};
#pragma pack(pop)

