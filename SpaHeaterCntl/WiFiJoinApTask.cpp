// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// WiFiJoinApTask implementation

#include "SpaHeaterCntl.hpp"
#include <memory.h>
#include <string.h>
#include "WiFiJoinApTask.hpp"

WiFiJoinApTask wifiJoinApTask("SpaHeaterAP", "123456789");


WiFiJoinApTask::WiFiJoinApTask(const char* ApNetName, const char* ApNetPassword)
    :   _config(),
        _server(80),
        _client(-1),
        _isInSleepState(false),
        _apNetName(ApNetName),
        _apNetPassword(ApNetPassword)
{
}

WiFiJoinApTask::~WiFiJoinApTask() 
{ 
    $FailFast(); 
}

void WiFiJoinApTask::GetNetworkConfig(const char *&SSID, const char *&Password)
{
    $Assert(_config.IsValid());
    SSID = &_config.GetRecord()._ssid[0];
    Password = &_config.GetRecord()._networkPassword[0];
}

void WiFiJoinApTask::EraseConfig()
{
    _config.Erase();
    $Assert(_config.IsValid() == false);
}

void WiFiJoinApTask::DumpConfig(Stream& ToStream)
{
    if (_config.IsValid() == false)
    {
        ToStream.println("WiFi config is NOT valid");
        return;
    }

    Config&   config = _config.GetRecord();
    printf(ToStream, "Network Config: SSID: '%s'; Password: '%s'; Admin Password: '%s'", 
                     config._ssid, config._networkPassword, config._adminPassword);
}

void WiFiJoinApTask::SetConfig(const char* SSID, const char* NetPassword, const char* AdminPassword)
{
    strcpy(_config.GetRecord()._ssid, SSID);
    strcpy(_config.GetRecord()._networkPassword, NetPassword);
    strcpy(_config.GetRecord()._adminPassword, AdminPassword);

    _config.Write();
    $Assert(_config.IsValid());
}

void WiFiJoinApTask::setup()
{
    _config.Begin();
    logger.Printf(Logger::RecType::Progress, "WiFiJoinApTask: Active - Config is %s", (_config.IsValid() ? "valid" : "invalid"));

    // check for the WiFi module
    if (WiFi.status() == WL_NO_MODULE) 
    {
        logger.Printf(Logger::RecType::Critical, "WiFiJoinApTask: Communication with WiFi module failed!");
        $FailFast();
    }

    // warn if FW out of date
    String fv = WiFi.firmwareVersion();
    if (fv < WIFI_FIRMWARE_LATEST_VERSION) 
    {
        logger.Printf(Logger::RecType::Warning, "WiFiJoinApTask: Please upgrade the firmware");
    }
}

void WiFiJoinApTask::loop() 
{
    // Statemachine used when config is invalid: creates an AP and a webserver to accept a user's network config. And then
    // writes that valid config

    enum class State
    {
        WatchConfig,
        FormAP,
        WaitForApToForm,
        WatchForClient,
        ClientConnected,
        EatPostHeader,
        ProcessFormData,
        StartNetConnection,
        NetConnected,
        CloseClientConnection,
        Sleep
    };

    static bool     firstTime = true;
    static State    state;
    static Timer    delayTimer;
    static char*    lastError;

    if (firstTime)
    {
        firstTime = false;
        lastError = nullptr;
        matrixTask.PutString("N00");
        state = State::WatchConfig;
    }

    switch (state)
    {
        //* Loop while config is valid; else turn into an access point and allow a one-time SSID/Password config
        case State::WatchConfig:
        {
            if (_config.IsValid() == false)
            {
                state = State::FormAP;
                return;
            }

            state = State::Sleep;
        }
        break;

        static uint8_t    status = WL_IDLE_STATUS;

        case State::FormAP:
        {
            matrixTask.PutString("N01");
            WiFi.config(IPAddress(192,48,56,2));

            status = WiFi.beginAP(_apNetName.c_str(), _apNetPassword.c_str());
            if (status != WL_AP_LISTENING) 
            {
                logger.Printf(Logger::RecType::Warning, "WiFiJoinApTask: Creating access point failed: %i\n\r", status);
                state = State::WatchConfig;
                return;
            }

            delayTimer.SetAlarm(10000);     // allow 10secs for connection

            state = State::WaitForApToForm;
        }
        break;

        case State::WaitForApToForm:
        {
            matrixTask.PutString("N02");
            if (delayTimer.IsAlarmed() == false)
            {
                return;
            }

            // start the web server - port 80
            _server.begin();

            PrintWiFiStatus(Serial);

            matrixTask.PutString("N03");
            state = State::WatchForClient;
        }
        break;

        case State::WatchForClient:
        {
            if (status != WiFi.status()) 
            {
                // it has changed update the variable
                status = WiFi.status();

                if (status == WL_AP_CONNECTED) 
                {
                    // a device has connected to the AP
                    logger.Printf(Logger::RecType::Progress, "WiFiJoinApTask: Device connected to AP");
                    matrixTask.PutString("N04");
                } 
                else 
                {
                    // a device has disconnected from the AP, and we are back in listening mode
                    logger.Printf(Logger::RecType::Progress, "WiFiJoinApTask: Device disconnected from AP");
                    matrixTask.PutString("N05");
                }
            }

            _client = _server.available();
            if (_client)
            {
                _currentLine = "";
                state = State::ClientConnected;
            }
        }
        break;

        case State::ClientConnected:
        {
            matrixTask.PutString("N06");
            if (!_client.connected())
            {
                state = State::CloseClientConnection;
                return;
            }

            delayMicroseconds(10);
            if (_client.available()) 
            {                                       // if there's bytes to read from the client,
                char c = _client.read();            // read a byte, then
                if (c == '\n') 
                {                                   
                    // EOL...
                    // if the current line is blank, you got two newline characters in a row.
                    // that's the end of the client HTTP request, so send a response:
                    if (_currentLine.length() == 0) 
                    {
                        // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                        // and a content-type so the client knows what's coming, then a blank line:
                        _client.println("HTTP/1.1 200 OK");
                        _client.println("Content-type:text/html");
                        _client.println();

                        // the content of the HTTP response follows the header:
                        static char pageStart[] = 
                            "<!DOCTYPE html>"
                            "<html>"
                            "<head>"
                            "    <title>Wi-Fi Configuration</title>"
                            "</head>"
                            "<body>"
                            "    <h1>Wi-Fi Configuration</h1> <!-- Visible Title Block -->"
                            "    <h2>Wi-Fi Setup</h2>"
                            "    <form action=\"/submit\" method=\"post\">";

                        static char pageEnd[] =
                            "        <br><br>"
                            "        <label for=\"SSID\">SSID:</label>"
                            "        <input type=\"SSID\" id=\"SSID\" name=\"SSID\">"
                            "        <label for=\"wifiPassword\">Wi-Fi Password:</label>"
                            "        <input type=\"password\" id=\"wifiPassword\" name=\"wifiPassword\">"
                            "        <br><br>"
                            "        <label for=\"telnetAdminPassword\">Telnet Administrator Password:</label>"
                            "        <input type=\"password\" id=\"telnetAdminPassword\" name=\"telnetAdminPassword\">"
                            "        <br><br>"
                            "        <input type=\"submit\" value=\"Submit\">"
                            "    </form>"
                            "</body>"
                            "</html>";                          

                        _client.print(pageStart);
                        if (lastError != nullptr)
                        {
                            _client.print("<br><br> Failed connection attempt: ");
                            _client.print(lastError);
                        }
                        _client.print(pageEnd);

                        // The HTTP response ends with another blank line:
                        _client.println();

                        // break out of the while loop:
                        _client.stop();

                        state = State::WatchForClient;
                        matrixTask.PutString("N03");
                        return;
                    }
                    else 
                    {   // if you got a newline, then clear currentLine:
                        _currentLine = "";
                    }
                }
                else if (c != '\r') 
                {             
                    // if you got anything else but a carriage return character,
                    _currentLine += c;      // add it to the end of the currentLine
                }

                if (_currentLine.startsWith("POST /submit"))
                {
                    // have posted response
                    state = State::EatPostHeader;
                    _currentLine = "";
                    return;
                }
            }
        }
        break;

        static int    contentLength;

        case State::EatPostHeader:
        {
            matrixTask.PutString("N08");
            if (!_client.connected())
            {
                state = State::CloseClientConnection;
                return;
            }

            delayMicroseconds(10);
            if (_client.available()) 
            {                                        // if there's bytes to read from the client,
                char c = _client.read();            // read a byte, then
                if (c == '\n') 
                {                                   
                    // EOL...
                    // if the current line is blank, you got two newline characters in a row.
                    // that's the end of the client HTTP response (POST), so get the submitted form data:
                    if (_currentLine.length() == 0) 
                    {
                        state = State::ProcessFormData;
                        return;
                    }
                    else 
                    { 
                        // Check for content length header and remember that value if found
                        if (_currentLine.startsWith("Content-Length: "))
                        {
                            contentLength = _currentLine.substring(16).toInt();
                        }
                          // if you got a newline, then clear currentLine. Keep eating lines until a blank line
                        _currentLine = "";
                    }
                }
                else if (c != '\r') 
                {             
                    // if you got anything else but a carriage return character,
                    _currentLine += c;      // add it to the end of the currentLine
                }
            }
        }
        break;

        static String savedSSID;
        static String savedNetPw;
        static String savedAdminPw;

        case State::ProcessFormData:
        {
            // contentLength has the expected number of charcters on the POSTed response line - next incoming
            matrixTask.PutString("N09");
            if (!_client.connected())
            {
                state = State::CloseClientConnection;
                return;
            }

            if (contentLength == 0)
            {
                // Have the full payload in _currentLine
                ParsePostData(_currentLine, savedSSID, savedNetPw, savedAdminPw);
                logger.Printf(Logger::RecType::Info, "WiFiJoinApTask: Posted Config data: SSID: '%s'; password: '%s'; admin pw: '%s'\n",
                              savedSSID.c_str(),
                              savedNetPw.c_str(),
                              savedAdminPw.c_str());

                // We have our config data and are ready to write a valid config but first prove we can connect 
                // to the configured network
                status = WL_IDLE_STATUS;
                state = State::StartNetConnection;
                return;
            }

            // accumlate _currentLine (posted response)
            delayMicroseconds(10);
            if (_client.available()) 
            {                                        // if there's bytes to read from the client,
                char c = _client.read();            // read a byte, then
                _currentLine += c;
                contentLength--;
            }
        }
        break;

        case State::StartNetConnection:
        {
            // Test that the supplied network is available and can be connected to given the supplied info
            matrixTask.PutString("N11");
            logger.Printf(Logger::RecType::Info, "WiFiJoinApTask: Attempting to connect to: '%s'", savedSSID.c_str());
            status = WiFi.begin(savedSSID.c_str(), savedNetPw.c_str());
            if (status == WL_CONNECTED)
            {
                state = State::NetConnected;
                return;
            }

            logger.Printf(Logger::RecType::Info, "WiFiJoinApTask: Attempt to connect to: '%s' failed! - try again", savedSSID.c_str());
            lastError = "*** WiFi.begin() failed ***";
            _client.stop();
            _server.end();
            state = State::FormAP;        // cause SM to start over if can't attach to net
        }
        break;

        case State::NetConnected:
        {
            // Supplied WiFi config info proved to work - store the config and restart SM at first state
            matrixTask.PutString("N13");
            logger.Printf(Logger::RecType::Info, "Connected to '%s'", savedSSID.c_str());

            // If is left up to other components to use the validated wifi config info - detach from the network
            _client.stop();
            _server.end();

            // Write our config
            memset(&_config.GetRecord(), 0, sizeof(Config));
            _config.GetRecord()._version = Config::CurrentVersion;
            strcpy(_config.GetRecord()._ssid, savedSSID.c_str());
            strcpy(_config.GetRecord()._networkPassword, savedNetPw.c_str());
            strcpy(_config.GetRecord()._adminPassword, savedAdminPw.c_str());

            _config.Write();
            $Assert(_config.IsValid());

            // Free up some heap
            savedSSID = "";
            savedNetPw = "";
            savedAdminPw = "";
            _currentLine = "";

            state = State::Sleep;       // Go to final state
        }
        break;

        case State::CloseClientConnection:
        {
            matrixTask.PutString("N14");
            _client.stop();
            logger.Printf(Logger::RecType::Info, "WiFiJoinApTask: client disconnected");
            state = State::WatchForClient;
        }
        break;

        case State::Sleep:
        {
            if (!_isInSleepState)
            {
                _isInSleepState = true;
                matrixTask.PutString("");
            }
        }
        break;

        default:
        {
            $FailFast();
        }
        break;
    }
}

void WiFiJoinApTask::PrintWiFiStatus(Stream& ToStream) 
{
    // print the SSID of the network you're attached to:
    ToStream.print("SSID: ");
    ToStream.println(WiFi.SSID());

    // print your WiFi shield's IP address:
    IPAddress ip = WiFi.localIP();
    ToStream.print("IP Address: ");
    ToStream.println(ip);

    // print where to go in a browser:
    ToStream.print("To see this page in action, open a browser to http://");
    ToStream.println(ip);
}

// Helper function to extract value for a given key
String WiFiJoinApTask::GetValueByKey(String& Data, String Key) 
{
    String value = "";
    int startPos = Data.indexOf(Key + "=");
    if (startPos != -1) {
        int endPos = Data.indexOf('&', startPos);
        if (endPos == -1) {
            endPos = Data.length();
        }
        value = Data.substring(startPos + Key.length() + 1, endPos);
    }
    return value;
}

// Function to parse the POST data
void WiFiJoinApTask::ParsePostData(String& PostData, String& Network, String& WifiPassword, String& TelnetAdminPassword) 
{
    Network = GetValueByKey(PostData, "SSID");
    WifiPassword = GetValueByKey(PostData, "wifiPassword");
    TelnetAdminPassword = GetValueByKey(PostData, "telnetAdminPassword");
}

