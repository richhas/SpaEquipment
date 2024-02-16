// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// NTP Client task related implementation

#include "NtpClient.hpp"



void NTPClient::setup()
{
    //_timeServerIPAddress.fromString(_timeServerIP);

    _timeServerIPAddress = IPAddress(162, 159, 200, 123); // time.nist.gov
    logger.Printf(Logger::RecType::Progress, "NTPClient: Starting - time server: %s", _timeServerIPAddress.toString().c_str());
    _udp = move(NetworkTask::CreateUDP());
    $Assert(_udp.get() != nullptr);
    $Assert(_udp->begin(_localPort));
}

void NTPClient::loop()
{
    enum class State
    {
        WaitForNetwork,
        Start,
        WaitForResponse,
        Done,
    };
    static StateMachineState<State> state(State::WaitForNetwork);

    static Timer timer;

    switch ((State)state)
    {
    case State::WaitForNetwork:
    {
        if (network.IsAvailable())
        {
            state.ChangeState(State::Start);
        }
    }
    break;

    case State::Start:
    {
        if (state.IsFirstTime())
        {
            sendNTPpacket();
            timer.SetAlarm(1000);
        }

        // Wait for 1 second for the request to be sent
        if (timer.IsAlarmed())
        {
            // Now allow 1 second for the response
            timer.SetAlarm(1000);
            state.ChangeState(State::WaitForResponse);
        }
    }
    break;

    case State::WaitForResponse:
    {
        bool foundRsp = false;

        while (_udp->parsePacket())     // drain and process any response packets
        {
            if (_udp->read(_packetBuffer, _ntpPacketSize) == _ntpPacketSize)
            {
                uint32_t highWord = word(_packetBuffer[40], _packetBuffer[41]);
                uint32_t lowWord = word(_packetBuffer[42], _packetBuffer[43]);
                uint32_t secsSince1900 = highWord << 16 | lowWord;
                const uint32_t seventyYears = 2208988800UL;
                uint32_t epoch = secsSince1900 - seventyYears;
                static RTCTime time(epoch);
                time.setUnixTime(epoch);

                logger.Printf(Logger::RecType::NtpRef, "%s", time.toString().c_str());
                if (!RTC.setTime(time))
                {
                    logger.Printf(Logger::RecType::Warning, "NtpClient: RTC.setTime() failed!");
                }

                foundRsp = true;
            }
            else
            {
                logger.Printf(Logger::RecType::Warning, "NtpClient: Response is bad");
            }
        }

        if (foundRsp)
        {
            timer.SetAlarm(uint32_t(60000) * 10); // 10 minutes until next update
            state.ChangeState(State::Done);
            return;
        }

        if (timer.IsAlarmed())
        {
            // No response, try again after a delay
            logger.Printf(Logger::RecType::Warning, "NtpClient: Response timeout");
            timer.SetAlarm(10000); // 10 seconds
            state.ChangeState(State::Done);
        }
    }
    break;

    case State::Done:
    {
        bool networkAvailable = network.IsAvailable();
        if (timer.IsAlarmed() || !networkAvailable)
        {
            if (!networkAvailable)
            {
                logger.Printf(Logger::RecType::Progress, "NtpClient: Network is not available");
            }
            state.ChangeState(State::WaitForNetwork);
        }
    }
    break;

    default:
    {
        $FailFast();
    }
    }
}

void NTPClient::sendNTPpacket()
{
    memset(_packetBuffer, 0, _ntpPacketSize);
    _packetBuffer[0] = 0b11100011; // LI, Version, Mode
    _packetBuffer[1] = 0;          // Stratum, or type of clock
    _packetBuffer[2] = 6;          // Polling Interval
    _packetBuffer[3] = 0xEC;       // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    _packetBuffer[12] = 49;
    _packetBuffer[13] = 0x4E;
    _packetBuffer[14] = 49;
    _packetBuffer[15] = 52;

    _udp->beginPacket(_timeServerIPAddress, _ntpPort);
    _udp->write(_packetBuffer, _ntpPacketSize);
    _udp->endPacket();
}

NTPClient ntpClient;
