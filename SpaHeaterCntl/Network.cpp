// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// Network related implementations

#include "SpaHeaterCntl.h"

//* Core Network Component implementation
void NetworkTask::Begin(const char* SSID, const char* NetPassword)
{
    _ssid = SSID;
    _networkPassword = NetPassword;
    logger.Printf(Logger::RecType::Info, "NetworkTask Started for '%s'", &_ssid[0]);
}

void NetworkTask::setup()
{
}

void NetworkTask::loop()
{
    // Give any registered Services and Clients time to run.
    if (_registeredServices.size() > 0)
    {
        vector<shared_ptr<NetworkService>> services = _registeredServices;

        for (shared_ptr<NetworkService>& service : services)
        {
            service->loop();
        }
    }

    if (_registeredClients.size() > 0)
    {
        vector<shared_ptr<NetworkClient>> clients = _registeredClients;
        for (shared_ptr<NetworkClient> &client : clients)
        {
            client->loop();
        }
    }

    enum class State
    {
        StartWiFiBegin,
        DelayAfterWiFiBegin,
        Connected,
    };

    static bool     firstTime = true;
    static State    state;
    static Timer    delayTimer;
    static int      status;

    if (firstTime)
    {
        firstTime = false;
        status = WL_IDLE_STATUS;
        state = State::StartWiFiBegin;
    }

    switch (state)
    {
        case State::StartWiFiBegin:
        {
            if (status != WL_CONNECTED)
            {
                logger.Printf(Logger::RecType::Info, "NetworkTask: Attempting to connect to WPA SSID: '%s'", _ssid);
                status = WiFi.begin(_ssid, _networkPassword);

                // wait 10 secs for connection
                delayTimer.SetAlarm(10000);
                state = State::DelayAfterWiFiBegin;
                return;
            }

            uint8_t     mac[6];
            String      ipAddr = WiFi.localIP().toString();

            WiFi.macAddress(&mac[0]);
            String      ourMAC = MacToString(mac);

            WiFi.BSSID(&mac[0]);
            String      apBSSID = MacToString(mac);

            logger.Printf(Logger::RecType::Info, "NetworkTask: Connected to SSID: '%s' @ %s (MAC: %s); AP BSSID: %s", 
                _ssid, 
                ipAddr.c_str(),
                ourMAC.c_str(),
                apBSSID.c_str());

            state = State::Connected;
        }
        break;

        case State::DelayAfterWiFiBegin:
        {
            if (delayTimer.IsAlarmed())
            {
                state = State::StartWiFiBegin;
            }
        }
        break;

        case State::Connected:
        {
            status = WiFi.status();

            if (status != WL_CONNECTED)
            {
                logger.Printf(Logger::RecType::Info, "NetworkTask: Disconnected");
                state = State::StartWiFiBegin;
                return;
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

void NetworkTask::RegisterService(shared_ptr<NetworkService> ToRegister)
{
    Serial.println("NetworkTask::RegisterService");
    _registeredServices.push_back(ToRegister);
}


//* NetworkService base class implementation
NetworkService::NetworkService()
{
}

NetworkService::~NetworkService()
{
    $FailFast();            // TODO: finish to allow services to unhook and go away
}

void NetworkService::Begin(int Port)
{
    Serial.println("NetworkService::Begin");
    $Assert(_firstTime);                // forces setup() to be called first
    _tcpPort = Port;
    network.RegisterService(shared_from_this());
}

void NetworkService::setup()
{
    _firstTime = true;
}

void NetworkService::loop()
{
    class DebugWiFiClient : public WiFiClient
    {
    public:
        inline int GetSock()
        {
            return _sock;
        }
    };

    if (_registeredClients.size() > 0)
    {
        int ix = 0;

        for (shared_ptr<Client>& client : _registeredClients)
        {
            if (client->GetClient().connected())
            {
                client->loop();
            }
            else
            {
                // Client connected dropped
                logger.Printf(Logger::RecType::Info, "NetworkService: Client connection dropped: Socket: %d - TcpPort: %d", 
                            ((DebugWiFiClient&)(client->GetClient())).GetSock(),
                            _tcpPort);

                shared_ptr<Client>  c = client;
                _registeredClients.erase(_registeredClients.begin() + ix);
                c->GetClient().stop();
                c->End();
                return;                     // Just skip to next cycle - saves from snapshoting _registeredClients
            }

            ix++;
        }
    }

    if (_firstTime)
    {
        _firstTime = false;
        _status = WL_IDLE_STATUS;
        _state = State::StartServer;
    }

    switch (_state)
    {
        case State::StartServer:
        {
            _status = WiFi.status();
            if (_status == WL_CONNECTED)
            {
                logger.Printf(Logger::RecType::Info, "NetworkService: Attempting to start server on port %d", _tcpPort);
                _server.begin(_tcpPort);
                _state = State::Connected;
                return;
            }
        }
        break;

        case State::Connected:
        {
            _status = WiFi.status();
            if (_status != WL_CONNECTED)
            {
                logger.Printf(Logger::RecType::Info, "NetworkService: Disconnected");
                _server.end();
                _state = State::StartServer;
                return;
            }

            WiFiClient client = _server.available();
            if (client)
            {
                for (shared_ptr<Client> c : _registeredClients)
                {
                    if (c->GetClient() == client)
                    {
                        // Drop dups
                        return;
                    }
                }

                logger.Printf(Logger::RecType::Info, "NetworkService: Client connected: Socket: %d on TcpPort: %d", 
                              ((DebugWiFiClient*)&client)->GetSock(),
                              _tcpPort);

                shared_ptr<Client> newClient = CreateClient(shared_from_this(), client);
                if (newClient.get() != nullptr)
                {
                    newClient->setup();
                    _registeredClients.push_back(newClient);
                    newClient->Begin();
                }
                else
                {
                    logger.Printf(Logger::RecType::Info, "NetworkService: Client rejected");
                    client.stop();
                }
            }
        }
        break;

        default:
            $FailFast();
    }
}

//* Common base class for all NetworkService::Client derivations
NetworkService::Client::Client(shared_ptr<NetworkService> Parent, WiFiClient Client)
    :   _parentService(Parent),
        _client(Client)
{
}

NetworkService::Client::~Client()
{
}

//* Outgoing client base implementation
NetworkClient::NetworkClient()
    :   _firstTime(false)
{
}

NetworkClient::~NetworkClient()
{
}

void NetworkClient::Begin()
{
    Serial.println("NetworkClient::Begin");
    $Assert(_firstTime); // forces setup() to be called first
    network.RegisterClient(shared_from_this());
}

void NetworkClient::setup()
{
    $Assert(_firstTime == false);
    _firstTime = true;
}

void NetworkClient::loop()
{
    if (_firstTime)
    {
        _firstTime = false;
        _state = State::WaitForWiFi;
    }

    switch (_state)
    {
        case State::WaitForWiFi:
        {
            _status = WiFi.status();
            if (_status == WL_CONNECTED)
            {
                OnNetConnected();
                _state = State::Connected;
                return;
            }

            _delayTimer.SetAlarm(1000);
            _state = State::DelayForWiFi;
        }
        break;

        case State::DelayForWiFi:
        {
            if (_delayTimer.IsAlarmed())
            {
                _state = State::WaitForWiFi;
            }
        }

        case State::Connected:
        {
            _status = WiFi.status();
            if (_status != WL_CONNECTED)
            {
                OnNetDisconnected();
                _state = State::WaitForWiFi;
                return;
            }

            OnLoop();           // Derived class can do what it wants            
        }
        break;

        default:
            $FailFast();
    }
}



//* TELNET Admin console implementation
TelnetServer::TelnetServer()
{
}

TelnetServer::~TelnetServer()
{
}

// Client factory
shared_ptr<NetworkService::Client> TelnetServer::CreateClient(shared_ptr<NetworkService> Parent, WiFiClient ClientToUse)
{
    shared_ptr<TelnetServer::Client> newClientObj;
    newClientObj = make_shared<TelnetServer::Client>(Parent, ClientToUse);
    $Assert(newClientObj != nullptr);
    return newClientObj;
}

//* TELNET Admin Console implementation
TelnetServer::Client::Client(shared_ptr<NetworkService> Parent, WiFiClient Client)
    : NetworkService::Client(Parent, Client)
{
}

TelnetServer::Client::~Client()
{
}

void TelnetServer::Client::Begin()
{
    _console = make_shared<ConsoleTask>(_client);
    $Assert(_console != nullptr);
    _console->setup();
}

void TelnetServer::Client::End()
{
}

void TelnetServer::Client::setup()
{
}

void TelnetServer::Client::loop()
{
    _console->loop();
}

