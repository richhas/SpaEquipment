#include <DS18B20.h>

DS18B20 ds(2);

void setup() 
{
    Serial.begin(9600);
}

void loop() 
{
    while (ds.selectNext()) 
    {
        uint8_t type = ds.getFamilyCode();

        if ((type != MODEL_DS18S20) && (type != MODEL_DS1822) && (type != MODEL_DS18B20))
            return;

        uint64_t    addr;
        {
            uint8_t address[8];
            ds.getAddress(address);

            addr = *((uint64_t*)(&address[0]));
        }

        uint8_t res = ds.getResolution();
        float temp = ds.getTempC();

        char        s[64];
        sprintf(&s[0], "%08lX%08lX;%02X;%02X;", (uint32_t)(addr>>32), (uint32_t)addr, type, res);
        Serial.print(s);
        Serial.println(temp);
        Serial.flush();
    }
}