// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// WiFiJoinApTask implementation

#include <WiFiS3.h>
#include "SpaHeaterCntl.h"

WiFiJoinApTask::WiFiJoinApTask(Stream& TraceOutput)
    :   _traceOutput(TraceOutput),
        _config(PS_NetworkConfigBase)
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
        static uint8_t    status;

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

        static WiFiServer server;

        case State::WaitForApToForm:
        {
            matrixTask.PutString("N02");
            if (delayTimer.IsAlarmed() == false)
            {
                return;
            }

            // start the web server on port 80
            server.begin(80);

            printWiFiStatus(_traceOutput);

            matrixTask.PutString("N03");
            state = State::WatchForClient;
        }
        break;

        static WiFiClient client;
        static String     currentLine;

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

            client = server.available();
            if (client)
            {
                currentLine = "";
                state = State::ClientConnected;
            }
        }
        break;

        case State::ClientConnected:
        {
            matrixTask.PutString("N06");
            if (!client.connected())
            {
                state = State::CloseClientConnection;
                return;
            }

            delayMicroseconds(10);
            if (client.available()) 
            {                                         // if there's bytes to read from the client,
                char c = client.read();             // read a byte, then
                _traceOutput.write(c);              // print it out to the serial monitor
                if (c == '\n') 
                {                                   // if the byte is a newline character
                    // if the current line is blank, you got two newline characters in a row.
                    // that's the end of the client HTTP request, so send a response:
                    if (currentLine.length() == 0) 
                    {
                        // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                        // and a content-type so the client knows what's coming, then a blank line:
                        client.println("HTTP/1.1 200 OK");
                        client.println("Content-type:text/html");
                        client.println();

                        // the content of the HTTP response follows the header:
                        client.print("<p style=\"font-size:7vw;\">Click <a href=\"/H\">here</a> turn the LED on<br></p>");
                        client.print("<p style=\"font-size:7vw;\">Click <a href=\"/L\">here</a> turn the LED off<br></p>");

                        // The HTTP response ends with another blank line:
                        client.println();
                        // break out of the while loop:
                        state = State::CloseClientConnection;
                        return;
                    }
                    else 
                    {   // if you got a newline, then clear currentLine:
                        currentLine = "";
                    }
                }
                else if (c != '\r') 
                {             
                    // if you got anything else but a carriage return character,
                    currentLine += c;      // add it to the end of the currentLine
                }
                // Check to see if the client request was "GET /H" or "GET /L":
                if (currentLine.endsWith("GET /H")) 
                {
                    digitalWrite(led, HIGH);               // GET /H turns the LED on
                }
                if (currentLine.endsWith("GET /L")) 
                {
                    digitalWrite(led, LOW);                // GET /L turns the LED off
                }
            }
        }
        break;

        case State::CloseClientConnection:
        {
            matrixTask.PutString("N07");
            matrixTask.PutString("N03");
            state = State::WatchForClient;
            client.stop();
        }
        break;

        case State::FreeServer:
        {
            server.end();
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

