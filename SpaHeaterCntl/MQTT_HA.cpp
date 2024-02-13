// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// HA_MQTT class implementation

#include "SpaHeaterCntl.hpp"
#include <ArduinoMqttClient.h>
#include <functional>
#include "MQTT_HA.hpp"

namespace TinyBus
{
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
            constexpr static int _maxUsernameLen = 16;
            constexpr static int _maxPasswordLen = 16;
            constexpr static int _maxClientIdLen = 16;
            char _clientId[_maxClientIdLen];
            char _username[_maxUsernameLen];
            char _password[_maxPasswordLen];

            // HA MQTT config
            constexpr static int _maxBaseHATopicLen = 32;
            constexpr static int _maxHaDeviceNameLen = 32;
            char _baseHATopic[_maxBaseHATopicLen];
            char _haDeviceName[_maxHaDeviceNameLen];

            bool IsFullyConfigured() const
            {
                return  (_brokerIP != 0) && 
                        (_brokerPort != 0) && 
                        (_clientId[0] != 0) && 
                        (_username[0] != 0) && 
                        (_password[0] != 0) && 
                        (_baseHATopic[0] != 0) && 
                        (_haDeviceName[0] != 0);
            }
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

        
        //*** Home Assistant MQTT Namespace Names definitions
        static constexpr char _defaultBaseTopic[] = "homeassistant";
        static constexpr char _haAvailTopicSuffix[] = "status";
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
        static constexpr char _defaultHysterisisName[] = "Hysterisis";

        //* HA MQTT Intg Topic strings
        static constexpr char _haIntgAvailSuffix[] = "/status";

        //* Common Avail Topic for all entities
        static constexpr char _commonAvailTopicTemplate[] = "TinyBus/%0/avail";     // %0=Device Name
        char*   commonAvailTopic;      // Common Avail Topic expanded string

        //* MQTT Topic suffixes for Home Assistant MQTT supported entity platforms
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

        // Numeric (Hysterisis)
        static constexpr char _haNumericState[] = "/state";
        static constexpr char _haNumericCmd[] = "/set";      // incoming command topic

        
        //* JSON templates for Home Assistant MQTT configuration and base topic strings

        // Home Assistant MQTT water_heater templates
        static constexpr char BoilerConfigJsonTemplate[] =
            "{\n" // Parms: <base_topic>, <device-name>, <entity-name>, commonAvailTopic
                "'~' : '%0/water_heater/%2',\n"
                "'name': '%2',\n"
                "'modes': [\n"
                    "'off',\n"
                    "'eco',\n"
                    "'performance'\n"
                "],\n"

                "'avty_t' : '%3',\n"
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


        // Template for Home Assistant MQTT sensor (temperature) configuration
        static constexpr char ThermometerConfigJsonTemplate[] =
            "{\n" // Parms: <base_topic>, <device-name>, <entity-name>, commonAvailTopic
                "'~' : '%0/sensor/%2',\n"
                "'name': '%2',\n"
                "'dev_cla' : 'temperature',\n"
                "'unit_of_meas' : '°F',\n"
                "'avty_t' : '%3',\n"
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

        // Our three thermometers
        char*   ambientThermometerBaseTopic;          // Ambient Thermometer Base Topic expanded string
        char*   boilerInThermometerBaseTopic;         // Boiler In Thermometer Base Topic expanded string
        char*   boilerOutThermometerBaseTopic;        // Boiler Out Thermometer Base Topic expanded string
        int expandedMsgSizeOfAmbientThermometerConfigJson;
        int expandedMsgSizeOfBoilerInThermometerConfigJson;
        int expandedMsgSizeOfBoilerOutThermometerConfigJson;

        // Template for Home Assistant MQTT binary_sensor (running) configuration
        static constexpr char BinarySensorConfigJsonTemplate[] =
            "{\n" // Parms: <base_topic>, <device-name>, <entity-name>, commonAvailTopic
                "'~' : '%0/binary_sensor/%2',\n"
                "'name': '%2',\n"
                "'avty_t' : '%3',\n"
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


        // Template for Home Assistant MQTT sensor (enum) configuration. This for any "sensor" that wants to just publish
        // a value that is an enum (e.g. "ON" or "OFF")
        static constexpr char EnumTextSensorConfigJsonTemplate[] =
            "{\n" // Parms: <base_topic>, <device-name>, <entity-name>, , commonAvailTopic
                "'~' : '%0/sensor/%2',\n"
                "'name': '%2',\n"
                "'device_class' : 'enum',\n"
                "'avty_t' : '%3',\n"
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
            "{\n" // Parms: <base_topic>, <device-name>, <entity-name>, commonAvailTopic
                "'~' : '%0/button/%2',\n"
                "'name': '%2',\n"
                "'avty_t' : '%3',\n"
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


        // JSON template for Home Assistant MQTT numeric (Hysterisis) configuration
        static constexpr char HysterisisConfigJsonTemplate[] =
            "{\n" // Parms: <base_topic>, <device-name>, <entity-name>, commonAvailTopic
                "'~' : '%0/number/%2',\n"
                "'name': '%2',\n"
                "'device_class' : 'temperature',\n"
                "'unit_of_meas' : '°F',\n"
                "'avty_t' : '%3',\n"
                "'avty_tpl' : '{{ value_json }}',\n"
                "'stat_t' : '~/state',\n"
                "'command_topic' : '~/set',\n"
                "'min' : 0.01,\n"
                "'max' : 5.0,\n"
                "'step' : 0.01,\n"
                "'uniq_id' : '%2',\n"
                "'dev':\n"
                "{\n"
                    "'identifiers' : ['01'],\n"
                    "'name' : '%1'\n"
                "}\n"
            "}\n";

        // JSON template for Home Assistant MQTT base topic for numeric (Hysterisis) topic
        static constexpr char HysterisisBaseTopicJsonTemplate[] = "%0/number/%1"; // Parms: <base_topic>, <entity-name>
        char*   hysterisisBaseTopic;           // Hysterisis Base Topic expanded string
        int expandedMsgSizeOfHysterisisConfigJson;


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
            HaEntityDesc(_defaultRebootButtonName, ButtonConfigJsonTemplate, ButtonBaseTopicJsonTemplate, &rebootButtonBaseTopic, &expandedMsgSizeOfRebootButtonConfigJson),
            HaEntityDesc(_defaultStopButtonName, ButtonConfigJsonTemplate, ButtonBaseTopicJsonTemplate, &stopButtonBaseTopic, &expandedMsgSizeOfStopButtonConfigJson),
            HaEntityDesc(_defaultHysterisisName, HysterisisConfigJsonTemplate, HysterisisBaseTopicJsonTemplate, &hysterisisBaseTopic, &expandedMsgSizeOfHysterisisConfigJson)
        };
        int _entityDescCount = sizeof(_entityDescs) / sizeof(HaEntityDesc);

        //* Flash store for MQTT configuration
        FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase> mqttConfig;
            static_assert(PS_MQTTBrokerConfigBlkSize >= sizeof(FlashStore<HA_MqttConfig, PS_MQTTBrokerConfigBase>));
    } // namespace HA_Mqtt
} // namespace TinyBus

using namespace TinyBus::HA_Mqtt;


//** MQTT/HA related console command processors for configuration of the MQTT client to HA
CmdLine::Status ExitConfigHAMqtt(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    ((ConsoleTask *)Context)->Pop();
    return CmdLine::Status::Ok;
}

CmdLine::Status ShowMqttConfig(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    // Show the current MQTT configuration from the configuration record
    if (!mqttConfig.IsValid())
    {
        printf(CmdStream, "MQTT configuration is not valid\n");
        return CmdLine::Status::Ok;
    }

    if (mqttConfig.GetRecord().IsFullyConfigured() == false)
    {
        printf(CmdStream, "*MQTT configuration is not fully configured\n");
    }

    printf(CmdStream, "MQTT Broker IP: %s; Port#: %d\n",
           IPAddress(mqttConfig.GetRecord()._brokerIP).toString().c_str(),
           mqttConfig.GetRecord()._brokerPort);
    printf(CmdStream, "Client ID: '%s'; Username: '%s'; Password: '%s'\n",
           mqttConfig.GetRecord()._clientId,
           mqttConfig.GetRecord()._username,
           mqttConfig.GetRecord()._password);

    printf(CmdStream, "Base HA Topic: '%s'; Device Name: '%s'\n\n",
           mqttConfig.GetRecord()._baseHATopic,
           mqttConfig.GetRecord()._haDeviceName);

    return CmdLine::Status::Ok;
}

CmdLine::Status EraseMqttConfig(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    // Erase the current MQTT configuration
    mqttConfig.Erase();
    mqttConfig.Begin();
    $Assert(mqttConfig.IsValid() == false);

    return CmdLine::Status::Ok;
}

CmdLine::Status SetConfigVar(Stream &CmdStream, int Argc, char const **Args, void *Context)
{
    if (Argc < 2)
    {
        printf(CmdStream, "Usage: set <var> <value> or set ? for help\n");
        return CmdLine::Status::UnexpectedParameterCount;
    }

    if (strcmp(Args[1], "?") == 0)
    {
        printf(CmdStream, "MQTT/HA Configuration Variables:\n");
        printf(CmdStream, "   ip <ip> - Set the MQTT broker IP\n");
        printf(CmdStream, "   port <port> - Set the MQTT broker port number\n");
        printf(CmdStream, "   id <id>  - Set the MQTT client ID\n");
        printf(CmdStream, "   user <username> - Set the MQTT username\n");
        printf(CmdStream, "   password <password> - Set the MQTT password\n");
        printf(CmdStream, "   topic <topic> - Set the base HA topic\n");
        printf(CmdStream, "   name <device name> - Set the HA device name\n");
        return CmdLine::Status::Ok;
    }

    if (Argc < 3)
    {
        return CmdLine::Status::UnexpectedParameterCount;
    }

    if (strcmp(Args[1], "ip") == 0)
    {
        mqttConfig.GetRecord()._brokerIP = IPAddress(Args[2]);
        mqttConfig.Write();
    }
    else if (strcmp(Args[1], "port") == 0)
    {
        mqttConfig.GetRecord()._brokerPort = atoi(Args[2]);
        mqttConfig.Write();
    }
    else if (strcmp(Args[1], "id") == 0)
    {
        strncpy(mqttConfig.GetRecord()._clientId, Args[2], sizeof(mqttConfig.GetRecord()._clientId));
        mqttConfig.Write();
    }
    else if (strcmp(Args[1], "user") == 0)
    {
        strncpy(mqttConfig.GetRecord()._username, Args[2], sizeof(mqttConfig.GetRecord()._username));
        mqttConfig.Write();
    }
    else if (strcmp(Args[1], "password") == 0)
    {
        strncpy(mqttConfig.GetRecord()._password, Args[2], sizeof(mqttConfig.GetRecord()._password));
        mqttConfig.Write();
    }
    else if (strcmp(Args[1], "topic") == 0)
    {
        strncpy(mqttConfig.GetRecord()._baseHATopic, Args[2], sizeof(mqttConfig.GetRecord()._baseHATopic));
        mqttConfig.Write();
    }
    else if (strcmp(Args[1], "name") == 0)
    {
        strncpy(mqttConfig.GetRecord()._haDeviceName, Args[2], sizeof(mqttConfig.GetRecord()._haDeviceName));
        mqttConfig.Write();
    }
    else
    {
        printf(CmdStream, "Unknown MQTT/HA configuration variable: %s\n", Args[1]);
        return CmdLine::Status::InvalidParameter;
    }

    return CmdLine::Status::Ok;
}

CmdLine::ProcessorDesc haMqttCmdProcessors[] =
    {
        {SetConfigVar, "set", "Set a MQTT/HA configuration variables - set <var> <value> -or- set ? for help"},
        {EraseMqttConfig, "erase", "Erase the current MQTT/HA configuration"},
        {ShowMqttConfig, "show", "Show the current MQTT configuration"},
        {ExitConfigHAMqtt, "exit", "Exit to parent menu"},
};
int const LengthOfHaMqttCmdProcessors = sizeof(haMqttCmdProcessors) / sizeof(haMqttCmdProcessors[0]);




//** HA_MqttClient class implementation - Setup for the MQTT client task. 
//   This function is called from the main setup() function
void HA_MqttClient::setup()
{
    logger.Printf(Logger::RecType::Progress, "mqttClientTask: starting");

    // Initialize the MQTT configuration - if not already initialized
    mqttConfig.Begin();
    if (!mqttConfig.IsValid())
    {
        logger.Printf(Logger::RecType::Warning, "MQTT: Config not valid - initializing to defaults");
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
        logger.Printf(Logger::RecType::Progress, "MQTT: Config valid");
    }

    //** Build all expanded topic strings and compute sizes of the expanded HA entity /config JSON strings
    $Assert(mqttConfig.IsValid());

    PrintOutputCounter counter;

    //* Build the common avail topic string
    int sizeNeeded = ExpandJson(counter, _commonAvailTopicTemplate, mqttConfig.GetRecord()._haDeviceName) + 1;
    commonAvailTopic = new char[sizeNeeded];
    $Assert(commonAvailTopic != nullptr);

    BufferPrinter printer(commonAvailTopic, sizeNeeded);
    $Assert(ExpandJson(printer, _commonAvailTopicTemplate, mqttConfig.GetRecord()._haDeviceName) == (sizeNeeded - 1));
    $Assert(strlen(commonAvailTopic) == (sizeNeeded - 1));

    //* Helper to build expanded topic strings
    static auto BuildTopicString = [](
        const char &Template, 
        char *&Result, 
        const char *BaseTopic, 
        const char *EntityName) -> int
    {
        PrintOutputCounter counter;
        size_t size = ExpandJson(counter, &Template, BaseTopic, EntityName); // Get size of expanded string
        $Assert(size > 0);                                                   // Error in template

        Result = new char[size + 1]; // Leave room for null terminator
        $Assert(Result != nullptr);  // Out of memory
        BufferPrinter printer(Result, size);

        $Assert(ExpandJson(printer, &Template, BaseTopic, EntityName) == size); // Expand template into buffer
        return size;
    };

    //* Build each topic base string
    for (int i = 0; i < _entityDescCount; i++)
    {
        HaEntityDesc &desc = _entityDescs[i];
        BuildTopicString(
            *desc._BaseTopicJsonTemplate, 
            *desc._BaseTopicResult, 
            mqttConfig.GetRecord()._baseHATopic, 
            desc._EntityName);
    }

    //* Compute sizes of the expanded HA entity /config JSON strings - this allows the use of a streaming for of
    //  MqttClient::BeginMessage(); thus reducing memory usage by avoiding the need to allocate a larger buffer for
    //  the JSON string
    for (int i = 0; i < _entityDescCount; i++)
    {
        HaEntityDesc &desc = _entityDescs[i];

        *desc._ExpandedMsgSizeResult = ExpandJson(
            counter,
            desc._ConfigJsonTemplate,
            mqttConfig.GetRecord()._baseHATopic,
            mqttConfig.GetRecord()._haDeviceName,
            desc._EntityName,
            commonAvailTopic);
        $Assert(*desc._ExpandedMsgSizeResult > 0);
    }
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
            logger.Printf(Logger::RecType::Warning, "MQTT: Failed to begin message");
            return false;
        }

        // Write the expanded /config message body JSON string directly into the message stream
        size_t expandedSize = ExpandJson(MqttClient, ConfigJsonPrototype, BaseTopic, DeviceName, EntityName, commonAvailTopic);
        if (expandedSize != ExpandedMsgSize)
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: Expanded size of /config message body JSON string is incorrect");
            return false;
        }

        status = MqttClient.endMessage();
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: endMessage() failed");
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
            logger.Printf(Logger::RecType::Warning, "MQTT: Failed to begin message");
            return false;
        }

        size_t size = MqttClient.print("\"online\"");
        if (size == 0)
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: Failed to write /avail message body JSON string");
            return false;
        }

        status = MqttClient.endMessage();
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: endMessage() failed");
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
            logger.Printf(Logger::RecType::Warning, "MQTT: SendPropertyMsg: Failed to begin message");
            return false;
        }

        // Write the property message body JSON string directly into the message stream
        size_t size = printf(MqttClient, "%0.2f", PropertyValue);
        if (size == 0)
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: SendPropertyMsg: Failed to write property message body JSON string");
            return false;
        }

        status = MqttClient.endMessage();
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: SendPropertyMsg: endMessage() failed");
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
            logger.Printf(Logger::RecType::Warning, "MQTT: SendPropertyMsg: Failed to begin message");
            return false;
        }

        // Write the property message body JSON string directly into the message stream
        size_t size = printf(MqttClient, "\"%s\"", PropertyValue);
        if (size == 0)
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: SendPropertyMsg: Failed to write property message body JSON string");
            return false;
        }

        status = MqttClient.endMessage();
        if (!status)
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: SendPropertyMsg: endMessage() failed");
            return false;
        }

        return true;
    };
    
    //* Monitor the Boiler State Machine for changes in state and send any changes to Home Assistant. All updated
    //  properties are sent to Home Assistant as separate messages from this function.
    static auto MonitorBoiler = [](MqttClient & MqttClient, bool DoForce = false) -> bool
    {
        // This nested state machine is used to control the sending of property messages to Home Assistant as
        // they change or when forced to do so by the caller. The state machine is used to ensure that the
        // property messages are sent in a timely manner and that the MQTT client is not overwhelmed with
        // messages. The state machine is also used to ensure the MQTT client work does not overwhelm the
        // other network related tasks in the system.

        enum class State
        {
            CalcWork,
            SendUpdates
        };
        static StateMachineState<State> state(State::CalcWork);

        // There are two phases to the state machine. The first phase is to compute the work that needs to be done
        // and the second phase is to incrementally send the updates to Home Assistant.

        if (DoForce)
        {
            // Callin in with DoForce set to true will force the state machine to do a complete cycle of work and
            // to abort any current work in progress.
            state.ChangeState(State::CalcWork);
        }

        switch ((State)state)
        {
            static BoilerControllerTask::TempertureState tempState = {0, 0.0, 0.0, 0.0, 0.0, 0.0, false};
            static BoilerControllerTask::StateMachineState lastHeaterState = BoilerControllerTask::StateMachineState(-1);
            static BoilerControllerTask::FaultReason lastFaultReason = BoilerControllerTask::FaultReason(-1);
            static BoilerControllerTask::BoilerMode lastBoilerMode = BoilerControllerTask::BoilerMode(-1);
            static BoilerControllerTask::TargetTemps lastTargetTemps = {-999.99, -999.99};

            static bool doBoilerInTemp = false;
            static bool doBoilerThermometer = false;
            static bool doBoilerOutTemp = false;
            static bool doAmbientTemp = false;
            static bool doHysterisis = false;
            static bool doHeaterState = false;
            static bool doBoilerState = false;
            static bool doFaultReason = false;
            static bool doSetPoint = false;
            static bool doBoilerMode = false;
            
            // Compute the work that needs to be done - taking into account the the last time the work was done
            // for certain items. This is to ensure that the MQTT system is not overwhelmed with messages.
            case State::CalcWork:
            {
                static Timer timer(1000);
                static uint32_t lastSeq = 0;

                doBoilerInTemp = DoForce;
                doBoilerThermometer = DoForce;
                doBoilerOutTemp = DoForce;
                doAmbientTemp = DoForce;
                doHysterisis = DoForce;
                doHeaterState = DoForce;
                doBoilerState = DoForce;
                doFaultReason = DoForce;
                doSetPoint = DoForce;
                doBoilerMode = DoForce;

                //* Compute and low frequency work
                if (timer.IsAlarmed() || DoForce)
                {
                    auto const seq = boilerControllerTask.GetHeaterStateSequence();
                    bool force = (lastSeq == 0) || DoForce;

                    if ((seq != lastSeq) || force)
                    {
                        BoilerControllerTask::TempertureState newState;
                        boilerControllerTask.GetTempertureState(newState);

                        doBoilerInTemp = (force || (newState._boilerInTemp != tempState._boilerInTemp));
                        doBoilerThermometer = doBoilerInTemp;
                        doBoilerOutTemp = (force || (newState._boilerOutTemp != tempState._boilerOutTemp));
                        doAmbientTemp = (force || (newState._ambiantTemp != tempState._ambiantTemp));
                        doHysterisis = (force || (newState._hysteresis != tempState._hysteresis));
                        doHeaterState = (force || (newState._heaterOn != tempState._heaterOn));

                        tempState = newState;
                        lastSeq = tempState._sequence;
                    }

                    timer.SetAlarm(1000);
                }

                //* Now compute any high frequency work

                // Cause sending the boiler state if it has changed or if forced - translate the state to a string
                BoilerControllerTask::StateMachineState currHeaterState = boilerControllerTask.GetStateMachineState();

                if ((currHeaterState != lastHeaterState) || DoForce)
                {
                    doBoilerState = true;
                    lastHeaterState = currHeaterState;
                }

                // Cause sending  the fault reason if it has changed or if forced - translate the fault reason to a string
                BoilerControllerTask::FaultReason currFaultReason = boilerControllerTask.GetFaultReason();

                if ((currFaultReason != lastFaultReason) || DoForce)
                {
                    doFaultReason = true;
                    lastFaultReason = currFaultReason;
                }

                // Cause sending the current boiler mode if needed
                BoilerControllerTask::BoilerMode currBoilerMode = boilerControllerTask.GetMode();

                if ((currBoilerMode != lastBoilerMode) || DoForce)
                {
                    doBoilerMode = true;
                    lastBoilerMode = currBoilerMode;
                }

                // Cause sending the current setPoint temperature if it has changed or if forced
                BoilerControllerTask::TargetTemps currTargetTemps;
                boilerControllerTask.GetTargetTemps(currTargetTemps);

                if ((currTargetTemps._setPoint != lastTargetTemps._setPoint) || DoForce)
                {
                    doSetPoint = true;
                    lastTargetTemps._setPoint = currTargetTemps._setPoint;
                }

                state.ChangeState(State::SendUpdates);
                return(true);
            }

            // Send any planned updates to Home Assistant - computed in the previous state
            case State::SendUpdates:
            {
                enum class SendState    // sequence of property messages to send
                {
                    SendBoilerInTemp,
                    SendBoilerThermometer,
                    SendBoilerOutTemp,
                    SendAmbientTemp,
                    SendHysterisis,
                    SendHeaterState,
                    SendBoilerState,
                    SendFaultReason,
                    SendSetPoint,
                    SendBoilerMode,
                    Done
                };
                static StateMachineState<SendState> sendState(SendState::SendBoilerInTemp);

                if (sendState.IsFirstTime())
                {
                    sendState.ChangeState(SendState::SendBoilerInTemp);
                }

                // cycle thru each possible property message to send. Just fall thru to the next state
                // if nothing to send.
                switch ((SendState)sendState)
                {
                    case SendState::SendBoilerInTemp:
                    {
                        if (doBoilerInTemp)
                        {
                            doBoilerInTemp = false;
                            sendState.ChangeState(SendState::SendBoilerThermometer);
                            return SendPropertyMsg(MqttClient, boilerBaseTopic, _haWHCurrTemp, $CtoF(tempState._boilerInTemp));
                        }
                    }
                    case SendState::SendBoilerThermometer:
                    {
                        if (doBoilerThermometer)
                        {
                            doBoilerThermometer = false;
                            sendState.ChangeState(SendState::SendBoilerOutTemp);
                            return SendPropertyMsg(MqttClient, boilerInThermometerBaseTopic, _haSensorTemp, $CtoF(tempState._boilerInTemp));
                        }
                    }
                    case SendState::SendBoilerOutTemp:
                    {
                        if (doBoilerOutTemp)
                        {
                            doBoilerOutTemp = false;
                            sendState.ChangeState(SendState::SendAmbientTemp);
                            return SendPropertyMsg(MqttClient, boilerOutThermometerBaseTopic, _haSensorTemp, $CtoF(tempState._boilerOutTemp));
                        }
                    }
                    case SendState::SendAmbientTemp:
                    {
                        if (doAmbientTemp)
                        {
                            doAmbientTemp = false;
                            sendState.ChangeState(SendState::SendHysterisis);
                            return SendPropertyMsg(MqttClient, ambientThermometerBaseTopic, _haSensorTemp, $CtoF(tempState._ambiantTemp));
                        }
                    }
                    case SendState::SendHysterisis:
                    {
                        if (doHysterisis)
                        {
                            doHysterisis = false;
                            sendState.ChangeState(SendState::SendHeaterState);
                            return SendPropertyMsg(MqttClient, hysterisisBaseTopic, _haNumericState, $CDiffToF(tempState._hysteresis));
                        }
                    }
                    case SendState::SendHeaterState:
                    {
                        if (doHeaterState)
                        {
                            doHeaterState = false;
                            sendState.ChangeState(SendState::SendBoilerState);
                            return SendPropertyMsgStr(MqttClient, heaterStateBinarySensorBaseTopic, _haBinarySensorState, tempState._heaterOn ? "On" : "Off");
                        }
                    }
                    case SendState::SendBoilerState:
                    {
                        if (doBoilerState)
                        {
                            doBoilerState = false;
                            sendState.ChangeState(SendState::SendFaultReason);
                            return SendPropertyMsgStr(MqttClient, boilerStateSensorBaseTopic, _haSensorEnum, BoilerControllerTask::GetStateMachineStateDescription(lastHeaterState));
                        }
                    }
                    case SendState::SendFaultReason:
                    {
                        if (doFaultReason)
                        {
                            doFaultReason = false;
                            sendState.ChangeState(SendState::SendSetPoint);
                            return SendPropertyMsgStr(MqttClient, faultReasonSensorBaseTopic, _haSensorEnum, BoilerControllerTask::GetFaultReasonDescription(lastFaultReason));
                        }
                    }
                    case SendState::SendSetPoint:
                    {
                        if (doSetPoint)
                        {
                            doSetPoint = false;
                            sendState.ChangeState(SendState::SendBoilerMode);
                            return SendPropertyMsg(MqttClient, boilerBaseTopic, _haWHSetpoint, $CtoF(lastTargetTemps._setPoint));
                        }
                    }
                    case SendState::SendBoilerMode:
                    {
                        if (doBoilerMode)
                        {
                            doBoilerMode = false;
                            sendState.ChangeState(SendState::Done);
                            return SendPropertyMsgStr(MqttClient, boilerBaseTopic, _haWHMode, BoilerControllerTask::GetBoilerModeDescription(lastBoilerMode));
                        }
                    }
                    case SendState::Done:
                    {
                        state.ChangeState(State::CalcWork);
                        return true;
                    }
                    default:
                    {
                        $FailFast();
                    }
                }
            }
            break;

            default:
            {
                $FailFast();
            }
            break;
        }
    };

    //** Support for subscription notification and handling of Home Assistant topics
    using NotificationHandler = std::function<bool(const char *)>;  // A subscription notification handler function type

    //* Notification handlers for each MQTT subscription topic
    // Mode Set command
    static NotificationHandler HandleWHModeSet = [](const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "MQTT: Received WH Mode Set command: %s", Payload);
        BoilerControllerTask::BoilerMode mode = BoilerControllerTask::GetBoilerModeFromDescription(Payload);
        if (mode == BoilerControllerTask::BoilerMode::Undefined)
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: Invalid WH Mode Set command value: %s", Payload);
            return false;
        }

        // Update the mode in the configuration and in the controller
        boilerConfig.GetRecord()._mode = mode;
        boilerConfig.Write();
        boilerConfig.Begin();
        if (!boilerConfig.IsValid())
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: Failed to write WH Mode Set command value to config: %s", Payload);
            return false;
        }

        boilerControllerTask.SetMode(mode);

        return true;
    };

    // Set target temperature Helper
    static auto SetTargetTempAndHysterisis = [](float SetpointInC, float HysterisisInC) -> bool
    {
        BoilerControllerTask::TargetTemps targetTemps;
        boilerControllerTask.GetTargetTemps(targetTemps);

        // Update the setpoint in the configuration and in the controller
        boilerConfig.GetRecord()._setPoint = SetpointInC;
        boilerConfig.GetRecord()._hysteresis = HysterisisInC;
        boilerConfig.Write();
        boilerConfig.Begin();
        if (!boilerConfig.IsValid())
        {
            logger.Printf(Logger::RecType::Warning, "MQTT: SetTargetTemp: Failed to write to config");
            return false;
        }

        targetTemps._setPoint = SetpointInC;
        targetTemps._hysteresis = HysterisisInC;
        boilerControllerTask.SetTargetTemps(targetTemps);

        return true;
    };
    // Setpoint Set command
    static NotificationHandler HandleWHSetpointSet = [](const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "MQTT: Received WH Setpoint Set command: %s", Payload);
        float setpoint = $FtoC(atof(Payload));

        return SetTargetTempAndHysterisis(setpoint, boilerConfig.GetRecord()._hysteresis);
    };

    // Hysterisis Set command
    static NotificationHandler HandleHysterisisSetCmd = [](const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "MQTT: Received Hysterisis Set command: %s", Payload);
        float hysterisis = $FDiffToC(atof(Payload));

        return SetTargetTempAndHysterisis(boilerConfig.GetRecord()._setPoint, hysterisis);
    };

    // Reset Button command
    static NotificationHandler HandleResetButtonCmd = [](const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "MQTT: Received Reset Button Event: %s", Payload);
        boilerControllerTask.ResetIfSafe();
        return true;
    };

    // Start Button command
    static NotificationHandler HandleStartButtonCmd = [](const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "MQTT: Received Start Button Event: %s", Payload);
        boilerControllerTask.StartIfSafe();
        return true;
    };

    // Stop Button command
    static NotificationHandler HandleStopButtonCmd = [](const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "MQTT: Received Stop Button Event: %s", Payload);
        boilerControllerTask.StopIfSafe();
        return true;
    };

    // Reboot Button command
    static NotificationHandler HandleRebootButtonCmd = [](const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "MQTT: Received Reboot Button Event: %s", Payload);
        logger.Printf(Logger::RecType::Progress, "MQTT: ***rebooting***");
        delay(1000);
        NVIC_SystemReset();
        $FailFast();
        return true;
    };

    // Home Assistant Integration Available event
    static bool HAIntgAvailCameTrue = false;

    static NotificationHandler HandleHAIntgAvailEvent = [](const char *Payload) -> bool
    {
        logger.Printf(Logger::RecType::Info, "MQTT: Received HA Intg Avail Event: %s", Payload);
        if (strcmp(Payload, "online") == 0)
        {
            HAIntgAvailCameTrue = true;     // Cause reconnect to broker
        }
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
        SubscribedTopic(mqttConfig.GetRecord()._baseHATopic, _haIntgAvailSuffix, HandleHAIntgAvailEvent),
        SubscribedTopic(boilerBaseTopic, _haWHModeSet, HandleWHModeSet),
        SubscribedTopic(boilerBaseTopic, _haWHSetpointSet, HandleWHSetpointSet),
        SubscribedTopic(resetButtonBaseTopic, _haButtonCmd, HandleResetButtonCmd),
        SubscribedTopic(startButtonBaseTopic, _haButtonCmd, HandleStartButtonCmd),
        SubscribedTopic(stopButtonBaseTopic, _haButtonCmd, HandleStopButtonCmd),
        SubscribedTopic(rebootButtonBaseTopic, _haButtonCmd, HandleRebootButtonCmd),
        SubscribedTopic(hysterisisBaseTopic, _haNumericCmd, HandleHysterisisSetCmd)
    };
    static int subscribedTopicCount = sizeof(subscribedTopics) / sizeof(SubscribedTopic);

    //* Notification dispatcher for incoming MQTT messages - called from the onMessage() handler lambda
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

    static shared_ptr<Client> netClient = move(NetworkTask::CreateClient());
    static MqttClient mqttClient(*netClient.get());

    enum class State
    {
        WaitForNetConnection,
        ConnectingToBroker,
        SendSubscriptions,
        SendConfigs,
        SendOnlineAvailMsg,
        Connected
    };
    static StateMachineState<State> state(State::WaitForNetConnection);

    switch ((State)state)
    {
        //* Delay while waiting for network to connect and be available
        case State::WaitForNetConnection:
        {
            enum class NetworkStatus
            {
                Unknown,
                Connected,
                Disconnected
            };
            static StateMachineState<NetworkStatus> networkState(NetworkStatus::Unknown);

            if (state.IsFirstTime())
            {
                networkState.ChangeState(NetworkStatus::Unknown);
            }

            // inner state machine to handle the network connection, delays, and retries
            switch ((NetworkStatus)networkState)
            {
                static Timer delayTimer;

                case NetworkStatus::Unknown:
                {
                    mqttClient.stop();
                    if (network.IsAvailable())
                    {
                        // Network is already connected - delay - allow some settling time
                        delayTimer.SetAlarm(4000);
                        networkState.ChangeState(NetworkStatus::Connected);
                        return;
                    }

                    logger.Printf(Logger::RecType::Progress, "MQTT: Waiting for network connection - delay 5 secs");
                    delayTimer.SetAlarm(5000);          // don't hammer the net
                    networkState.ChangeState(NetworkStatus::Disconnected);
                }
                break;

                // Network is connected - delay before connecting to broker
                case NetworkStatus::Connected:
                {
                    if (delayTimer.IsAlarmed())
                    {
                        state.ChangeState(State::ConnectingToBroker);
                    }
                }
                break;

                // Network is disconnected - delay before retrying net connection
                case NetworkStatus::Disconnected:
                {
                    if (delayTimer.IsAlarmed())
                    {
                        networkState.ChangeState(NetworkStatus::Unknown);
                    }
                }
                break;
            }
        }
        break;

        //* Connect to the MQTT broker; if successful, then send subscriptions to Home Assistant
        case State::ConnectingToBroker:
        {
            static IPAddress brokerIP;

            logger.Printf(Logger::RecType::Progress, "MQTT: Connecting to Broker: IP: '%s' Port: '%d'", 
                IPAddress(mqttConfig.GetRecord()._brokerIP).toString().c_str(), 
                mqttConfig.GetRecord()._brokerPort);

            brokerIP = mqttConfig.GetRecord()._brokerIP;
            mqttClient.stop();
            mqttClient.setId(mqttConfig.GetRecord()._clientId);
            mqttClient.setUsernamePassword(mqttConfig.GetRecord()._username, mqttConfig.GetRecord()._password);

            // Add LWT topic; this will change all /avail topics all the entities at the same time
            mqttClient.beginWill(commonAvailTopic, sizeof(_haAvailOffline), true, 1);
            printf(mqttClient, "\"%s\"", _haAvailOffline);
            mqttClient.endWill();

            if (!mqttClient.connect(brokerIP, mqttConfig.GetRecord()._brokerPort))
            {
                // failed for some reason
                logger.Printf(Logger::RecType::Critical, "MQTT: Failed to connect to Broker - delaying 5 secs and retrying");
                state.ChangeState(State::WaitForNetConnection);
                return;
            }

            logger.Printf(Logger::RecType::Progress, "MQTT: Connected to Broker - sending subscriptions to Home Assistant");

            // Set the message handler for incoming messages
            HAIntgAvailCameTrue = false;
            mqttClient.onMessage([](int messageSize) -> void { OnMessage(messageSize, mqttClient); } );

            // Subscribe to all the Home Assistant incoming topics for each entity
            state.ChangeState(State::SendSubscriptions);
        }
        break;

        //* Send all subscriptions to Home Assistant
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
                char topic[subscribedTopics[ix]._baseTopicSize + subscribedTopics[ix]._topicSuffixSize + 1];
                memcpy(topic, subscribedTopics[ix]._baseTopic, subscribedTopics[ix]._baseTopicSize);
                memcpy(&topic[subscribedTopics[ix]._baseTopicSize], subscribedTopics[ix]._topicSuffix, subscribedTopics[ix]._topicSuffixSize);
                topic[subscribedTopics[ix]._baseTopicSize + subscribedTopics[ix]._topicSuffixSize] = '\0';

                logger.Printf(Logger::RecType::Progress, "MQTT: Subscribing to topic: %s", topic);
                if (!mqttClient.subscribe(topic))
                {
                    logger.Printf(Logger::RecType::Warning, "MQTT: Failed to subscribe to topic: %s - restarting", topic);
                    state.ChangeState(State::WaitForNetConnection);
                    return;
                }

                ix++;
            }
            else
            {
                logger.Printf(Logger::RecType::Progress, "MQTT: All subscriptions sent to Home Assistant - now sending /config messages");
                state.ChangeState(State::SendConfigs);
            }
        }
        break;

        //* Send all /config messages to Home Assistant
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
                logger.Printf(Logger::RecType::Progress, "MQTT: Sending /config message for entity: %s", desc._EntityName);
                if (!SendConfigJSON(
                        mqttClient,
                        mqttConfig.GetRecord()._baseHATopic,
                        *desc._BaseTopicResult,
                        mqttConfig.GetRecord()._haDeviceName,
                        desc._EntityName, desc._ConfigJsonTemplate,
                        *desc._ExpandedMsgSizeResult))
                {
                    logger.Printf(Logger::RecType::Warning, "MQTT: Failed to send /config message for entity: %s - restarting", desc._EntityName);
                    state.ChangeState(State::WaitForNetConnection);
                    return;
                }
            }
            else
            {
                logger.Printf(Logger::RecType::Progress, "MQTT: All /config messages sent to Home Assistant - now sending /avail message after 2sec delay");
                state.ChangeState(State::SendOnlineAvailMsg);
            }

            ix++;
        }
        break;

        //* Send the /avail message to Home Assistant after a short delay
        case State::SendOnlineAvailMsg:
        {
            enum class AvailState
            {
                Wait2Secs,
                SendAvail
            };
            static StateMachineState<AvailState> availState(AvailState::Wait2Secs);

            // Send the /avail message to Home Assistant after a short delay - race issue with Home Assistant
            if (state.IsFirstTime())
            {
                availState.ChangeState(AvailState::Wait2Secs);  // reset inner state machine on new state change to this state
            }

            switch ((AvailState)availState)
            {
                // Delay before sending /avail message for 2secs
                case AvailState::Wait2Secs:     // Delay before sending /avail message for 2secs
                {
                    static Timer twoSecTimer;

                    if (availState.IsFirstTime())
                    {
                        twoSecTimer.SetAlarm(2000);
                    }
                    if (twoSecTimer.IsAlarmed())
                    {
                        availState.ChangeState(AvailState::SendAvail);
                    }
                    else
                    {
                        mqttClient.poll(); // Check for incoming messages
                    }
                }
                break;

                // Send the /avail message to Home Assistant
                case AvailState::SendAvail:
                {
                    // Tell Home Assistant that all entities are available
                    logger.Printf(Logger::RecType::Progress, "MQTT: Sending /avail message to Home Assistant");
                    if (!SendOnlineAvailMsg(mqttClient))
                    {
                        logger.Printf(Logger::RecType::Warning, "Failed to send /avail messages to Home Assistant - restarting");
                        state.ChangeState(State::WaitForNetConnection);
                        return;
                    }

                    logger.Printf(Logger::RecType::Progress, "MQTT: /avail message sent to Home Assistant - now monitoring for incoming messages");
                    state.ChangeState(State::Connected);
                }
                break;

                default:
                {
                    $FailFast();
                }
            }
        }
        break;

        //* Main idle state from an MQTT client perspective - monitor for incoming messages and send outgoing messages
        //  for any changed properties
        case State::Connected:
        {
            // Check for lost connection to MQTT Broker and restart SM if lost
            if (!mqttClient.connected())
            {
                logger.Printf(Logger::RecType::Warning, "MQTT: Lost connection to Broker - restarting");
                state.ChangeState(State::WaitForNetConnection);
                return;
            }

            // Check for HAIntgAvailCameTrue and restart SM if true
            if (HAIntgAvailCameTrue)
            {
                HAIntgAvailCameTrue = false;
                logger.Printf(Logger::RecType::Progress, "MQTT: HAIntgAvailCameTrue - restarting");
                state.ChangeState(State::WaitForNetConnection);
                return;
            }

            mqttClient.poll(); // Check for incoming messages

            // Publish to Home Assistant any change of state for the Boiler for each entity - force all properties to be sent
            // if this state was just entered from another state
            if (!MonitorBoiler(mqttClient, state.IsFirstTime()))
            {
                logger.Printf(Logger::RecType::Warning, "MQTT: Failed in MonitorBoiler - restarting");
                state.ChangeState(State::WaitForNetConnection);
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


HA_MqttClient haMqttClient;
