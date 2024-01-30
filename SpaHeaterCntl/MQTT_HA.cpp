// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// HA_MQTT class implementation

#include "SpaHeaterCntl.h"

class PrintOutputCounter : public Print
{
private:
    int _count;

public:
    PrintOutputCounter() : _count(0) {}
    virtual size_t write(uint8_t c) override final { _count++; return 1; }
    virtual size_t write(const uint8_t *buffer, size_t size) override final { _count += size; return size; }
    int GetCount() const { return _count; }
    void Reset() { _count = 0; }
};

class BufferPrinter : public Print
{
private:
    char *_buffer;
    int _size;
    int _maxSize;

public:
    BufferPrinter(char *buffer, int maxSize) : _buffer(buffer), _size(0), _maxSize(maxSize) {}

    virtual size_t write(uint8_t c) override final;
    virtual size_t write(const uint8_t *buffer, size_t size) override final;

    const char *GetBuffer() const { return _buffer; }
    int GetSize() const { return _size; }
};

size_t BufferPrinter::write(uint8_t c)
{
    if (_size < _maxSize)
    {
        _buffer[_size++] = (char)c;
        _buffer[_size] = 0;
        return 1;
    }
    else
    {
        _buffer[_maxSize] = 0;
    }
    return 0;
}

size_t BufferPrinter::write(const uint8_t *buffer, size_t size)
{
    size_t bytesToCopy = std::min(size, static_cast<size_t>(_maxSize - _size));
    if (bytesToCopy > 0)
    {
        memcpy(&_buffer[_size], buffer, bytesToCopy);
        _size += bytesToCopy;
    }
    _buffer[_size] = 0;
    return bytesToCopy;
}

size_t ExpandJson(Print &To, const char *JsonFormat, ...)
{
    uint32_t(*va_list)[0] = VarArgsBase(&JsonFormat);
    size_t result = 0;

    char *p = (char *)JsonFormat;
    while (*p)
    {
        if (*p == '%')
        {
            p++;

            if (isdigit(*p))
            {
                int index = *p - '0';
                if ((index <= 9) && (index >= 0))
                {
                    result += To.print((char *)((*va_list)[index]));
                }
            }
            else if (*p == '%')
            {
                result += To.print('%');
            }
            else
            {
                Serial.print("Error: Invalid format specifier: ");
                return 0;
            }
        }
        else if (*p == '\'')
        {
            result += To.print('"');
        }
        else
        {
            result += To.print(*p);
        }
        p++;
    }

    return result;
}


//* Home Assistant MQTT Namespace Names
namespace HA_MqttNames
{
    static constexpr char _defaultBaseTopic[] = "homeassistant";
    static constexpr char _haAvailTopic[] = "status";
        static constexpr char _haAvailOnline[] = "online";
        static constexpr char _haAvailOffline[] = "offline";

    static constexpr char _defaultDeviceName[] = "SpaHeater";
    static constexpr char _defaultBoilerName[] = "boiler";
    static constexpr char _defaultAmbientTempName[] = "ambientTemp";
    static constexpr char _defaultBoilerInTempName[] = "boilerInTemp";
    static constexpr char _defaultBoilerOutTempName[] = "boilerOutTemp";
}

using namespace HA_MqttNames;



static constexpr char BoilerConfigJsonTemplate[] =
    "{\n" // Parms: <base_topic>, <device-name>, <entity-name>
        "'~' : '%0/water_heater/%2',\n"
        "'name': '%2',\n"
        "'modes': [\n"
            "'off',\n"
            "'eco',\n"
            "'performance'\n"
        "],\n"

        "'avty_t' : '~/avail',\n"
        "'avty_tpl' : '{{ value_json }}',\n"
        "'mode_stat_t': '~/mode',\n"
        "'mode_stat_tpl' : '{{ value_json }}',\n"
        "'mode_cmd_t': '~/mode/set',\n"
        "'temp_stat_t': '~/temperature',\n"
        "'temp_cmd_t': '~/temperature/set',\n"
        "'curr_temp_t': '~/current_temperature',\n"
        "'power_command_topic' : '~/power/set',\n"
        "'max_temp' : '160',\n"
        "'min_temp' : '65',\n"
        "'precision': 1.0,\n"
        "'temp_unit' : 'F',\n"
        "'init': 101,\n"
        "'opt' : 'false',\n"
        "'uniq_id':'%2',\n"
        "'dev':\n"
        "{\n"
            "'identifiers' : ['01'],\n"
            "'name' : '%1'\n"
        "}\n"
    "}\n";

static constexpr char BoilerBaseTopicJsonTemplate[] = "%0/water_heater/%1";                         // Parms: <base_topic>, <entity-name>
char* boilerBaseTopic;

static constexpr char BoilerConfigTopicJsonTemplate[] = "%0/water_heater/%1/config";                // Parms: <base_topic>, <entity-name>
char *boilerConfigTopic;

static constexpr char BoilerAvailTopicJsonTemplate[] = "%0/water_heater/%1/avail"; // Parms: <base_topic>, <entity-name>
char* boilerAvailTopic;

static constexpr char BoilerModeTopicJsonTemplate[] = "%0/water_heater/%1/mode";                    // Parms: <base_topic>, <entity-name>
char* boilerModeTopic;

static constexpr char BoilerModeSetTopicJsonTemplate[] = "%0/water_heater/%1/mode/set";         // Parms: <base_topic>, <entity-name>
char *boilerModeSetTopic;

static constexpr char BoilerSetpointTopicJsonTemplate[] = "%0/water_heater/%1/temperature";      // Parms: <base_topic>, <entity-name>
char* boilerSetpointTopic;

static constexpr char BoilerSetpointSetTopicJsonTemplate[] = "%0/water_heater/%1/temperature/set"; // Parms: <base_topic>, <entity-name>
char *boilerSetpointSetTopic;

static constexpr char BoilerCurrTempTopicJsonTemplate[] = "%0/water_heater/%1/current_temperature"; // Parms: <base_topic>, <entity-name>
char* boilerCurrTempTopic;

static constexpr char BoilerPowerTopicJsonTemplate[] = "%0/water_heater/%1/power";                  // Parms: <base_topic>, <entity-name>
char* boilerPowerTopic;

static constexpr char ThermometerConfigJsonTemplate[] =
    "{\n" // Parms: <base_topic>, <device-name>, <entity-name>
        "'~' : '%0/sensor/%2',\n"
        "'name': '%2',\n"
        "'dev_cla' : 'temperature',\n"
        "'unit_of_meas' : '°F',\n"
        "'avty_t' : '~/avail',\n"
        "'avty_tpl' : '{{ value_json }}',\n"
        "'stat_t' : '~/temperature',\n"    

        "'uniq_id' : '%2',\n"
        "'dev':\n"
        "{\n"
            "'identifiers' : ['01'],\n"
            "'name' : '%1'\n"
        "}\n"
    "}\n";

static constexpr char ThermometerBaseTopicJsonTemplate[] = "%0/sensor/%1"; // Parms: <base_topic>, <entity-name>
char *ambientThermometerBaseTopic;
char *boilerInThermometerBaseTopic;
char *boilerOutThermometerBaseTopic;

static constexpr char ThermometerConfigTopicJsonTemplate[] = "%0/sensor/%1/config"; // Parms: <base_topic>, <entity-name>
char *ambientThermometerConfigTopic;
char *boilerInThermometerConfigTopic;
char *boilerOutThermometerConfigTopic;

static constexpr char ThermometerAvailTopicJsonTemplate[] = "%0/sensor/%1/avail"; // Parms: <base_topic>, <entity-name>
char* ambientThermometerAvailTopic;
char* boilerInThermometerAvailTopic;
char* boilerOutThermometerAvailTopic;

static constexpr char ThermometerTempTopicJsonTemplate[] = "%0/sensor/%1/temperature"; // Parms: <base_topic>, <entity-name>
char* ambientThermometerTempTopic;
char* boilerInThermometerTempTopic;
char* boilerOutThermometerTempTopic;

void BuildTopicString(const char& Template, char*& Result, const char* BaseTopic, const char* EntityName)
{
    PrintOutputCounter counter;
    size_t size = ExpandJson(counter, &Template, BaseTopic, EntityName);        // Get size of expanded string
    $Assert(size > 0); // Error in template

    Result = new char[size + 1];                // Leave room for null terminator
    $Assert(Result != nullptr);                 // Out of memory
    BufferPrinter printer(Result, size);

    $Assert(ExpandJson(printer, &Template, BaseTopic, EntityName) == size);     // Expand template into buffer
}

//* Boiler Config Record - In persistant storage
#pragma pack(push, 1)
struct HA_MqttConfig
{
    // MQTT Broker config
    uint32_t _brokerIP;
    uint16_t _brokerPort;

    // MQTT Client config
    char _clientId[16];
    char _username[16];
    char _password[16];

    // HA MQTT config
    char _baseHATopic[32];
    char _haDeviceName[32];
};
#pragma pack(pop)

FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase> mqttConfig;
static_assert(PS_MQTTBrokerConfigBlkSize >= sizeof(FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase>));

void BuildTopicStrings(FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase>* Config)
{
    $Assert(Config->IsValid());

    // Boiler
    BuildTopicString(BoilerBaseTopicJsonTemplate[0], boilerBaseTopic, Config->GetRecord()._baseHATopic, _defaultBoilerName);
    BuildTopicString(BoilerConfigTopicJsonTemplate[0], boilerConfigTopic, Config->GetRecord()._baseHATopic, _defaultBoilerName);
    BuildTopicString(BoilerAvailTopicJsonTemplate[0], boilerAvailTopic, Config->GetRecord()._baseHATopic, _defaultBoilerName);
    BuildTopicString(BoilerModeTopicJsonTemplate[0], boilerModeTopic, Config->GetRecord()._baseHATopic, _defaultBoilerName);
    BuildTopicString(BoilerModeSetTopicJsonTemplate[0], boilerModeSetTopic, Config->GetRecord()._baseHATopic, _defaultBoilerName);
    BuildTopicString(BoilerSetpointTopicJsonTemplate[0], boilerSetpointTopic, Config->GetRecord()._baseHATopic, _defaultBoilerName);
    BuildTopicString(BoilerSetpointSetTopicJsonTemplate[0], boilerSetpointSetTopic, Config->GetRecord()._baseHATopic, _defaultBoilerName);
    BuildTopicString(BoilerCurrTempTopicJsonTemplate[0], boilerCurrTempTopic, Config->GetRecord()._baseHATopic, _defaultBoilerName);
    BuildTopicString(BoilerPowerTopicJsonTemplate[0], boilerPowerTopic, Config->GetRecord()._baseHATopic, _defaultBoilerName);

    // Ambient Thermometer
    BuildTopicString(ThermometerBaseTopicJsonTemplate[0], ambientThermometerBaseTopic, Config->GetRecord()._baseHATopic, _defaultAmbientTempName);
    BuildTopicString(ThermometerConfigTopicJsonTemplate[0], ambientThermometerConfigTopic, Config->GetRecord()._baseHATopic, _defaultAmbientTempName);
    BuildTopicString(ThermometerAvailTopicJsonTemplate[0], ambientThermometerAvailTopic, Config->GetRecord()._baseHATopic, _defaultAmbientTempName);
    BuildTopicString(ThermometerTempTopicJsonTemplate[0], ambientThermometerTempTopic, Config->GetRecord()._baseHATopic, _defaultAmbientTempName);

    // Boiler In Thermometer
    BuildTopicString(ThermometerBaseTopicJsonTemplate[0], boilerInThermometerBaseTopic, Config->GetRecord()._baseHATopic, _defaultBoilerInTempName);
    BuildTopicString(ThermometerConfigTopicJsonTemplate[0], boilerInThermometerConfigTopic, Config->GetRecord()._baseHATopic, _defaultBoilerInTempName);
    BuildTopicString(ThermometerAvailTopicJsonTemplate[0], boilerInThermometerAvailTopic, Config->GetRecord()._baseHATopic, _defaultBoilerInTempName);
    BuildTopicString(ThermometerTempTopicJsonTemplate[0], boilerInThermometerTempTopic, Config->GetRecord()._baseHATopic, _defaultBoilerInTempName);

    // Boiler Out Thermometer
    BuildTopicString(ThermometerBaseTopicJsonTemplate[0], boilerOutThermometerBaseTopic, Config->GetRecord()._baseHATopic, _defaultBoilerOutTempName);
    BuildTopicString(ThermometerConfigTopicJsonTemplate[0], boilerOutThermometerConfigTopic, Config->GetRecord()._baseHATopic, _defaultBoilerOutTempName);
    BuildTopicString(ThermometerAvailTopicJsonTemplate[0], boilerOutThermometerAvailTopic, Config->GetRecord()._baseHATopic, _defaultBoilerOutTempName);
    BuildTopicString(ThermometerTempTopicJsonTemplate[0], boilerOutThermometerTempTopic, Config->GetRecord()._baseHATopic, _defaultBoilerOutTempName);
}

void RunWorkbench()
{
    mqttConfig.Begin();
    if (!mqttConfig.IsValid())
    {
        Serial.println("MQTT Config not valid - initializing");
        mqttConfig.GetRecord()._brokerIP = (uint32_t)(IPAddress(192, 168, 3, 48));
        mqttConfig.GetRecord()._brokerPort = 1883;
        strcpy(mqttConfig.GetRecord()._clientId, "SpaHeater");
        strcpy(mqttConfig.GetRecord()._username, "mqttuser");
        strcpy(mqttConfig.GetRecord()._password, "mqttpassword");
        strcpy(mqttConfig.GetRecord()._baseHATopic, _defaultBaseTopic);
        strcpy(mqttConfig.GetRecord()._haDeviceName, _defaultDeviceName);

        mqttConfig.Write();
        mqttConfig.Begin();
        $Assert(mqttConfig.IsValid());
    }
    else
    {
        Serial.println("MQTT Config is valid");
    }

    BuildTopicStrings(&mqttConfig);

    Serial.println("Boiler Config Message:");
    Serial.println(boilerConfigTopic);
    ExpandJson(Serial, BoilerConfigJsonTemplate, mqttConfig.GetRecord()._baseHATopic, mqttConfig.GetRecord()._haDeviceName, _defaultBoilerName);

    Serial.print("Boiler Base Topic: "); Serial.println(boilerBaseTopic);
    Serial.print("Boiler Avail Topic: "); Serial.println(boilerAvailTopic);
    Serial.print("Boiler Mode Topic: "); Serial.println(boilerModeTopic);
    Serial.print("Boiler Mode Set Topic: "); Serial.println(boilerModeSetTopic);
    Serial.print("Boiler Setpoint Topic: "); Serial.println(boilerSetpointTopic);
    Serial.print("Boiler Setpoint Set Topic: "); Serial.println(boilerSetpointSetTopic);
    Serial.print("Boiler Curr Temp Topic: "); Serial.println(boilerCurrTempTopic);
    Serial.print("Boiler Power Topic: "); Serial.println(boilerPowerTopic);
    Serial.println("**********************************");

    Serial.print("Ambient Thermometer Base Topic: "); Serial.println(ambientThermometerBaseTopic);
    ExpandJson(Serial, ThermometerConfigJsonTemplate, mqttConfig.GetRecord()._baseHATopic, mqttConfig.GetRecord()._haDeviceName, _defaultAmbientTempName);
    Serial.print("Ambient Thermometer Config Topic: "); Serial.println(ambientThermometerConfigTopic);
    Serial.print("Ambient Thermometer Avail Topic: "); Serial.println(ambientThermometerAvailTopic);
    Serial.print("Ambient Thermometer Temp Topic: "); Serial.println(ambientThermometerTempTopic);
    Serial.println("**********************************");

    Serial.print("Boiler In Thermometer Base Topic: "); Serial.println(boilerInThermometerBaseTopic);
    ExpandJson(Serial, ThermometerConfigJsonTemplate, mqttConfig.GetRecord()._baseHATopic, mqttConfig.GetRecord()._haDeviceName, _defaultBoilerInTempName);
    Serial.print("Boiler In Thermometer Config Topic: "); Serial.println(boilerInThermometerConfigTopic);
    Serial.print("Boiler In Thermometer Avail Topic: "); Serial.println(boilerInThermometerAvailTopic);
    Serial.print("Boiler In Thermometer Temp Topic: "); Serial.println(boilerInThermometerTempTopic);
    Serial.println("**********************************");

    Serial.print("Boiler Out Thermometer Base Topic: "); Serial.println(boilerOutThermometerBaseTopic);
    ExpandJson(Serial, ThermometerConfigJsonTemplate, mqttConfig.GetRecord()._baseHATopic, mqttConfig.GetRecord()._haDeviceName, _defaultBoilerOutTempName);
    Serial.print("Boiler Out Thermometer Config Topic: "); Serial.println(boilerOutThermometerConfigTopic);
    Serial.print("Boiler Out Thermometer Avail Topic: "); Serial.println(boilerOutThermometerAvailTopic);
    Serial.print("Boiler Out Thermometer Temp Topic: "); Serial.println(boilerOutThermometerTempTopic);
    Serial.println("**********************************");


    while (true)
    {
    }
}

//* Outgoing TCP client session - maintains a single connection to a remote server
class HA_MqttClient : public NetworkClient
{
public:
    HA_MqttClient() = delete;
    HA_MqttClient(IPAddress ServerIP, int ServerPort);
    ~HA_MqttClient();

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
