// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// Network related implementations

#include "SpaHeaterCntl.hpp"
#include "Network.hpp"
#include "WiFiJoinApTask.hpp"

//** Network configuration record
#pragma pack(push, 1)
struct NetworkConfig
{
    char        _signature[7];
        constexpr static char _sigConst[sizeof(_signature)] = "NetCfg";

    bool        _useDHCP;
    uint32_t    _ipAddr;
    uint32_t    _subnetMask;
    uint32_t    _gateway;
    uint32_t    _dnsServer;
};
#pragma pack(pop)

FlashStore<NetworkConfig, PS_NetworkConfigBase> networkConfigRecord;
static_assert(sizeof(networkConfigRecord) <= PS_NetworkConfigBlkSize, "NetworkConfig size does not fit in config store");

//** Network related command line commands for admin console
//* common network command line processors
CmdLine::Status ExitCmdProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Pop(); // Back to previous menu
    return CmdLine::Status::Ok;
}

//* Network Addressing Configuration
CmdLine::Status ShowAddressingConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    printf(CmdStream, "Network Addressing Configuration: DHCP: %s; IP: %s; Subnet: %s; Gateway: %s; DNS: %s\n",
        networkConfigRecord.GetRecord()._useDHCP ? "Yes" : "No",
        IPAddress(IPAddress(networkConfigRecord.GetRecord()._ipAddr)).toString().c_str(),
        IPAddress(IPAddress(networkConfigRecord.GetRecord()._subnetMask)).toString().c_str(),
        IPAddress(IPAddress(networkConfigRecord.GetRecord()._gateway)).toString().c_str(),
        IPAddress(IPAddress(networkConfigRecord.GetRecord()._dnsServer)).toString().c_str());

    return CmdLine::Status::Ok;
}

CmdLine::Status SetAddressingConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if ((Argc == 2) && strcmp(Args[1], "?") == 0)
    {
        CmdStream.println("    Format: <IP|Subnet|Gateway|DNS> \"#.#.#.#\"");
        CmdStream.println("     -or- : DHCP true|false");

        return CmdLine::Status::Ok;
    }
    if (Argc != 3)
    {
        return CmdLine::Status::UnexpectedParameterCount;
    }

    if (strcmp(Args[1], "DHCP") == 0)
    {
        if (strcmp(Args[2], "true") == 0)
        {
            networkConfigRecord.GetRecord()._useDHCP = true;
        }
        else if (strcmp(Args[2], "false") == 0)
        {
            networkConfigRecord.GetRecord()._useDHCP = false;
        }
        else
        {
            return CmdLine::Status::InvalidParameter;
        }
    }
    else if (strcmp(Args[1], "IP") == 0)
    {
        networkConfigRecord.GetRecord()._ipAddr = (uint32_t)(IPAddress(Args[2]));
    }
    else if (strcmp(Args[1], "Subnet") == 0)
    {
        networkConfigRecord.GetRecord()._subnetMask = (uint32_t)(IPAddress(Args[2]));
    }
    else if (strcmp(Args[1], "Gateway") == 0)
    {
        networkConfigRecord.GetRecord()._gateway = (uint32_t)(IPAddress(Args[2]));
    }
    else if (strcmp(Args[1], "DNS") == 0)
    {
        networkConfigRecord.GetRecord()._dnsServer = (uint32_t)(IPAddress(Args[2]));
    }
    else
    {
        return CmdLine::Status::InvalidParameter;
    }

    return CmdLine::Status::Ok;
}

CmdLine::Status WriteNetworkConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    networkConfigRecord.Write();
    networkConfigRecord.Begin();
    $Assert(networkConfigRecord.IsValid());
    return CmdLine::Status::Ok;
}

CmdLine::ProcessorDesc networkAddressingConfigProcessors[] =
{
    {SetAddressingConfigProcessor, "set", "Set Network Addressing parameters. Format: <DHCP|IP|Subnet|Gateway|DNS|?> <parameter>"},
    {ShowAddressingConfigProcessor, "show", "Show Network Addressing Configuration"},
    {WriteNetworkConfigProcessor, "write", "Write Network Addressing Configuration to EEPROM"},
    {ExitCmdProcessor, "exit", "Exit Network Addressing Menu"},
};
int const LengthOfNetworkAddressingConfigProcessors = sizeof(networkAddressingConfigProcessors) / sizeof(networkAddressingConfigProcessors[0]);

//* Network WiFi Configuration and control
CmdLine::Status SetWiFiConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc != 4)
    {
        return CmdLine::Status::UnexpectedParameterCount;
    }

    wifiJoinApTask.SetConfig(Args[1], Args[2], Args[3]);

    return CmdLine::Status::Ok;
}

CmdLine::Status DisconnectNetProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    network.disconnect();
    return CmdLine::Status::Ok;
}

CmdLine::Status ClearEEPROMProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    CmdStream.println("Starting EEPROM Erase...");
    wifiJoinApTask.EraseConfig();
    CmdStream.println("EEPROM Erase has completed");
    return CmdLine::Status::Ok;
}

CmdLine::Status AddressingConfigProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Push(
        networkAddressingConfigProcessors[0], 
        LengthOfNetworkAddressingConfigProcessors, 
        "NetAddressing");

    return CmdLine::Status::Ok;
}

CmdLine::ProcessorDesc networkTaskCmdProcessors[] =
    {
        {SetWiFiConfigProcessor, "setWiFi", "Set the WiFi Config. Format: <SSID> <Net Password> <Admin Password>"},
        {DisconnectNetProcessor, "stopNet", "Disconnect Network"},
        {ClearEEPROMProcessor, "clearWiFiConfig", "Clear WiFi Config from EEPROM"},
        {AddressingConfigProcessor, "addressing", "Network Addressing Configuration"},
        {ExitCmdProcessor, "exit", "Exit Network Menu"},
};
int const LengthOfNetworkTaskCmdProcessors = sizeof(networkTaskCmdProcessors) / sizeof(networkTaskCmdProcessors[0]);

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
    networkConfigRecord.Begin();
    strcpy(networkConfigRecord.GetRecord()._signature, NetworkConfig::_sigConst);
    if (!networkConfigRecord.IsValid())
    {
        logger.Printf(Logger::RecType::Progress, "NetworkTask: No valid network configuration found - creating default");
        networkConfigRecord.GetRecord()._useDHCP = true;
        networkConfigRecord.GetRecord()._ipAddr = 0;
        networkConfigRecord.GetRecord()._subnetMask = 0;
        networkConfigRecord.GetRecord()._gateway = 0;
        networkConfigRecord.GetRecord()._dnsServer = 0;
        networkConfigRecord.Write();
        networkConfigRecord.Begin();
        $Assert(networkConfigRecord.IsValid());
    }
}

void NetworkTask::disconnect()
{
    WiFi.disconnect();
    _isAvailable = false;
}

bool NetworkTask::IsAvailable()
{
    return _isAvailable;
}

void NetworkTask::setup()
{
    wifiJoinApTask.Setup();
}

void NetworkTask::loop()
{
    enum class State
    {
        WaitForConfig,
        StartWiFiBegin,
        Connected,
        DelayAfterDisconnect
    };
    static StateMachineState<State> state(State::WaitForConfig);

    switch ((State)state)
    {
        //* Wait for the WiFiJoinApTask to complete and get the network configuration
        case State::WaitForConfig:
        {
            wifiJoinApTask.Loop();

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
        static Timer delayTimer;
        
        case State::StartWiFiBegin:
        {
            if (state.IsFirstTime())
            {
                status = WiFi.status();
                if (status != WL_CONNECTED)
                {
                    _isAvailable = false;
                    WiFi.setHostname("SpaHeaterCntl");

                    // Dump the network configuration
                    logger.Printf(Logger::RecType::Progress, "NetworkTask: Network Configuration: DHCP: %s; IP: %s; Subnet: %s; Gateway: %s; DNS: %s",
                                  networkConfigRecord.GetRecord()._useDHCP ? "Yes" : "No",
                                  IPAddress(IPAddress(networkConfigRecord.GetRecord()._ipAddr)).toString().c_str(),
                                  IPAddress(IPAddress(networkConfigRecord.GetRecord()._subnetMask)).toString().c_str(),
                                  IPAddress(IPAddress(networkConfigRecord.GetRecord()._gateway)).toString().c_str(),
                                  IPAddress(IPAddress(networkConfigRecord.GetRecord()._dnsServer)).toString().c_str());

                    logger.Printf(Logger::RecType::Progress, "NetworkTask: Attempting to connect to WPA SSID: '%s'", _ssid);

                    WiFi.setHostname("SpaHeaterCntl");
                    if (!networkConfigRecord.GetRecord()._useDHCP)
                    {
                        // Set the static IP addressing given dhcp has been disabled
                        WiFi.config(
                            IPAddress(networkConfigRecord.GetRecord()._ipAddr),
                            IPAddress(networkConfigRecord.GetRecord()._dnsServer),
                            IPAddress(networkConfigRecord.GetRecord()._gateway),
                            IPAddress(networkConfigRecord.GetRecord()._subnetMask));

                        logger.Printf(Logger::RecType::Progress, "NetworkTask: Using static IP addressing");
                    }

                    status = WiFi.begin(_ssid, _networkPassword);
                    if (status != WL_CONNECTED)
                    {
                        // If we get here, we are retrying but delaying for a bit - connection failed
                        logger.Printf(Logger::RecType::Progress, "NetworkTask: WiFi.begin() failed with status: %d", status);
                        delayTimer.SetAlarm(5000);          // cause retry in 5 seconds
                        return;
                    }
                }

                uint8_t mac[6];
                WiFi.macAddress(&mac[0]);
                String ourMAC = MacToString(mac);

                IPAddress ip = WiFi.localIP();
                if ((networkConfigRecord.GetRecord()._useDHCP) && (ip == IPAddress(0, 0, 0, 0)))
                {
                    logger.Printf(Logger::RecType::Progress, "NetworkTask: DHCP IP address not assigned: SSID: '%s' (MAC: %s) - retrying...", 
                        _ssid, 
                        MacToString(mac).c_str());
                    
                    WiFi.disconnect();
                    delayTimer.SetAlarm(4000);              // cause retry in 4 seconds
                    return;
                }

                // Have a connection - move to Connected state
                String      ipAddr = ip.toString();

                WiFi.BSSID(&mac[0]);
                String      apBSSID = MacToString(mac);

                logger.Printf(Logger::RecType::Progress, "NetworkTask: Connected: SSID: '%s' @ %s (MAC: %s); AP BSSID: %s", 
                    _ssid, 
                    ipAddr.c_str(),
                    ourMAC.c_str(),
                    apBSSID.c_str());

                state.ChangeState(State::Connected);
                return;
            }// end: if (state.IsFirstTime())

            // If we get here, we are retrying but delaying for a bit
            if (delayTimer.IsAlarmed())
            {
                state.ChangeState(State::StartWiFiBegin);
            }
        }
        break;

        case State::Connected:
        {
            if (state.IsFirstTime())
            {
                _isAvailable = true;
                delayTimer.SetAlarm(2000);  // only check every 2 seconds for WiFi status - reduce overhead to CoProc
            }

            if (delayTimer.IsAlarmed())
            {
                status = WiFi.status();
                if (status != WL_CONNECTED)
                {
                    _isAvailable = false;
                    WiFi.disconnect();

                    logger.Printf(Logger::RecType::Progress, "NetworkTask: Disconnected - delay 2 seconds before retrying...");
                    state.ChangeState(State::DelayAfterDisconnect);
                    return;
                }

                delayTimer.SetAlarm(2000);  // only check every 2 seconds for WiFi status - reduce overhead to CoProc
            }
        }
        break;

        case State::DelayAfterDisconnect:
        {
            if (state.IsFirstTime())
            {
                delayTimer.SetAlarm(2000);  // only check after 2 seconds
            }

            if (delayTimer.IsAlarmed())
            {
                state.ChangeState(State::StartWiFiBegin);
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
