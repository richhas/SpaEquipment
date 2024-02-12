// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// Network related definitions

#pragma once
#include "SpaHeaterCntl.hpp"
#include "Network.hpp"

//** Network related definitions
class NetworkService;
class NetworkClient;

//* Main Network Task - maintains WiFi connection and hosts outgoing Client and incoming Services
class NetworkTask final : public ArduinoTask
{
public:
    NetworkTask() {}
    ~NetworkTask() { $FailFast(); }

    void Begin(const char *SSID, const char *NetPassword);

    virtual void setup() override;
    virtual void loop() override;

private:
    static String &AppendHEX(uint8_t Byte, String &AddTo);
    static String MacToString(uint8_t *Mac);

    friend class NetworkService;
    friend class NetworkClient;

    void RegisterService(shared_ptr<NetworkService> ToRegister);
    void RegisterClient(shared_ptr<NetworkClient> ToRegister);

    void UnregisterService(shared_ptr<NetworkService> ToRegister);
    void UnregisterClient(shared_ptr<NetworkClient> ToRegister);

private:
    const char *_ssid;
    const char *_networkPassword;

    vector<shared_ptr<NetworkService>> _registeredServices;
    vector<shared_ptr<NetworkClient>> _registeredClients;
};

//* Base class for all incoming hosted services
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
        virtual void End() = 0;
        virtual void setup() override final { OnSetup(); }
        virtual void loop() override final { OnLoop(); }

        inline WiFiClient &GetClient() { return _client; }

    protected:
        virtual void OnSetup(){};
        virtual void OnLoop(){};

    protected:
        WiFiClient _client;
        shared_ptr<NetworkService> _parentService;
    };

public:
    NetworkService();
    ~NetworkService();

    void Begin(int Port);
    void End();
    virtual void setup() override final;
    virtual void loop() override final;
    inline int GetNumberOfClients() { return _registeredClients.size(); }

protected:
    virtual shared_ptr<Client> CreateClient(shared_ptr<NetworkService> Me, WiFiClient ClientToUse) = 0;
    virtual void OnSetup() {}
    virtual void OnLoop() {}

protected:
    WiFiServer _server;

private:
    enum class State
    {
        StartServer,
        Connected,
    };

    bool _firstTime;
    State _state;
    Timer _delayTimer;
    int _status;
    int _tcpPort;
    vector<shared_ptr<Client>> _registeredClients;
};

//* Outgoing client session base class implementation
//      - derived classes must implement the OnConnected/OnDisconnected/OnLoop/OnSetup methods
class NetworkClient : public ArduinoTask, public std::enable_shared_from_this<NetworkClient>
{
public:
    NetworkClient();
    ~NetworkClient();
    virtual void setup() override final;
    virtual void loop() override final;

    void Begin();
    void End();

protected:
    // Optional overrides for derived classes
    virtual void OnNetConnected() = 0;
    virtual void OnNetDisconnected() = 0;
    virtual void OnLoop() = 0;
    virtual void OnSetup() = 0;

private:
    enum class State
    {
        WaitForWiFi,
        DelayForWiFi,
        Connected,
    };

    bool _firstTime;
    State _state;
    Timer _delayTimer;
    int _status;
};

//* Outgoing TCP client session - maintains a single connection to a remote server
class TcpClient : public NetworkClient
{
public:
    TcpClient() = delete;
    TcpClient(IPAddress ServerIP, int ServerPort);
    ~TcpClient();

protected:
    virtual void OnNetConnected() override final;
    virtual void OnNetDisconnected() override final;
    virtual void OnLoop() override final;
    virtual void OnSetup() override final;

protected:
    virtual void OnConnected();
    virtual void OnDisconnected();
    virtual void OnDoProcess();

protected:
    WiFiClient _client;

private:
    IPAddress _serverIP;
    int _serverPort;
    bool _netIsUp;

    enum class State
    {
        WaitForNetUp,
        DelayBeforeConnectAttempt,
        Connected,
    };

    bool _firstTime;
    State _state;
    Timer _reconnectTimer;
};

class UdpClient : public NetworkClient
{
public:
    UdpClient();
    ~UdpClient();
};

//* Specific TELNET service bound to instances of ConsoleTask per session
class TelnetServer final : public NetworkService
{
private:
    class Client : public NetworkService::Client
    {
    public:
        Client() = delete;
        Client(shared_ptr<NetworkService> Parent, WiFiClient Client);
        ~Client();
        virtual void Begin();
        virtual void End();

    protected:
        virtual void OnSetup() override;
        virtual void OnLoop() override;

    private:
        shared_ptr<ConsoleTask> _console;
    };

public:
    TelnetServer();
    ~TelnetServer();
    virtual shared_ptr<NetworkService::Client> CreateClient(shared_ptr<NetworkService> Parent, WiFiClient ClientToUse);
};

//** Cross module references
extern class NetworkTask network;
