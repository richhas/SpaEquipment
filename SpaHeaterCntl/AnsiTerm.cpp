/*  Common ANSI terminal control library implementation

    (C) 2020 TinyBus

*/

#include <Arduino.h>
#include "AnsiTerm.h"

// Consts used in imp
char const* AnsiTerm::CSI = "\x01B[";

void AnsiTerm::SetCursPos(Stream& Sink, int Row, int Col)
{
    Sink.write(CSI);
    Sink.print(Row);
    Sink.print(";");
    Sink.print(Col);
    Sink.print('H');
}

void AnsiTerm::EraseDisplay(Stream& Sink, int Mode)
{
     Sink.write(CSI);
     Sink.print(Mode);
     Sink.print('J');
}

void AnsiTerm::SingleControl(Stream& Sink, int Parameter, char const Code)
{
     Sink.write(CSI);
     Sink.print(Parameter);
     Sink.print(Code);
}

bool AnsiTerm::GetCursPos(Stream& Sink, int& Row, int& Col)
{
    Row = 0;
    Col = 0;

    // Do our best to clear the link 
    Sink.flush();
    while (Sink.available() > 0)
    {
        Sink.read();
    }

    Sink.write(CSI);
    Sink.print("6n");
    Sink.flush();

    // Parse for status report 
    if (Sink.find((char*)CSI, 2) == false)
    {
        return false;
    }

    long row = Sink.parseInt();
    if ((row < 1) || (row > INT16_MAX))
    {
        return false;
    }

    if (Sink.find(';') == false)
    {
        return false;
    }

    long col = Sink.parseInt();
    if ((col < 1) || (col > INT16_MAX))
    {
        return false;
    }

    if (Sink.find('R') == false)
    {
        return false;
    }

    // we have a good report - stuff results
    Row = row;
    Col = col;
    return true;
}

void AnsiTerm::SetSGR(Stream& Sink, char const* SGR)
{
    Sink.write(CSI);

    while ((*SGR) != 0)
    {
        Sink.print(*SGR);
        SGR++;
    }
    Sink.print('m');
    Sink.flush();
}

void AnsiTerm::SetColor(Stream& Sink, int Foreground, int Background)
{
    Sink.write(CSI);
    Sink.print("38;5;");
    Sink.print(Foreground);
    Sink.print(";48;5;");
    Sink.print(Background);
    Sink.print("m");
    Sink.flush();
}

void AnsiTerm::SendControl(Stream& Sink, char const* CntlStr)
{
    Sink.write(CSI);
    Sink.print(CntlStr);
    Sink.flush();
}

void AnsiTerm::CursOn(Stream& Sink)
{
     AnsiTerm::SendControl(Serial1, "?25h");
}

void AnsiTerm::CursOff(Stream& Sink)
{
    AnsiTerm::SendControl(Serial1, "?25l");
}


