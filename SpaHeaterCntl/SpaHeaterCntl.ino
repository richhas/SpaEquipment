// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus

#include "SpaHeaterCntl.h"
#include <vector>

using namespace std;


ConsoleTask     consoleTask(Serial);
LedMatrixTask   matrixTask(Serial, 50);
WiFiJoinApTask  wifiJoinApTask(Serial, "SpaHeaterAP", "123456789");
Logger          logger(Serial);
FlashStore<BootRecord, PS_BootRecordBase> bootRecord;

// Tasks to add:
//    Not WiFi dependent:
//      Thermo
//      Boiler
//      DiagLog
//
//    WiFi dependent:
//      NetworkMonitor
//
//    NetworkMonitor dependent:
//      AdminTelnetServer
//      MqttClient

//  Overall network core of the system.
//
//  Implements state machine for:
//      1) Establishing WIFI client membership
//      2) Creating and maintinence of client and server objects that will track the available of the network
//          2.1) Each of these objects will receive UP/DOWN callbacks
//          2.2) Each will be an ArduinoTask derivation and will thus be setup() (once) and given time for their 
//               state machine via loop()

class NetworkService;
class NetworkClient;

class NetworkTask : public ArduinoTask
{
public:
    NetworkTask() {}
    ~NetworkTask() { $FailFast(); }

    void Begin(const char* SSID, const char* NetPassword);

    virtual void setup();
    virtual void loop();

private:
    static String& AppendHEX(uint8_t Byte, String& AddTo);
    static String MacToString(uint8_t* Mac);

    friend class NetworkService;
    friend class NetworkClient;

    void RegisterService(shared_ptr<NetworkService> ToRegister);
    void RegisterClient(shared_ptr<NetworkClient> ToRegister);

    void UnregisterService(shared_ptr<NetworkService> ToRegister);
    void UnregisterClient(shared_ptr<NetworkClient> ToRegister);

private:
    const char*     _ssid;
    const char*     _networkPassword;

    vector<shared_ptr<NetworkService>>    _registeredServices;
    vector<shared_ptr<NetworkClient>>     _registeredClients;
};


class NetworkService : public ArduinoTask, public std::enable_shared_from_this<NetworkService>
{
public:
    class Client : public ArduinoTask, public std::enable_shared_from_this<NetworkService::Client>
    {
    public:
        Client() = delete;
        Client(shared_ptr<NetworkService> Parent, WiFiClient Client);
        ~Client();

        virtual void Begin() = 0;
        virtual void setup() = 0;
        virtual void loop() = 0;

        inline WiFiClient& GetClient() { return _client; }

    protected:
        WiFiClient                  _client;
        shared_ptr<NetworkService>  _parentService;
    };

public:
    NetworkService();
    ~NetworkService();

    void Begin(int Port);
    void End();
    virtual void setup();
    virtual void loop();

protected:
    virtual shared_ptr<Client> CreateClient(shared_ptr<NetworkService> Me, WiFiClient ClientToUse) = 0;

protected:
    WiFiServer      _server;

// private:
public:
    enum class State
    {
        StartServer,
        Connected,
    };

    bool            _firstTime;
    State           _state;
    Timer           _delayTimer;
    int             _status;
    int             _tcpPort;
    vector<shared_ptr<Client>> _registeredClients;
};

NetworkService::Client::Client(shared_ptr<NetworkService> Parent, WiFiClient Client)
    :   _client(Client),
        _parentService(Parent)
{
}

NetworkService::Client::~Client()
{
}

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

class DebugWiFiClient : public WiFiClient
{
public:
    int GetSock()
    {
        //getSocket();
        return _sock;
    }
};

void NetworkService::loop()
{
    if (_registeredClients.size() > 0)
    {
        vector<shared_ptr<Client>> clients = _registeredClients;
        for (shared_ptr<Client>& client : clients)
        {
            client->loop();
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
                        return;
                    }
                }

                logger.Printf(Logger::RecType::Info, "NetworkService: Client connected: Socket: %d", ((DebugWiFiClient*)&client)->GetSock());
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

class NetworkClient : public ArduinoTask
{
public:
    NetworkClient() = delete;
    NetworkClient(WiFiClient ClientToUse, shared_ptr<NetworkService> Service);
    ~NetworkClient();
    virtual void setup();
    virtual void loop();

private:
    WiFiClient _client;
    shared_ptr<NetworkService> _service;
};

NetworkClient::NetworkClient(WiFiClient ClientToUse, shared_ptr<NetworkService> Service)
{
    _client = ClientToUse;
    _service = Service;
}

NetworkClient::~NetworkClient()
{
    $FailFast();
}

void NetworkClient::setup()
{
}

void NetworkClient::loop()
{
}

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


NetworkTask     network;


class TelnetServer : public NetworkService
{
private:
    class Client : public NetworkService::Client
    {
    public:
        Client() = delete;
        Client(shared_ptr<NetworkService> Parent, WiFiClient Client);
        ~Client();
        virtual void Begin();
        virtual void setup();
        virtual void loop();
    };

public:
    TelnetServer();
    ~TelnetServer();
    virtual shared_ptr<NetworkService::Client> CreateClient(shared_ptr<NetworkService> Parent, WiFiClient ClientToUse);
};

TelnetServer::TelnetServer()
{
}

TelnetServer::~TelnetServer()
{
}

shared_ptr<NetworkService::Client> TelnetServer::CreateClient(shared_ptr<NetworkService> Parent, WiFiClient ClientToUse)
{
    shared_ptr<TelnetServer::Client> newClientObj;
    newClientObj = make_shared<TelnetServer::Client>(Parent, ClientToUse);
    $Assert(newClientObj != nullptr);
    return newClientObj;
}

TelnetServer::Client::Client(shared_ptr<NetworkService> Parent, WiFiClient Client)
    : NetworkService::Client(Parent, Client)
{
}

TelnetServer::Client::~Client()
{
    $FailFast();
}

void TelnetServer::Client::Begin()
{
    Serial.println("TelnetServer::Client::Begin");
}

void TelnetServer::Client::setup()
{
    Serial.println("TelnetServer::Client::setup");
}

void TelnetServer::Client::loop()
{
    if (_client.connected())
    {
        while (_client.available() > 0)
        {
            Serial.print((char)(_client.read()));
            // Serial.print(" ");
        }
    }
    else
    {
        // Serial.println("Client not connected");
        _client.stop();
    }
}



shared_ptr<TelnetServer>      telnetServer;

void StartTelnet()
{
    Serial.print("TELNET starting");
    telnetServer = make_shared<TelnetServer>();
    $Assert(telnetServer != nullptr);
    telnetServer->setup();
    Serial.print("TELNET starting: after setup()");
    telnetServer->Begin(23);
    Serial.print("TELNET started");
}

CmdLine::Status StartTelnetProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    StartTelnet();
    return CmdLine::Status::Ok;
}

CmdLine::Status ShowTelnetInfoProcessor(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    printf(Serial, "There are %u clients\n", telnetServer->_registeredClients.size());
    return CmdLine::Status::Ok;
}











void setup()
{
    matrixTask.setup();
    matrixTask.PutString("S00");

    Serial.begin(9600);
    delay(1000);

    matrixTask.PutString("S01");
    bootRecord.Begin();
    if (!bootRecord.IsValid())
    {
        Serial.println("bootRecord NOT valid - auto creating");
        // Do auto setup and write
        bootRecord.GetRecord().BootCount = 0;
        bootRecord.Write();
        bootRecord.Begin();
        $Assert(bootRecord.IsValid());
    }
    else
    {
        bootRecord.GetRecord().BootCount++;
        bootRecord.Write();
        bootRecord.Begin();
        $Assert(bootRecord.IsValid());
    }

    RTC.begin();
    logger.Begin(bootRecord.GetRecord().BootCount);

    matrixTask.PutString("S02");
    consoleTask.setup();
    matrixTask.PutString("S03");
    wifiJoinApTask.setup();
    matrixTask.PutString("S04");
    network.setup();
    matrixTask.PutString("S05");
}

void loop() 
{
    consoleTask.loop();
    matrixTask.loop();
    wifiJoinApTask.loop();

    // Network dependent Tasks will get started and given time only after we kow we have a valid
    // wifi config
    if (wifiJoinApTask.IsCompleted())
    {
        static bool firstTime = true;

        $Assert(wifiJoinApTask.IsConfigured());

        if (firstTime)
        {
            // First time we've had the wifi config completed - let wifi dependent Tasks set up
            firstTime = false;

            const char* ssid;
            const char* password;

            wifiJoinApTask.GetNetworkConfig(ssid, password);
            network.Begin(ssid, password);

            //StartTelnet();
        }

        // Give each wifi dependent Task time
        network.loop();
    }
}
