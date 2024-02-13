// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// Network related implementations

#include "SpaHeaterCntl.hpp"
#include "Network.hpp"



//* Core Network Component implementation
shared_ptr<Client> NetworkTask::CreateClient() 
{ 
    return make_shared<WiFiClient>(); 
}

shared_ptr<Server> NetworkTask::CreateServer(int Port) 
{ 
    return make_shared<WiFiServer>(Port); 
}

shared_ptr<UDP> NetworkTask::CreateUDP() 
{ 
    return make_shared<WiFiUDP>(); 
}

shared_ptr<Client> NetworkTask::available(shared_ptr<Server> Server)
{
    WiFiClient client = (static_pointer_cast<WiFiServer>(Server))->available();
    if (client)
    {
        return make_shared<WiFiClient>(client);
    }
    return nullptr;
}

void NetworkTask::begin(shared_ptr<Server> Server)
{
    (static_pointer_cast<WiFiServer>(Server))->begin();
}

void NetworkTask::end(shared_ptr<Server> Server)
{
    (static_pointer_cast<WiFiServer>(Server))->end();
}


void NetworkTask::Begin()
{
    logger.Printf(Logger::RecType::Progress, "NetworkTask: Starting...");
}

void NetworkTask::setup()
{
    wifiJoinApTask.setup();
}

void NetworkTask::loop()
{
    wifiJoinApTask.loop();

    enum class State
    {
        WaitForConfig,
        StartWiFiBegin,
        Connected,
    };
    static StateMachineState<State> state(State::WaitForConfig);

#if 0
    // Give any registered Services and Clients time to run.
    if (((State)state != State::WaitForConfig) && (_registeredServices.size() > 0))
    {
        vector<shared_ptr<NetworkService>> services = _registeredServices;
        for (shared_ptr<NetworkService> &service : services)
        {
            service->loop();
        }
    }
#endif

    switch ((State)state)
    {
        //* Wait for the WiFiJoinApTask to complete and get the network configuration
        case State::WaitForConfig:
        {
            if (state.IsFirstTime())
            {
                logger.Printf(Logger::RecType::Progress, "NetworkTask: Waiting for configuration from wifiJoinApTask...");
            }
            if (wifiJoinApTask.IsCompleted())
            {
                $Assert(wifiJoinApTask.IsConfigured()); // should be true by now

                wifiJoinApTask.GetNetworkConfig(_ssid, _networkPassword);

                logger.Printf(Logger::RecType::Progress, "NetworkTask: Have configuration for SSID: '%s'", &_ssid[0]);
                state.ChangeState(State::StartWiFiBegin);
            }
        }
        break;

        static int status;
        case State::StartWiFiBegin:
        {
            static Timer delayTimer;

            if (state.IsFirstTime())
            {
                status = WiFi.status();
                if (status != WL_CONNECTED)
                {
                    _isAvailable = false;
                    logger.Printf(Logger::RecType::Progress, "NetworkTask: Attempting to connect to WPA SSID: '%s'", _ssid);
                    status = WiFi.begin(_ssid, _networkPassword);

                    // wait 5 secs for connection
                    delayTimer.SetAlarm(5000);
                    return;
                }

                uint8_t mac[6];
                WiFi.macAddress(&mac[0]);
                String ourMAC = MacToString(mac);

                IPAddress ip = WiFi.localIP();
                if (ip == IPAddress(0, 0, 0, 0))
                {
                    logger.Printf(Logger::RecType::Progress, "NetworkTask: DHCP IP addrress not assigned: SSID: '%s' (MAC: %s) - retrying...", 
                        _ssid, 
                        MacToString(mac).c_str());
                    
                    WiFi.disconnect();
                    WiFi.end();

                    delayTimer.SetAlarm(4000);
                    return;
                }

                String      ipAddr = ip.toString();

                WiFi.BSSID(&mac[0]);
                String      apBSSID = MacToString(mac);

                logger.Printf(Logger::RecType::Progress, "NetworkTask: Connected to SSID: '%s' @ %s (MAC: %s); AP BSSID: %s", 
                    _ssid, 
                    ipAddr.c_str(),
                    ourMAC.c_str(),
                    apBSSID.c_str());

                state.ChangeState(State::Connected);
                return;
            }

            // If we get here, we are retrying but delaying for a bit
            if (delayTimer.IsAlarmed())
            {
                state.ChangeState(State::StartWiFiBegin);
            }
        }
        break;

        case State::Connected:
        {
            status = WiFi.status();
            if (status != WL_CONNECTED)
            {
                logger.Printf(Logger::RecType::Progress, "NetworkTask: Disconnected");
                state.ChangeState(State::StartWiFiBegin);
                return;
            }

            if (state.IsFirstTime())
            {
                _isAvailable = true;
            }
        }
        break;

        default:
            $FailFast();
    }
}

String &NetworkTask::AppendHEX(uint8_t Byte, String &AddTo)
{
    static char hexDigit[] = "0123456789ABCDEF";

    AddTo += hexDigit[((Byte & 0xF0) >> 4)];
    AddTo += hexDigit[(Byte & 0x0F)];

    return AddTo;
}

String NetworkTask::MacToString(uint8_t *Mac)
{
    String result = "";
    for (int i = 5; i >= 0; i--)
    {
        AppendHEX(Mac[i], result);
        if (i > 0)
        {
            result += ":";
        }
    }

    return result;
}


NetworkTask     network;
