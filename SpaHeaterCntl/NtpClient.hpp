// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// NTP Client task related definitions

#pragma once
#include "SpaHeaterCntl.hpp"

class NTPClient : public ArduinoTask
{
private:
    constexpr static uint32_t _ntpPacketSize = 48;
    constexpr static char *_timeServerIP = "192.168.3.202"; // core2.maxie.hasha.org
    constexpr static uint16_t _ntpPort = 123;
    constexpr static uint32_t _localPort = 2390;
    uint8_t _packetBuffer[_ntpPacketSize];
    shared_ptr<UDP> _udp;
    IPAddress _timeServerIPAddress;

private:
    void sendNTPpacket();

protected:
    virtual void setup() override;
    virtual void loop() override;
};

extern NTPClient ntpClient;
