// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// WiFiJoinApTask implementation

#include "SpaHeaterCntl.h"

WiFiJoinApTask::WiFiJoinApTask(Stream& TraceOutput)
    :   _traceOutput(TraceOutput),
        _config(PS_NetworkConfigBase),
        _server(80),
        _client(-1)
{
}

WiFiJoinApTask::~WiFiJoinApTask() 
{ 
    $FailFast(); 
}

void WiFiJoinApTask::GetNetworkConfig(String& SSID, String& Password)
{
    $Assert(_config.IsValid());
    SSID = _config.GetRecord()._ssid;
    Password = _config.GetRecord()._networkPassword;
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

int led =  LED_BUILTIN;

void WiFiJoinApTask::setup()
{
    _traceOutput.print("WiFiJoinApTask is active - ");
    if (_config.IsValid())
    {
        _traceOutput.println("Config is valid");
    }
    else
    {
        _traceOutput.println("Config is invalid");
    }

    // check for the WiFi module
    if (WiFi.status() == WL_NO_MODULE) 
    {
      _traceOutput.println("Communication with WiFi module failed!");
      _traceOutput.flush();
      $FailFast();
    }

    String fv = WiFi.firmwareVersion();
    if (fv < WIFI_FIRMWARE_LATEST_VERSION) 
    {
        _traceOutput.println("Please upgrade the firmware");
    }

    pinMode(led, OUTPUT);      // set the LED pin mode
}


void printWiFiStatus(Stream& ToStream) 
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
String getValueByKey(String data, String key) {
    String value = "";
    int startPos = data.indexOf(key + "=");
    if (startPos != -1) {
        int endPos = data.indexOf('&', startPos);
        if (endPos == -1) {
            endPos = data.length();
        }
        value = data.substring(startPos + key.length() + 1, endPos);
    }
    return value;
}

// Function to parse the POST data
void parsePostData(String postData, String &network, String &wifiPassword, String &telnetAdminPassword) {
    network = getValueByKey(postData, "SSID");
    wifiPassword = getValueByKey(postData, "wifiPassword");
    telnetAdminPassword = getValueByKey(postData, "telnetAdminPassword");
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
        CloseClientConnection,
        FreeServer,
    };

    static bool     firstTime = true;
    static State    state;

    if (firstTime)
    {
        firstTime = false;
        state = State::WatchConfig;
    }

    switch (state)
    {
        case State::WatchConfig:
        {
            matrixTask.PutString("N00");
            if (_config.IsValid() == false)
            {
                state = State::FormAP;
            }
        }
        break;

        static Timer      delayTimer;
        static uint8_t    status = WL_IDLE_STATUS;

        case State::FormAP:
        {
            matrixTask.PutString("N01");
            WiFi.config(IPAddress(192,48,56,2));

            //_traceOutput.print("Creating access point named: ");
            //_traceOutput.println("SpaHeaterAP");
            
            status = WiFi.beginAP("SpaHeaterAP", "123456789");
            if (status != WL_AP_LISTENING) 
            {
                printf(_traceOutput, "Creating access point failed: %i\n\r", status);
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

            printWiFiStatus(_traceOutput);

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
                    _traceOutput.println("Device connected to AP");
                    matrixTask.PutString("N04");
                } 
                else 
                {
                    // a device has disconnected from the AP, and we are back in listening mode
                    _traceOutput.println("Device disconnected from AP");
                    matrixTask.PutString("N05");
                }
            }

            _client = _server.available();
            if (_client)
            {
                _traceOutput.println("new client");
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
            {                                        // if there's bytes to read from the client,
                char c = _client.read();            // read a byte, then
                _traceOutput.write(c);              // print it out to the serial monitor
                if (c == '\n') 
                {                                   // if the byte is a newline character
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
                        static char page[] = 
                            "<!DOCTYPE html>"
                            "<html>"
                            "<head>"
                            "    <title>Wi-Fi Configuration</title>"
                            "</head>"
                            "<body>"
                            "    <h1>Wi-Fi Configuration</h1> <!-- Visible Title Block -->"
                            "    <h2>Wi-Fi Setup</h2>"
                            "    <form action=\"/submit\" method=\"post\">"
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

                        _client.print(page);
                        //_client.print("<p style=\"font-size:7vw;\">Click <a href=\"/H\">here</a> turn the LED on<br></p>");
                        //_client.print("<p style=\"font-size:7vw;\">Click <a href=\"/L\">here</a> turn the LED off<br></p>");

                        // The HTTP response ends with another blank line:
                        _client.println();

                        // break out of the while loop:
                        _client.stop();
                        _traceOutput.println("client disconnected");

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
                _traceOutput.write(c);              // print it out to the serial monitor

            
                if (c == '\n') 
                {                                   // if the byte is a newline character
                    // if the current line is blank, you got two newline characters in a row.
                    // that's the end of the client HTTP request, so get the submitted form data:
                    if (_currentLine.length() == 0) 
                    {
                        _traceOutput.println("**BLANK LINE**");
                        state = State::ProcessFormData;
                        return;
                    }
                    else 
                    { 
                        // Check for content length header
                        if (_currentLine.startsWith("Content-Length: "))
                        {
                            contentLength = _currentLine.substring(16).toInt();
                            printf(_traceOutput, "Have content length of: %i\n", contentLength);
                        }
                          // if you got a newline, then clear currentLine:
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

        case State::ProcessFormData:
        {
            matrixTask.PutString("N09");
            if (!_client.connected())
            {
                state = State::CloseClientConnection;
                return;
            }

            if (contentLength == 0)
            {
                String  netName;
                String  netPw;
                String  adminPw;

                parsePostData(_currentLine, netName, netPw, adminPw);
                printf(_traceOutput, "\nPosted Config data: SSID: '%s'; password: '%s'; admin pw: '%s'\n",
                      netName.c_str(), netPw.c_str(), adminPw.c_str());


                // We have our config data and are ready to write valid config 
                //_config.GetRecord()

                state = State::CloseClientConnection;
                return;
            }

            delayMicroseconds(10);
            if (_client.available()) 
            {                                        // if there's bytes to read from the client,
                char c = _client.read();            // read a byte, then
                _traceOutput.write(c);              // print it out to the serial monitor
                _currentLine += c;
                contentLength--;
            }
        }
        break;

        case State::CloseClientConnection:
        {
            matrixTask.PutString("N10");
            state = State::WatchForClient;
            _client.stop();
            _traceOutput.println("client disconnected");
        }
        break;

        case State::FreeServer:
        {
            _server.end();
            state = State::WatchConfig;
        }
        break;

        default:
        {
            $FailFast();
        }
        break;
    }
}

