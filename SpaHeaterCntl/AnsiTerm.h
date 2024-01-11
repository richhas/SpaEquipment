/*  Common ANSI terminal control library definitions

    (C) 2020 TinyBus

*/

#include <Arduino.h>

class AnsiTerm
{
public:
    static void SetCursPos(Stream& Sink, int Row, int Col);
    static void EraseDisplay(Stream& Sink, int Mode);
    static void EraseToEnd(Stream& Sink) 
        { EraseDisplay(Sink, 0); }

    static void EraseToBeginning(Stream& Sink) 
        { EraseDisplay(Sink, 1); }

    static void EraseAll(Stream& Sink) 
        { 
            SetCursPos(Sink, 1, 1);
            EraseDisplay(Sink, 3); 
            EraseDisplay(Sink, 0); 
        }

    static void CursUp(Stream& Sink, int PosCount = 1)
        { SingleControl(Sink, PosCount, 'A'); }

    static void CursDown(Stream& Sink, int PosCount = 1)
        { SingleControl(Sink, PosCount, 'B'); }

    static void CursFwd(Stream& Sink, int PosCount = 1)
        { SingleControl(Sink, PosCount, 'C'); }

    static void CursBack(Stream& Sink, int PosCount = 1)
        { SingleControl(Sink, PosCount, 'D'); }

    static void CursNextLine(Stream& Sink, int PosCount = 1)
        { SingleControl(Sink, PosCount, 'E'); }

    static void CursPrevLine(Stream& Sink, int PosCount = 1)
        { SingleControl(Sink, PosCount, 'F'); }

    static void SetCursColAbs(Stream& Sink, int Pos)
        { SingleControl(Sink, Pos, 'G'); }

    static void EraseToEOL(Stream& Sink)
        { SingleControl(Sink, 0, 'K'); }

    static void EraseToBOL(Stream& Sink)
        { SingleControl(Sink, 1, 'K'); }

    static void EraseLine(Stream& Sink)
        { SingleControl(Sink, 2, 'K'); }

    static bool GetCursPos(Stream& Sink, int& Row, int& Col);           // returns 'true' on success
    static void SetSGR(Stream& Sink, char const* SGR);
    static void SetColor(Stream& Sink, int Foreground, int Background);
    static void SendControl(Stream& Sink, char const* CntlStr);
    static void CursOn(Stream& Sink);
    static void CursOff(Stream& Sink);

private:
    static void SingleControl(Stream& Sink, int Parameter, char const Code);

private:
    static char const* CSI;
};