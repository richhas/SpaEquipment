#include <DS18B20.h>


DS18B20 ds(2);

void ResetISR() 
{
    asm volatile ("jmp 0");         // Cause software reset
}

void setup() 
{
    Serial.begin(9600);
    delay(1000);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // Config pin 3 for a reset interrupt
    pinMode(3, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(3), ResetISR, FALLING);
}


void loop() 
{   
    delay(20);

    digitalWrite(LED_BUILTIN, true);
    Serial.println("ESTART");
    while (ds.selectNext()) 
    {
        uint8_t type = ds.getFamilyCode();

        if ((type != MODEL_DS18S20) && (type != MODEL_DS1822) && (type != MODEL_DS18B20))
        {
            return;
        }

        uint64_t    addr;
        {
            uint8_t address[8];
            ds.getAddress(address);

            addr = *((uint64_t*)(&address[0]));
        }

        uint8_t res = ds.getResolution();
        float temp = ds.getTempC();
        const uint32_t& tempEnc = ((uint32_t)temp);

        static char hexChars[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

        static auto ByteToAsciiHex = [] (uint8_t Byte, char* Out) -> void
        {
            *Out = hexChars[(Byte>>4) & 0x0F];
            *(Out+1) = hexChars[(Byte >> 0) & 0x0F];
        };

        static auto ToAscii = [] (uint32_t Uint32, char* Out) -> void
        {
            ByteToAsciiHex(uint8_t(Uint32 >> 24), Out);
            ByteToAsciiHex(uint8_t(Uint32 >> 16), Out+2);
            ByteToAsciiHex(uint8_t(Uint32 >> 8), Out+4);
            ByteToAsciiHex(uint8_t(Uint32 >> 0), Out+6);
        };

      //  XXXXXXXXXXXXXXXX;XX;XX;FFFFFFFF<\n>
      //  01234567890123456789012345678901
        static char        s[32];
        // sprintf(&s[0], "%08lX%08lX;%02X;%02X;", (uint32_t)(addr >> 32), (uint32_t)addr, type, res);

        ToAscii((uint32_t)(addr>>32), &s[0]);
        ToAscii((uint32_t)(addr), &s[8]);
        s[16] = ';';
        ByteToAsciiHex(type, &s[17]);
        s[19] = ';';
        ByteToAsciiHex(res, &s[20]);
        s[22] = ';';
        ToAscii(tempEnc, &s[23]);
        s[31] = 0;
        Serial.print(s);
        Serial.println();
        Serial.flush();
    }
    Serial.println("ESTOP");
    digitalWrite(LED_BUILTIN, false);
}
