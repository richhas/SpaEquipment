// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// HA_MQTT class implementation

#include "SpaHeaterCntl.h"

#define MQTT_CLIENT_DEBUG 1
#include <ArduinoMqttClient.h>

namespace HA_Mqtt
{
    //* MQTT Configuration record
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

    //* Utility class for determining the size of an Expanded JSON string template
    class PrintOutputCounter : public Print
    {
    private:
        int _count;

    public:
        PrintOutputCounter() : _count(0) {}
        virtual size_t write(uint8_t c) override final
        {
            _count++;
            return 1;
        }
        virtual size_t write(const uint8_t *buffer, size_t size) override final
        {
            _count += size;
            return size;
        }
        int GetCount() const { return _count; }
        void Reset() { _count = 0; }
    };

    //* Utility class for expanding a JSON string template into a buffer
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

    //* Utility function for expanding a JSON template string using passed parameters into a Print object
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
    static constexpr char _defaultBaseTopic[] = "homeassistant";
    static constexpr char _haAvailTopic[] = "status";
        static constexpr char _haAvailOnline[] = "online";
        static constexpr char _haAvailOffline[] = "offline";

    static constexpr char _defaultDeviceName[] = "SpaHeater";
    static constexpr char _defaultBoilerName[] = "boiler";
    static constexpr char _defaultAmbientTempName[] = "ambientTemp";
    static constexpr char _defaultBoilerInTempName[] = "boilerInTemp";
    static constexpr char _defaultBoilerOutTempName[] = "boilerOutTemp";
    static constexpr char _defaultHeaterStateName[] = "HeatingElement";
    static constexpr char _defaultBoilerStateName[] = "BoilerState";
    static constexpr char _defaultFaultReasonName[] = "FaultReason";

    // MQTT Topic suffixes for Home Assistant MQTT supported platforms
    // Common
    static constexpr char _haConfig[] = "/config";
    static constexpr char _haAvail[] = "/avail";

    // water_heater
    static constexpr char _haWHMode[] = "/mode";
    static constexpr char _haWHModeSet[] = "/mode/set";
    static constexpr char _haWHSetpoint[] = "/temperature";
    static constexpr char _haWHSetpointSet[] = "/temperature/set";
    static constexpr char _haWHCurrTemp[] = "/current_temperature";
    static constexpr char _haWHPower[] = "/power";

    // sensor (temperature)
    static constexpr char _haSensorTemp[] = "/temperature";

    // binary_sensor (running)
    static constexpr char _haBinarySensorState[] = "/state";

    // sensor (enum)
    static constexpr char _haSensorEnum[] = "/state";

    // JSON template for Home Assistant MQTT water_heater configuration
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

    // JSON template for Home Assistant MQTT base topic for water_heater
    static constexpr char BoilerBaseTopicJsonTemplate[] = "%0/water_heater/%1";   // Parms: <base_topic>, <entity-name>
    char*   boilerBaseTopic;      // Boiler Base Topic expanded string
    int expandedMsgSizeOfBoilerConfigJson;

    // JSON template for Home Assistant MQTT sensor (temperature) configuration
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

    int expandedMsgSizeOfAmbientThermometerConfigJson;
    int expandedMsgSizeOfBoilerInThermometerConfigJson;
    int expandedMsgSizeOfBoilerOutThermometerConfigJson;

    // JSON template for Home Assistant MQTT base topic for sensor (temperature) topics
    static constexpr char ThermometerBaseTopicJsonTemplate[] = "%0/sensor/%1"; // Parms: <base_topic>, <entity-name>
    char*   ambientThermometerBaseTopic;          // Ambient Thermometer Base Topic expanded string
    char*   boilerInThermometerBaseTopic;         // Boiler In Thermometer Base Topic expanded string
    char*   boilerOutThermometerBaseTopic;        // Boiler Out Thermometer Base Topic expanded string


    // JSON template for Home Assistant MQTT binary_sensor (running) configuration
    static constexpr char BinarySensorConfigJsonTemplate[] =
        "{\n" // Parms: <base_topic>, <device-name>, <entity-name>
            "'~' : '%0/binary_sensor/%2',\n"
            "'name': '%2',\n"
            "'avty_t' : '~/avail',\n"
            "'avty_tpl' : '{{ value_json }}',\n"
            "'stat_t' : '~/state',\n"
            "'val_tpl' : '{{ value_json }}',\n"
            "'pl_on' : 'On',\n"
            "'pl_off' : 'Off',\n"
            "'uniq_id' : '%2',\n"
            "'dev':\n"
            "{\n"
                "'identifiers' : ['01'],\n"
                "'name' : '%1'\n"
            "}\n"
        "}\n";

    // JSON template for Home Assistant MQTT base topic for binary_sensor (running) topics
    static constexpr char BinarySensorBaseTopicJsonTemplate[] = "%0/binary_sensor/%1"; // Parms: <base_topic>, <entity-name>
    char*   heaterStateBinarySensorBaseTopic;           // Heater State Binary Sensor Base Topic expanded string
    int     expandedMsgSizeOfHeaterBinarySensorConfigJson;


    // JSON template for Home Assistant MQTT sensor (enum) configuration. This for any "sensor" that wants to just publish
    // a value that is an enum (e.g. "ON" or "OFF")
    static constexpr char EnumTextSensorConfigJsonTemplate[] =
        "{\n" // Parms: <base_topic>, <device-name>, <entity-name>
            "'~' : '%0/sensor/%2',\n"
            "'name': '%2',\n"
            "'device_class' : 'enum',\n"
            "'avty_t' : '~/avail',\n"
            "'avty_tpl' : '{{ value_json }}',\n"
            "'stat_t' : '~/state',\n"
            "'val_tpl' : '{{ value_json }}',\n"
            "'uniq_id' : '%2',\n"
            "'dev':\n"
            "{\n"
                "'identifiers' : ['01'],\n"
                "'name' : '%1'\n"
            "}\n"
        "}\n";

    // JSON template for Home Assistant MQTT base topic for sensor (enum) topic
    static constexpr char EnumTextSensorBaseTopicJsonTemplate[] = "%0/sensor/%1"; // Parms: <base_topic>, <entity-name>
    char* boilerStateSensorBaseTopic;           // Boiler State Sensor Base Topic expanded string
    int expandedMsgSizeOfBoilerStateSensorConfigJson;

    char* faultReasonSensorBaseTopic;           // Fault Reason Sensor Base Topic expanded string
    int expandedMsgSizeOfFaultReasonSensorConfigJson;


    // Describes a Home Assistant entity for the sake of building the /config and /avail messages
    class HaEntityDesc
    {
    public:
        const char* const _EntityName;
        const char* const _ConfigJsonTemplate;
        const char* const _BaseTopicJsonTemplate;
        char** _BaseTopicResult;
        int* _ExpandedMsgSizeResult;
    
        HaEntityDesc() = delete;
        HaEntityDesc(const char* EntityName, const char* ConfigJsonTemplate, const char* BaseTopicJsonTemplate, char** BaseTopicResult, int* ExpandedMsgSizeResult) :
            _EntityName(EntityName), 
            _ConfigJsonTemplate(ConfigJsonTemplate), 
            _BaseTopicJsonTemplate(BaseTopicJsonTemplate), 
            _BaseTopicResult(BaseTopicResult), 
            _ExpandedMsgSizeResult(ExpandedMsgSizeResult)
        {}
    };

    // Describes all Home Assistant entities for the sake of building the /config and /avail messages
    HaEntityDesc _entityDescs[] = 
    {
        HaEntityDesc(_defaultBoilerName, BoilerConfigJsonTemplate, BoilerBaseTopicJsonTemplate, &boilerBaseTopic, &expandedMsgSizeOfBoilerConfigJson),
        HaEntityDesc(_defaultAmbientTempName, ThermometerConfigJsonTemplate, ThermometerBaseTopicJsonTemplate, &ambientThermometerBaseTopic, &expandedMsgSizeOfAmbientThermometerConfigJson),
        HaEntityDesc(_defaultBoilerInTempName, ThermometerConfigJsonTemplate, ThermometerBaseTopicJsonTemplate, &boilerInThermometerBaseTopic, &expandedMsgSizeOfBoilerInThermometerConfigJson),
        HaEntityDesc(_defaultBoilerOutTempName, ThermometerConfigJsonTemplate, ThermometerBaseTopicJsonTemplate, &boilerOutThermometerBaseTopic, &expandedMsgSizeOfBoilerOutThermometerConfigJson),
        HaEntityDesc(_defaultHeaterStateName, BinarySensorConfigJsonTemplate, BinarySensorBaseTopicJsonTemplate, &heaterStateBinarySensorBaseTopic, &expandedMsgSizeOfHeaterBinarySensorConfigJson),
        HaEntityDesc(_defaultBoilerStateName, EnumTextSensorConfigJsonTemplate, EnumTextSensorBaseTopicJsonTemplate, &boilerStateSensorBaseTopic, &expandedMsgSizeOfBoilerStateSensorConfigJson),
        HaEntityDesc(_defaultFaultReasonName, EnumTextSensorConfigJsonTemplate, EnumTextSensorBaseTopicJsonTemplate, &faultReasonSensorBaseTopic, &expandedMsgSizeOfFaultReasonSensorConfigJson)
    };
    int _entityDescCount = sizeof(_entityDescs) / sizeof(HaEntityDesc);



    //* Helper to build expanded topic strings
    int BuildTopicString(const char& Template, char*& Result, const char* BaseTopic, const char* EntityName)
    {
        PrintOutputCounter counter;
        size_t size = ExpandJson(counter, &Template, BaseTopic, EntityName);        // Get size of expanded string
        $Assert(size > 0); // Error in template

        Result = new char[size + 1];                // Leave room for null terminator
        $Assert(Result != nullptr);                 // Out of memory
        BufferPrinter printer(Result, size);

        $Assert(ExpandJson(printer, &Template, BaseTopic, EntityName) == size);     // Expand template into buffer
        return size;
    }

    // Helper to build all expanded topic strings and compute sizes of the expanded HA entity /config JSON strings
    void InitStrings(FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase> *Config)
    {
        $Assert(Config->IsValid());

        //* Build each topic base string
        for (int i = 0; i < _entityDescCount; i++)
        {
            HaEntityDesc& desc = _entityDescs[i];
            BuildTopicString(*desc._BaseTopicJsonTemplate, *desc._BaseTopicResult, Config->GetRecord()._baseHATopic, desc._EntityName);
        }

        //* Compute sizes of the expanded HA entity /config JSON strings - this allow the use of a streaming for of MqttClient::BeginMessage();
        //  thus reducing memory usage by avoiding the need to allocate a larger buffer for the JSON string
        PrintOutputCounter counter;

        for (int i = 0; i < _entityDescCount; i++)
        {
            HaEntityDesc& desc = _entityDescs[i];
            *desc._ExpandedMsgSizeResult = ExpandJson(counter, desc._ConfigJsonTemplate, Config->GetRecord()._baseHATopic, Config->GetRecord()._haDeviceName, desc._EntityName);
            $Assert(*desc._ExpandedMsgSizeResult > 0);
        }
    }

    //* Flash store for MQTT configuration
    FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase> mqttConfig;
    static_assert(PS_MQTTBrokerConfigBlkSize >= sizeof(FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase>));
} // namespace HA_Mqtt

using namespace HA_Mqtt;

HA_MqttClient mqttClient;

void HA_MqttClient::setup()
{
    logger.Printf(Logger::RecType::Info, "mqttClientTask is starting");

    // Initialize the MQTT configuration - if not already initialized
    mqttConfig.Begin();
    if (!mqttConfig.IsValid())
    {
        logger.Printf(Logger::RecType::Info, "MQTT Config not valid - initializing to defaults");
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
        logger.Printf(Logger::RecType::Info, "MQTT Config is valid");
    }

    InitStrings(&mqttConfig);
}

void HA_MqttClient::loop()
{
    //** local support functions for the state machine

    //* Common beginMessage() function with topic expansion for all messages
    static auto BeginMessage = [](MqttClient &MqttClient, const char *TopicPrefix, const char *Suffix, uint32_t ExpandedMsgSize = 0xffffffffL) -> int
    {
        int status;
        {
            //* We use the shared buffer to build the full topic string and start the message
            auto handle = sharedPrintfBuffer.GetHandle(); // take lock on shared buffer
            char *buffer = (char *)handle.GetBuffer();
            BufferPrinter printer(buffer, handle.GetSize()); // create a buffer printer into the shared buffer

            size_t size = ExpandJson(printer, "%0%1", TopicPrefix, Suffix); // Append the /config topic suffix to the base topic
            $Assert(size < sharedPrintfBuffer.GetSize());

            // Begin the message with the expanded topic string + /config
            status = MqttClient.beginMessage((const char *)buffer, ExpandedMsgSize); // note: this form of beginMessage(, MsgSize) is used to avoid
                                                                                     // the need to allocate a larger buffer for the JSON string
        }

        return status;
    };

    //* Send a /config JSON message to Home Assistant for a given entity
    static auto SendConfigJSON = [&BeginMessage](
        MqttClient &MqttClient,
        const char *BaseTopic,
        const char *BaseEntityTopic,
        const char *DeviceName,
        const char *EntityName,
        const char *ConfigJsonPrototype,
        uint32_t ExpandedMsgSize) -> bool
    {
        int status = BeginMessage(MqttClient, BaseEntityTopic, _haConfig, ExpandedMsgSize);
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: Failed to begin message");
            return false;
        }

        // Write the expanded /config message body JSON string directly into the message stream
        size_t expandedSize = ExpandJson(MqttClient, ConfigJsonPrototype, BaseTopic, DeviceName, EntityName);
        if (expandedSize != ExpandedMsgSize)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: Expanded size of /config message body JSON string is incorrect");
            return false;
        }

        status = MqttClient.endMessage();
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: endMessage() failed");
            return false;
        }

        return true;
    };

    //* Send all /config JSON messages to Home Assistant for all entities
    static auto SendAllConfigMsgs = [&SendConfigJSON](MqttClient &MqttClient) -> bool
    {
        for (int i = 0; i < _entityDescCount; i++)
        {
            HaEntityDesc& desc = _entityDescs[i];
            if (!SendConfigJSON(
                MqttClient, 
                mqttConfig.GetRecord()._baseHATopic, 
                *desc._BaseTopicResult, 
                mqttConfig.GetRecord()._haDeviceName, 
                desc._EntityName, desc._ConfigJsonTemplate, 
                *desc._ExpandedMsgSizeResult))
            {
                return false;
            }
        }

       return true;
    };

    //* Send a /avail message to Home Assistant for a given entity
    static auto SendAvailMsg = [&BeginMessage](
        MqttClient &MqttClient,
        const char *BaseTopic,
        const char *BaseEntityTopic,
        const char *EntityName,
        const char *AvailStatus) -> bool
    {
        int status = BeginMessage(MqttClient, BaseEntityTopic, _haAvail);
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: Failed to begin message");
            return false;
        }

        // Write the /avail message body JSON string directly into the message stream
        size_t size = printf(MqttClient, "\"%s\"", AvailStatus);
        if (size == 0)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: Failed to write /avail message body JSON string");
            return false;
        }

        status = MqttClient.endMessage();
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: endMessage() failed");
            return false;
        }

        return true;
    };

    //* Send all /avail messages to Home Assistant for all entities
    static auto SendAllOnlineAvailMsgs = [&SendAvailMsg](MqttClient &MqttClient) -> bool
    {
        for (int i = 0; i < _entityDescCount; i++)
        {
            HaEntityDesc& desc = _entityDescs[i];
            if (!SendAvailMsg(MqttClient, mqttConfig.GetRecord()._baseHATopic, *desc._BaseTopicResult, desc._EntityName, _haAvailOnline))
            {
                return false;
            }
        }

        return true;
    };

    //* Send a float property message to Home Assistant for a given entity
    static auto SendPropertyMsg = [&BeginMessage](
        MqttClient &MqttClient,
        const char *BaseEntityTopic,
        const char *PropertyName,
        float       PropertyValue) -> bool
    {
        int status = BeginMessage(MqttClient, BaseEntityTopic, PropertyName);
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: SendPropertyMsg: Failed to begin message");
            return false;
        }

        // Write the property message body JSON string directly into the message stream
        size_t size = printf(MqttClient, "%0.2f", PropertyValue);
        if (size == 0)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: SendPropertyMsg: Failed to write property message body JSON string");
            return false;
        }

        status = MqttClient.endMessage();
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: SendPropertyMsg: endMessage() failed");
            return false;
        }

        return true;
    };

    //* Send a string property message to Home Assistant for a given entity
    static auto SendPropertyMsgStr = [&BeginMessage](
        MqttClient &MqttClient,
        const char *BaseEntityTopic,
        const char *PropertyName,
        const char *PropertyValue) -> bool
    {
        int status = BeginMessage(MqttClient, BaseEntityTopic, PropertyName);
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: SendPropertyMsg: Failed to begin message");
            return false;
        }

        // Write the property message body JSON string directly into the message stream
        size_t size = printf(MqttClient, "\"%s\"", PropertyValue);
        if (size == 0)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: SendPropertyMsg: Failed to write property message body JSON string");
            return false;
        }

        status = MqttClient.endMessage();
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: SendPropertyMsg: endMessage() failed");
            return false;
        }

        return true;
    };
    
    //* Monitor the Boiler State Machine for changes in state and send any changes to Home Assistant
    static auto MonitorBoiler = [&SendPropertyMsg, &SendPropertyMsgStr](MqttClient & MqttClient) -> bool
    {
        static uint32_t lastSeq = 0;
        static Timer timer(1000);
        static BoilerControllerTask::TempertureState state = {0.0, 0.0, 0.0, 0.0, 0.0, false};

        if (timer.IsAlarmed())
        {
            auto const seq = boilerControllerTask.GetHeaterStateSequence();
            bool force = (lastSeq == 0);
            if ((seq != lastSeq) || force)
            {
                BoilerControllerTask::TempertureState newState;
                boilerControllerTask.GetTempertureState(newState);

                if (force || (newState._boilerInTemp != state._boilerInTemp))
                {
                    if (!SendPropertyMsg(MqttClient, boilerBaseTopic, _haWHCurrTemp, $CtoF(newState._boilerInTemp)))
                    {
                        return false;
                    }
                    if (!SendPropertyMsg(MqttClient, boilerInThermometerBaseTopic, _haSensorTemp, $CtoF(newState._boilerInTemp)))
                    {
                        return false;
                    }
                }
                if (force || (newState._setPoint != state._setPoint))
                {
                    if (!SendPropertyMsg(MqttClient, boilerBaseTopic, _haWHSetpoint, $CtoF(newState._setPoint)))
                    {
                        return false;
                    }
                }
                if (force || (newState._boilerOutTemp != state._boilerOutTemp))
                {
                    if (!SendPropertyMsg(MqttClient, boilerOutThermometerBaseTopic, _haSensorTemp, $CtoF(newState._boilerOutTemp)))
                    {
                        return false;
                    }
                }
                if (force || (newState._ambiantTemp != state._ambiantTemp))
                {
                    if (!SendPropertyMsg(MqttClient, ambientThermometerBaseTopic, _haSensorTemp, $CtoF(newState._ambiantTemp)))
                    {
                        return false;
                    }
                }
                if (force || (newState._heaterOn != state._heaterOn))
                {
                    if (!SendPropertyMsgStr(MqttClient, heaterStateBinarySensorBaseTopic, _haBinarySensorState, newState._heaterOn ? "On" : "Off"))
                    {
                        return false;
                    }
                }

                state = newState;
                lastSeq = state._sequence;
            }

            // Send the boiler state if it has changed or if forced - translate the state to a string
            static BoilerControllerTask::HeaterState lastHeaterState = BoilerControllerTask::HeaterState(-1);
            BoilerControllerTask::HeaterState currHeaterState = boilerControllerTask.GetHeaterState();

            if (force || (currHeaterState != lastHeaterState))
            {
                if (!SendPropertyMsgStr(
                    MqttClient, 
                    boilerStateSensorBaseTopic, 
                    _haSensorEnum, 
                    BoilerControllerTask::GetHeaterStateDescription(currHeaterState)))
                {
                    return false;
                }

                lastHeaterState = currHeaterState;
            }

            // Send the fault reason if it has changed or if forced - translate the fault reason to a string
            static BoilerControllerTask::FaultReason lastFaultReason = BoilerControllerTask::FaultReason(-1);
            BoilerControllerTask::FaultReason currFaultReason = boilerControllerTask.GetFaultReason();

            if (force || (currFaultReason != lastFaultReason))
            {
                if (!SendPropertyMsgStr(
                    MqttClient, 
                    faultReasonSensorBaseTopic, 
                    _haSensorEnum, 
                    BoilerControllerTask::GetFaultReasonDescription(currFaultReason)))
                {
                    return false;
                }

                lastFaultReason = currFaultReason;
            }

            timer.SetAlarm(1000);
        }

        return true;
    };

    //** State machine implementation for MQTT client
    static bool firstTime = true;

    enum class State
    {
        WaitForWiFi,
        DelayAfterWiFiConnected,
        Delay,
        ConnectingToBroker,
        Connected
    };

    static State state;
    static Timer delayTimer;
    static WiFiClient wifiClient;
    static MqttClient mqttClient(wifiClient);
    static int wifiStatus;
    static Timer publishConfigTimer;
    static Timer publishAvailTimer;

    if (firstTime)
    {
        state = State::WaitForWiFi;
        firstTime = false;
    }

    switch (state)
    {
        case State::WaitForWiFi:
        {
            wifiStatus = WiFi.status();
            if (wifiStatus == WL_CONNECTED)
            {   
                delayTimer.SetAlarm(4000);
                state = State::DelayAfterWiFiConnected;
            }
            else
            {
                logger.Printf(Logger::RecType::Info, "Waiting for WiFi to connect");
                state = State::Delay;
                delayTimer.SetAlarm(5000);
            }
        }
        break;

        case State::Delay:
        {
            mqttClient.stop();
            if (delayTimer.IsAlarmed())
            {
                state = State::WaitForWiFi;
            }
        }
        break;

        case State::DelayAfterWiFiConnected:
        {
            if (delayTimer.IsAlarmed())
            {
                state = State::ConnectingToBroker;
            }
        }
        break;

        case State::ConnectingToBroker:
        {
            static IPAddress brokerIP;

            logger.Printf(Logger::RecType::Info, "Connecting to MQTT Broker");
            brokerIP = mqttConfig.GetRecord()._brokerIP;
            mqttClient.stop();
            mqttClient.setId(mqttConfig.GetRecord()._clientId);
            mqttClient.setUsernamePassword(mqttConfig.GetRecord()._username, mqttConfig.GetRecord()._password);

            // ADD: LWT topic; this will change all /avail topics for the temperature sensors to be the same as the boiler /avail topic

            if (!mqttClient.connect(brokerIP, mqttConfig.GetRecord()._brokerPort))
            {
                logger.Printf(Logger::RecType::Critical, "Failed to connect to MQTT Broker - delaying 5 secs and retrying");
                delayTimer.SetAlarm(5000);
                state = State::Delay;
                return;
            }

            logger.Printf(Logger::RecType::Info, "Connected to MQTT Broker - publishing initial /config and /avail messages to Home Assistant");   

            publishConfigTimer.SetAlarm(0);                         // Cause the /config messages to be sent immediately
            publishAvailTimer.SetAlarm(publishAvailTimer.FOREVER);  // delay the /avail messages to be sent until after the /config messages have been sent

            // Subscribe to all the Home Assistant incoming topics for each entity
            state = State::Connected;
        }
        break;

        case State::Connected:
        {
            if (!mqttClient.connected())
            {
                logger.Printf(Logger::RecType::Critical, "Lost connection to MQTT Broker - restarting");
                mqttClient.stop();
                state = State::WaitForWiFi;
                return;
            }

            mqttClient.poll();

            if (publishConfigTimer.IsAlarmed())
            {
                // Publish the Home Assistant /config JSON strings for each entity
                logger.Printf(Logger::RecType::Info, "Sending /config messages to Home Assistant");
                if (!SendAllConfigMsgs(mqttClient))
                {
                    logger.Printf(Logger::RecType::Critical, "Failed to send /config messages to Home Assistant - restarting");
                    state = State::Delay;
                    return;
                }

                publishConfigTimer.SetAlarm(publishConfigTimer.FOREVER);
                publishAvailTimer.SetAlarm(1000); // delay the /avail messages to be sent until after the /config messages have been sent
            }

            if (publishAvailTimer.IsAlarmed())
            {
                // Make all entities available
                logger.Printf(Logger::RecType::Info, "Sending /avail messages to Home Assistant");
                if (!SendAllOnlineAvailMsgs(mqttClient))
                {
                    logger.Printf(Logger::RecType::Critical, "Failed to send /avail messages to Home Assistant - restarting");
                    state = State::Delay;
                    return;
                }

                publishAvailTimer.SetAlarm(publishAvailTimer.FOREVER);
            }

            // Check for incoming messages

            // republish the Home Assistant /configs topic for each entity is something has changed (HA restart, etc) or some amount of time has passed

            // Publish to Home Assistant any change of state for the Boiler for each entity
            if (!MonitorBoiler(mqttClient))
            {
                logger.Printf(Logger::RecType::Warning, "Failed in MonitorBoiler - restarting");
                state = State::Delay;
                return;
            }
        }
        break;

        default:
        {
            $FailFast();
        }
    }
}