// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// LedMatrixTask implementation

#include "SpaHeaterCntl.hpp"
#include "LedMatrixTask.hpp"

 LedMatrixTask   matrixTask(Serial, 50);

LedMatrixTask::LedMatrixTask(Stream& Output, uint8_t ScrollTimeInMS)
    :   _output(Output),
        _scrollTimeInMS(ScrollTimeInMS),
        _doScrollDisplay(false)
{  
}

LedMatrixTask::~LedMatrixTask() 
{ 
    $FailFast(); 
}

void LedMatrixTask::setup()
{
    _output.println("LedMatricTask is Active");
    _matrix.begin();
}

void LedMatrixTask::loop() 
{
    if (_doScrollDisplay) 
    {
      _matrix.beginDraw();
      _matrix.stroke(0xFFFFFFFF);
      _matrix.textScrollSpeed(_scrollTimeInMS);
      _matrix.textFont(Font_4x6);
      _matrix.beginText(0, 1, 0xFFFFFF);
      _matrix.println(_text);
      _matrix.endText(SCROLL_LEFT);
      _matrix.endDraw();
    }
}

void LedMatrixTask::PutString(char* Text)
{
    _doScrollDisplay = false;
    _text = Text;

    if (_text.length() < 4)
    {
        _matrix.beginDraw();
        _matrix.stroke(0xFFFFFFFF);
        _matrix.textFont(Font_4x6);
        _matrix.beginText(0, 1, 0xFFFFFF);
        _matrix.print("   \r");
        _matrix.print(_text);
        _matrix.endText();
        _matrix.endDraw();
    }
    else
    {
        int padLength = 0; // _text.length();

        if (padLength > 0)
        {
            for (int i = 0; i < padLength; i++) _text += " ";
        }
        _doScrollDisplay = true;
    }
}


