// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// Network related definitions

#pragma once
#include "SpaHeaterCntl.hpp"
#include "Network.hpp"

//** Network related definitions
//* Main Network Task - maintains WiFi connection
class NetworkTask final : public ArduinoTask
{
public:
    NetworkTask() : _isAvailable(false) {}
    ~NetworkTask() { $FailFast(); }

    void Begin();

    virtual void setup() override;
    virtual void loop() override;
    bool IsAvailable() { return _isAvailable; }

    static shared_ptr<Client> CreateClient();
    static shared_ptr<Server> CreateServer(int Port);
    static shared_ptr<UDP> CreateUDP();

    static shared_ptr<Client> available(shared_ptr<Server> Server);
    static void begin(shared_ptr<Server> Server);
    static void end(shared_ptr<Server> Server);

private: 
    static String &AppendHEX(uint8_t Byte, String &AddTo);
    static String MacToString(uint8_t *Mac);

private:
    const char *_ssid;
    const char *_networkPassword;
    bool _isAvailable;
};


//** Cross module references
extern class NetworkTask network;
