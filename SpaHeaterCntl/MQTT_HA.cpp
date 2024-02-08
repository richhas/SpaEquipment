// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// HA_MQTT class implementation

#include "SpaHeaterCntl.h"
#include <ArduinoMqttClient.h>
#include <functional>

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
        // TODO: Add max sizes to these fields
        constexpr static int _maxUsernameLen = 16;
        constexpr static int _maxPasswordLen = 16;
        constexpr static int _maxClientIdLen = 16;
        char _clientId[16];
        char _username[16];
        char _password[16];

        // HA MQTT config
        constexpr static int _maxBaseHATopicLen = 32;
        constexpr static int _maxHaDeviceNameLen = 32;
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
    //  For example this function is used to expand a JSON template string direct;ly into a MqttClient 
    //  message stream
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
    static constexpr char _defaultBoilerName[] = "Boiler"; 
    static constexpr char _defaultBoilerInTempName[] = "BoilerInTemp";
    static constexpr char _defaultBoilerOutTempName[] = "BoilerOutTemp";
    static constexpr char _defaultAmbientTempName[] = "AmbientTemp";
    static constexpr char _defaultHeaterStateName[] = "HeatingElement";
    static constexpr char _defaultBoilerStateName[] = "BoilerState";
    static constexpr char _defaultFaultReasonName[] = "FaultReason";
    static constexpr char _defaultResetButtonName[] = "ResetButton";
    static constexpr char _defaultStartButtonName[] = "StartButton";
    static constexpr char _defaultStopButtonName[] = "StopButton";
    static constexpr char _defaultRebootButtonName[] = "RebootButton";

    static constexpr char _commonAvailTopicTemplate[] = "TinyBus/%0/avail";
    char*   commonAvailTopic;      // Common Avail Topic expanded string


    // MQTT Topic suffixes for Home Assistant MQTT supported platforms
    // Common
    static constexpr char _haConfig[] = "/config";
    static constexpr char _haAvail[] = "/avail";

    // water_heater
    static constexpr char _haWHMode[] = "/mode";
    static constexpr char _haWHModeSet[] = "/mode/set";             // incoming command topic
    static constexpr char _haWHSetpoint[] = "/temperature";
    static constexpr char _haWHSetpointSet[] = "/temperature/set";  // incoming command topic
    static constexpr char _haWHCurrTemp[] = "/current_temperature";
    static constexpr char _haWHPower[] = "/power";

    // sensor (temperature)
    static constexpr char _haSensorTemp[] = "/temperature";

    // binary_sensor (running)
    static constexpr char _haBinarySensorState[] = "/state";

    // sensor (enum)
    static constexpr char _haSensorEnum[] = "/state";

    // button cmd
    static constexpr char _haButtonCmd[] = "/cmd";      // incoming command topic

    

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

            "'avty_t' : 'TinyBus/%1/avail',\n"
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
            "'avty_t' : 'TinyBus/%1/avail',\n"
            "'avty_tpl' : '{{ value_json }}',\n"
            "'stat_t' : '~/temperature',\n"    
            "'uniq_id' : '%2',\n"
            "'dev':\n"
            "{\n"
                "'identifiers' : ['01'],\n"
                "'name' : '%1'\n"
            "}\n"
        "}\n";

    // JSON template for Home Assistant MQTT base topic for sensor (temperature) topics
    static constexpr char ThermometerBaseTopicJsonTemplate[] = "%0/sensor/%1"; // Parms: <base_topic>, <entity-name>
    char*   ambientThermometerBaseTopic;          // Ambient Thermometer Base Topic expanded string
    char*   boilerInThermometerBaseTopic;         // Boiler In Thermometer Base Topic expanded string
    char*   boilerOutThermometerBaseTopic;        // Boiler Out Thermometer Base Topic expanded string
    int expandedMsgSizeOfAmbientThermometerConfigJson;
    int expandedMsgSizeOfBoilerInThermometerConfigJson;
    int expandedMsgSizeOfBoilerOutThermometerConfigJson;

    // JSON template for Home Assistant MQTT binary_sensor (running) configuration
    static constexpr char BinarySensorConfigJsonTemplate[] =
        "{\n" // Parms: <base_topic>, <device-name>, <entity-name>
            "'~' : '%0/binary_sensor/%2',\n"
            "'name': '%2',\n"
            "'avty_t' : 'TinyBus/%1/avail',\n"
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
            "'avty_t' : 'TinyBus/%1/avail',\n"
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

    // JSON template for Home Assistant MQTT button configuration
    static constexpr char ButtonConfigJsonTemplate[] =
        "{\n" // Parms: <base_topic>, <device-name>, <entity-name>
            "'~' : '%0/button/%2',\n"
            "'name': '%2',\n"
            "'avty_t' : 'TinyBus/%1/avail',\n"
            "'avty_tpl' : '{{ value_json }}',\n"
            "'command_topic' : '~/cmd',\n"
            "'device_class' : 'restart',\n"
            "'uniq_id' : '%2',\n"
            "'dev':\n"
            "{\n"
                "'identifiers' : ['01'],\n"
                "'name' : '%1'\n"
            "}\n"
        "}\n";


    // JSON template for Home Assistant MQTT base topic for button topics
    static constexpr char ButtonBaseTopicJsonTemplate[] = "%0/button/%1"; // Parms: <base_topic>, <entity-name>
    char*   resetButtonBaseTopic;           // Reset Button Base Topic expanded string
    int expandedMsgSizeOfResetButtonConfigJson;
    char*   startButtonBaseTopic;           // Start Button Base Topic expanded string
    int expandedMsgSizeOfStartButtonConfigJson;
    char*   stopButtonBaseTopic;            // Stop Button Base Topic expanded string
    int expandedMsgSizeOfStopButtonConfigJson;
    char*   rebootButtonBaseTopic;          // Reboot Button Base Topic expanded string
    int expandedMsgSizeOfRebootButtonConfigJson;


    //* Describes a Home Assistant entity for the sake of building the /config and /avail messages
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

    // Describes all Home Assistant entities - for building the /config and /avail messages
    // TODO: Enhance this to support the scheduling of the sending of /config and /avail messages. A Timer() is
    //       instance& will be carried with each entry. For each entity above we will declare such a Timer() instance.
    //       There will be a method that looks at one entry per call and if the timer is expired the corresponding /config
    //       and /avail messages will be sent. The timer will be reset. When the method does detect an Alarm, it will
    //       will send the /config and then one the next call, it will send the /avail message... This timer needs to
    //       also cause the sending of related property messages. 
    HaEntityDesc _entityDescs[] = 
    {
        HaEntityDesc(_defaultBoilerName, BoilerConfigJsonTemplate, BoilerBaseTopicJsonTemplate, &boilerBaseTopic, &expandedMsgSizeOfBoilerConfigJson),
        HaEntityDesc(_defaultAmbientTempName, ThermometerConfigJsonTemplate, ThermometerBaseTopicJsonTemplate, &ambientThermometerBaseTopic, &expandedMsgSizeOfAmbientThermometerConfigJson),
        HaEntityDesc(_defaultBoilerInTempName, ThermometerConfigJsonTemplate, ThermometerBaseTopicJsonTemplate, &boilerInThermometerBaseTopic, &expandedMsgSizeOfBoilerInThermometerConfigJson),
        HaEntityDesc(_defaultBoilerOutTempName, ThermometerConfigJsonTemplate, ThermometerBaseTopicJsonTemplate, &boilerOutThermometerBaseTopic, &expandedMsgSizeOfBoilerOutThermometerConfigJson),
        HaEntityDesc(_defaultHeaterStateName, BinarySensorConfigJsonTemplate, BinarySensorBaseTopicJsonTemplate, &heaterStateBinarySensorBaseTopic, &expandedMsgSizeOfHeaterBinarySensorConfigJson),
        HaEntityDesc(_defaultBoilerStateName, EnumTextSensorConfigJsonTemplate, EnumTextSensorBaseTopicJsonTemplate, &boilerStateSensorBaseTopic, &expandedMsgSizeOfBoilerStateSensorConfigJson),
        HaEntityDesc(_defaultFaultReasonName, EnumTextSensorConfigJsonTemplate, EnumTextSensorBaseTopicJsonTemplate, &faultReasonSensorBaseTopic, &expandedMsgSizeOfFaultReasonSensorConfigJson),
        HaEntityDesc(_defaultResetButtonName, ButtonConfigJsonTemplate, ButtonBaseTopicJsonTemplate, &resetButtonBaseTopic, &expandedMsgSizeOfResetButtonConfigJson),
        HaEntityDesc(_defaultStartButtonName, ButtonConfigJsonTemplate, ButtonBaseTopicJsonTemplate, &startButtonBaseTopic, &expandedMsgSizeOfStartButtonConfigJson),
        HaEntityDesc(_defaultStopButtonName, ButtonConfigJsonTemplate, ButtonBaseTopicJsonTemplate, &stopButtonBaseTopic, &expandedMsgSizeOfStopButtonConfigJson),
        HaEntityDesc(_defaultRebootButtonName, ButtonConfigJsonTemplate, ButtonBaseTopicJsonTemplate, &rebootButtonBaseTopic, &expandedMsgSizeOfRebootButtonConfigJson) 
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

        //* Build the common avail topic string
        int sizeNeeded = ExpandJson(counter, _commonAvailTopicTemplate, Config->GetRecord()._haDeviceName) + 1;
        commonAvailTopic = new char[sizeNeeded];
        BufferPrinter printer(commonAvailTopic, sizeNeeded);
        $Assert(ExpandJson(printer, _commonAvailTopicTemplate, Config->GetRecord()._haDeviceName) == sizeNeeded - 1);
    }

    //* Flash store for MQTT configuration
    FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase> mqttConfig;
    static_assert(PS_MQTTBrokerConfigBlkSize >= sizeof(FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase>));
} // namespace HA_Mqtt


using namespace HA_Mqtt;

HA_MqttClient mqttClient;


//** HA_MqttClient class implementation - Setup for the MQTT client task. 
//   This function is called from the main setup() function
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


//** Main task loop - implements the state machine for the MQTT client
//   This function is called from the main loop() function
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

    //* Send a /config JSON message to Home Assistant for a given entity (topic)
    static auto SendConfigJSON = [](
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

   //* Send a /avail message to Home Assistant
    static auto SendOnlineAvailMsg = [](MqttClient &MqttClient) -> bool
    {
        int status = MqttClient.beginMessage(commonAvailTopic);
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "HA_MqttClient: Failed to begin message");
            return false;
        }

        size_t size = MqttClient.print("\"online\"");
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

    //* Send a float property message to Home Assistant for a given entity
    static auto SendPropertyMsg = [](
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
    static auto SendPropertyMsgStr = [](
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
    
    //* Monitor the Boiler State Machine for changes in state and send any changes to Home Assistant. All updated
    //  properties are sent to Home Assistant as separate messages from this function.
    static auto MonitorBoiler = [](MqttClient & MqttClient) -> bool
    {
        // TODO: This needs to become a state machine that services the 1sec items and also the high frequency items
        //       The state machine has at least teo states: CalcWork and SendWork. The CalcWork state is where the
        //       state machine calculates the work to be done and the SendWork state is where the state machine 
        //       sends each planned message - one per call - until all work has been done for the cycle. The state
        //       machine then transitions back to the CalcWork state. In the CalcWork state, the state machine
        //       first the 1sec timer is checked and if it is alarmed, the state machine calculates the work to be
        //       done and transitions. The high frequency items are then checked and if any have changed state, they
        //       are added to the work list. The state machine then transitions to the SendWork state. In the SendWork
        //       state, the state machine sends one message from the work list per cycle (call) until the work list is
        //       empty. The state machine then transitions back to the CalcWork state.
        
        static Timer timer(1000);

        if (timer.IsAlarmed())
        {
            static BoilerControllerTask::TempertureState state = {0, 0.0, 0.0, 0.0, 0.0, 0.0, false};
            static uint32_t lastSeq = 0;

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

                /* Covered below - more frequently: Pull eventually
                if (force || (newState._setPoint != state._setPoint))
                {
                    if (!SendPropertyMsg(MqttClient, boilerBaseTopic, _haWHSetpoint, $CtoF(newState._setPoint)))
                    {
                        return false;
                    }
                }
                */

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

            timer.SetAlarm(1000);
        }

        // Send the boiler state if it has changed or if forced - translate the state to a string
        static BoilerControllerTask::StateMachineState lastHeaterState = BoilerControllerTask::StateMachineState(-1);
        BoilerControllerTask::StateMachineState currHeaterState = boilerControllerTask.GetStateMachineState();

        if (currHeaterState != lastHeaterState)
        {
            logger.Printf(Logger::RecType::Info, "Sending Heater State: %s", BoilerControllerTask::GetStateMachineStateDescription(currHeaterState)); 
            if (!SendPropertyMsgStr(
                MqttClient, 
                boilerStateSensorBaseTopic, 
                _haSensorEnum, 
                BoilerControllerTask::GetStateMachineStateDescription(currHeaterState)))
            {
                return false;
            }

            lastHeaterState = currHeaterState;
        }

        // Send the fault reason if it has changed or if forced - translate the fault reason to a string
        static BoilerControllerTask::FaultReason lastFaultReason = BoilerControllerTask::FaultReason(-1);
        BoilerControllerTask::FaultReason currFaultReason = boilerControllerTask.GetFaultReason();

        if (currFaultReason != lastFaultReason)
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

        // Send the current boiler mode if needed
        static BoilerControllerTask::BoilerMode lastBoilerMode = BoilerControllerTask::BoilerMode(-1);
        BoilerControllerTask::BoilerMode currBoilerMode = boilerControllerTask.GetMode();

        if (currBoilerMode != lastBoilerMode)
        {
            if (!SendPropertyMsgStr(
                    MqttClient,
                    boilerBaseTopic,
                    _haWHMode,
                    BoilerControllerTask::GetBoilerModeDescription(currBoilerMode)))
            {
                return false;
            }

            lastBoilerMode = currBoilerMode;
        }

        static BoilerControllerTask::TargetTemps lastTargetTemps = {-999.99, -999.99};
        BoilerControllerTask::TargetTemps currTargetTemps; boilerControllerTask.GetTargetTemps(currTargetTemps);

        if (currTargetTemps._setPoint != lastTargetTemps._setPoint)
        {
            if (!SendPropertyMsg(MqttClient, boilerBaseTopic, _haWHSetpoint, $CtoF(currTargetTemps._setPoint)))
            {
                return false;
            }

            lastTargetTemps._setPoint = currTargetTemps._setPoint;
        }

        return true;
    };

    //** Support for subscription notification and handling of Home Assistant topics
    using NotificationHandler = std::function<bool(const char *)>;  // A subscription notification handler function type

    //* Notification handlers for each MQTT subscription topic
    static NotificationHandler HandleWHModeSet = [] (const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "Received WH Mode Set command: %s", Payload);
        BoilerControllerTask::BoilerMode mode = BoilerControllerTask::GetBoilerModeFromDescription(Payload);
        if (mode == BoilerControllerTask::BoilerMode::Undefined)
        {
            logger.Printf(Logger::RecType::Warning, "Invalid WH Mode Set command value: %s", Payload);
            return false;
        }

        // Update the mode in the configuration and in the controller
        boilerConfig.GetRecord()._mode = mode;
        boilerConfig.Write();
        boilerConfig.Begin();
        if (!boilerConfig.IsValid())
        {
            logger.Printf(Logger::RecType::Warning, "Failed to write WH Mode Set command value to config: %s", Payload);
            return false;
        }

        boilerControllerTask.SetMode(mode);

        return true;
    };

    static NotificationHandler HandleWHSetpointSet = [] (const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "Received WH Setpoint Set command: %s", Payload);
        float setpoint = $FtoC(atof(Payload));

        BoilerControllerTask::TargetTemps targetTemps;
        boilerControllerTask.GetTargetTemps(targetTemps);

        // Update the setpoint in the configuration and in the controller
        boilerConfig.GetRecord()._setPoint = setpoint;
        boilerConfig.Write();
        boilerConfig.Begin();
        if (!boilerConfig.IsValid())
        {
            logger.Printf(Logger::RecType::Warning, "Failed to write WH Setpoint command value to config: %s", Payload);
            return false;
        }

        targetTemps._setPoint = setpoint;
        boilerControllerTask.SetTargetTemps(targetTemps);

        return true;
    };

    static NotificationHandler HandleResetButtonCmd = [] (const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "Received Reset Button Event: %s", Payload);
        boilerControllerTask.ResetIfSafe();
        return true;
    };

    static NotificationHandler HandleStartButtonCmd = [] (const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "Received Start Button Event: %s", Payload);
        boilerControllerTask.StartIfSafe();
        return true;
    };

    static NotificationHandler HandleStopButtonCmd = [] (const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "Received Stop Button Event: %s", Payload);
        boilerControllerTask.StopIfSafe();
        return true;
    };

    static NotificationHandler HandleRebootButtonCmd = [] (const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "Received Reboot Button Event: %s", Payload);
        logger.Printf(Logger::RecType::Info, "***rebooting***");
        delay(1000);
        NVIC_SystemReset();
        $FailFast();
        return true;
    };

    //* Describes a Home Assistant MQTT subscription topic and its notification handler
    class SubscribedTopic
    {
    public:
        const char *_baseTopic;
        const char *_topicSuffix;
        int _baseTopicSize;
        int _topicSuffixSize;
        NotificationHandler &_handler;

        SubscribedTopic(const char *BaseTopic, const char *TopicSuffix, NotificationHandler &Handler)
            : _baseTopic(BaseTopic),
              _topicSuffix(TopicSuffix),
              _handler(Handler)
        {
            _baseTopicSize = strlen(BaseTopic);
            _topicSuffixSize = strlen(TopicSuffix);
        }

        SubscribedTopic() = delete;
    };

    //* Table of all subscribed topics
    static SubscribedTopic subscribedTopics[] =
    {
        SubscribedTopic(boilerBaseTopic, _haWHModeSet, HandleWHModeSet),
        SubscribedTopic(boilerBaseTopic, _haWHSetpointSet, HandleWHSetpointSet),
        SubscribedTopic(resetButtonBaseTopic, _haButtonCmd, HandleResetButtonCmd),
        SubscribedTopic(startButtonBaseTopic, _haButtonCmd, HandleStartButtonCmd),
        SubscribedTopic(stopButtonBaseTopic, _haButtonCmd, HandleStopButtonCmd),
        SubscribedTopic(rebootButtonBaseTopic, _haButtonCmd, HandleRebootButtonCmd)
    };
    static int subscribedTopicCount = sizeof(subscribedTopics) / sizeof(SubscribedTopic);

    //* Notification dispatcher for incoming MQTT messages
    static auto OnMessage = [](int msgSize, MqttClient &Client) -> void
    {
        // Process the incoming message
        String topic = std::move(Client.messageTopic());
        char payload[msgSize + 1];
        int read = Client.read((uint8_t *)&payload[0], msgSize);
        $Assert(read == msgSize);
        payload[msgSize] = '\0';
        logger.Printf(Logger::RecType::Info, "Received message on topic: %s - payload: %s", topic.c_str(), payload);

        // Call the handler for the topic
        int i;
        for (i = 0; i < subscribedTopicCount; i++)
        {
            if ((topic.length() == (subscribedTopics[i]._baseTopicSize + subscribedTopics[i]._topicSuffixSize)) 
                                    && (topic.startsWith(subscribedTopics[i]._baseTopic)) 
                                    && (topic.endsWith(subscribedTopics[i]._topicSuffix)))
            {
                subscribedTopics[i]._handler(payload); // Call the handler for the topic; given it the payload
                break;
            }
        }

        if (i == subscribedTopicCount)
        {
            logger.Printf(Logger::RecType::Warning, "Topic: %s not found in subscribedTopics table", topic.c_str());
        }
    };

    //******************************************************************************
    //** State machine implementation for MQTT client
    enum class State
    {
        WaitForWiFi,
        DelayAfterWiFiConnected,
        Delay,
        ConnectingToBroker,
        SendSubscriptions,
        SendConfigs,
        SendOnlineAvailMsg,
        Connected
    };

    static StateMachineState<State> state(State::WaitForWiFi);
    static Timer delayTimer;
    static WiFiClient wifiClient;
    static MqttClient mqttClient(wifiClient);
    static int wifiStatus;


    switch ((State)state)
    {
        case State::WaitForWiFi:
        {
            wifiStatus = WiFi.status();
            if (wifiStatus == WL_CONNECTED)
            {   
                delayTimer.SetAlarm(4000);
                state.ChangeState(State::DelayAfterWiFiConnected);
            }
            else
            {
                logger.Printf(Logger::RecType::Info, "Waiting for WiFi to connect");
                state.ChangeState(State::Delay);
                delayTimer.SetAlarm(5000);
            }
        }
        break;

        case State::Delay:
        {
            mqttClient.stop();
            if (delayTimer.IsAlarmed())
            {
                state.ChangeState(State::WaitForWiFi);
            }
        }
        break;

        case State::DelayAfterWiFiConnected:
        {
            if (delayTimer.IsAlarmed())
            {
                state.ChangeState(State::ConnectingToBroker);
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
                // TODO: Keep track of the number of failed attempts to connect to the broker. If it exceeds a threshold and
                // there are no other network connections, then reset the WiFi.
                logger.Printf(Logger::RecType::Critical, "Failed to connect to MQTT Broker - delaying 5 secs and retrying");
                delayTimer.SetAlarm(5000);
                state.ChangeState(State::Delay);
                return;
            }

            logger.Printf(Logger::RecType::Info, "Connected to MQTT Broker - publishing initial /config and /avail messages to Home Assistant");   

            // Set the message handler for incoming messages
            mqttClient.onMessage([](int messageSize) -> void { OnMessage(messageSize, mqttClient); } );

            // Subscribe to all the Home Assistant incoming topics for each entity
            state.ChangeState(State::SendSubscriptions);
        }
        break;

        case State::SendSubscriptions:
        {
            // Subscribe to all the Home Assistant incoming topics of interest - only one subscription per call
            // to loop() to allow other network related tasks to be performed between each subscription
            static int ix;

            if (state.IsFirstTime())
            {
                ix = 0;
            }

            if (ix < subscribedTopicCount)
            {
                string topic = subscribedTopics[ix]._baseTopic;          // TODO: use a stack buffer (prefix + suffix)
                topic += subscribedTopics[ix]._topicSuffix;

                logger.Printf(Logger::RecType::Info, "Subscribing to topic: %s", topic.c_str());

                if (!mqttClient.subscribe(topic.c_str()))
                {
                    logger.Printf(Logger::RecType::Critical, "Failed to subscribe to topic: %s - restarting", topic.c_str());
                    state.ChangeState(State::Delay);
                    return;
                }

                ix++;
            }
            else
            {
                logger.Printf(Logger::RecType::Info, "All subscriptions sent to Home Assistant - now sending /config messages");
                state.ChangeState(State::SendConfigs);
            }
        }
        break;

        case State::SendConfigs:
        {
            // Publish the Home Assistant /config JSON strings for each entity - only one /config message per call
            // to loop() to allow other tasks to be performed between each /config message
            static int ix;

            if (state.IsFirstTime())
            {
                ix = 0;
            }

            if (ix < _entityDescCount)
            {
                HaEntityDesc &desc = _entityDescs[ix];
                logger.Printf(Logger::RecType::Info, "Sending /config message for entity: %s", desc._EntityName);
                if (!SendConfigJSON(
                        mqttClient,
                        mqttConfig.GetRecord()._baseHATopic,
                        *desc._BaseTopicResult,
                        mqttConfig.GetRecord()._haDeviceName,
                        desc._EntityName, desc._ConfigJsonTemplate,
                        *desc._ExpandedMsgSizeResult))
                {
                    logger.Printf(Logger::RecType::Critical, "Failed to send /config message for entity: %s - restarting", desc._EntityName);
                    state.ChangeState(State::Delay);
                    return;
                }
            }
            else
            {
                logger.Printf(Logger::RecType::Info, "All /config messages sent to Home Assistant - now sending /avail message");
                state.ChangeState(State::SendOnlineAvailMsg);
            }

            ix++;
        }
        break;

        case State::SendOnlineAvailMsg:
        {
            // Tell Home Assistant that all entities are available
            if (!SendOnlineAvailMsg(mqttClient))
            {
                logger.Printf(Logger::RecType::Critical, "Failed to send /avail messages to Home Assistant - restarting");
                state.ChangeState(State::Delay);
                return;
            }

            logger.Printf(Logger::RecType::Info, "/avail message sent to Home Assistant - now monitoring for incoming messages");
            state.ChangeState(State::Connected);
        }


        // TODO: Generally we need to break up the outgoing messages into smaller chunks and send them over time to 
        //       allow other network related tasks to be performed. This will also allow the system to be more responsive
        //       to incoming messages and other tasks.
        //
        //       In additon we need to keep an activity time for each subscription and if no activity is seen for a period
        //       of time then we need to re-subscribe to the topic. This will allow the system to recover from network
        //       outages and broker restarts.
        //
        //       There should be a Timer for each entity that we reset each time there is activity on the topic. If the
        //       timer expires then we publish the /coning and /avail messages for that entity and re-subscribe its
        //       related topics. In addition, we should republish each state property for the entity.
        //
        case State::Connected:
        {
            if (!mqttClient.connected())
            {
                logger.Printf(Logger::RecType::Critical, "Lost connection to MQTT Broker - restarting");
                mqttClient.stop();
                state.ChangeState(State::WaitForWiFi);
                return;
            }

            mqttClient.poll();  // Check for incoming messages

            // Publish to Home Assistant any change of state for the Boiler for each entity
            if (!MonitorBoiler(mqttClient))
            {
                logger.Printf(Logger::RecType::Warning, "Failed in MonitorBoiler - restarting");
                state.ChangeState(State::Delay);
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

