/*  Common Command Line Interface library implementation

    (C) 2020 TinyBus

*/

#include <string.h>
#include "clilib.h"

CmdLine::CmdLine()
{
    end();
}

CmdLine::~CmdLine()
{
    end();
}

bool CmdLine::begin(Stream& CmdStream, ProcessorDesc* Descs, int NbrOfDescs, char const* ContextStr, void* Context)
{
    end();
    if ((Descs == nullptr) || (NbrOfDescs <= 0))
    {
        return false;
    }
    _stream = &CmdStream;
    _descs = Descs;
    _nbrOfDescs = NbrOfDescs;
    _currentContext = ContextStr;
    _sol = true;
    _context = Context;
    return true;
}

void CmdLine::end()
{
    _stream = nullptr;
    _descs = nullptr;
    _nbrOfDescs = 0;
    _cmdLineIx = 0;
    _lastStatus = CmdLine::Status::Ok;
}
    
bool CmdLine::IsReady()
{
    if (_sol) 
    {
        _stream->print("\r");
        _stream->print(_currentContext);
        _stream->print("> ");
        _stream->flush();
        _sol = false;
    }
    while ((_stream != nullptr) && (_stream->available() > 0))
    {
        char c = _stream->read();

        switch (c)
        {
            case 127:   // Backspace
            {
                if (_cmdLineIx == 0)
                {
                    // No space in buffer to backspace
                    _stream->print('\x07');     //bell
                    _stream->flush();
                    return false;
                }
                _cmdLineIx--;
                _stream->print(c);_stream->print(' ');_stream->print(c);
                _stream->flush();
            }
            break;

            case 27:     // ESC
            {
                _stream->print('\x07');     //bell
                _stream->flush();

                // clear the esc seq from the stream - or timeout
                unsigned long timeoutTime = millis() + 2000;
                while (timeoutTime > millis())
                {
                    if (_stream->available() > 0)
                    {
                        c = _stream->read();
                        if (((c >= 'A') && (c <= 'Z')) || (c == '~'))
                        {
                            // End of valid ESC seq
                            return false;
                        }
                    }
                    else
                    {
                        yield();
                    }
                }
            }   
            break;

            case '\n':
              break;

            default:
            {
                if (c == '\r')
                {
                    // Process command line and execute matching processor
                    _cmdLine[_cmdLineIx] = 0;       // terminate the cli string
                    _stream->print("\r\n");
                    _stream->flush();
                    _cmdLineIx = 0;
                    _sol = true;
                    ProcessCommandLine();
                    if (_lastStatus != CmdLine::Ok)
                    {
                        _stream->print("Error: ");
                        _stream->print(CmdLine::StatusToText(_lastStatus));
                        _stream->print("\n\r");
                    }
                    return true;
                }

                if (c == '\t')
                {
                    c = ' ';
                }

                if (_cmdLineIx == _maxCmdLineSize)
                {
                    // No space in buffer
                    _stream->print('\x07');     //bell
                    _stream->flush();
                    return false;
                }

                // Stuff into buffer
                _stream->print(c); _stream->flush();
                _cmdLine[_cmdLineIx] = c;
                _cmdLineIx++;
            }
            break;
        }
    }
    return false;
}

void CmdLine::ShowHelp()
{
    for (CmdLine::ProcessorDesc *pdPtr = _descs; pdPtr < _descs + _nbrOfDescs; pdPtr++)
    {
        _stream->print("\tCmd: '");
        _stream->print(pdPtr->_cmdText);
        _stream->print("' -- ");
        _stream->print(pdPtr->_helpText);
        _stream->print("\n\r");
    }
}

void CmdLine::ProcessCommandLine()
{
  //Serial.print("AT: ProcessCommandLine: '");
  //Serial.print(_cmdLine);
  //Serial.println("'");

    const int maxParameters = 20;
    char* params[maxParameters + 1];
    int nbrParams = 0;
    char* linePtr = &_cmdLine[0];

    while (nbrParams < maxParameters)
    {
        while (*linePtr == ' ') linePtr++;      // skip leading spaces

        if (*linePtr == 0)
        {
            // EOL
            params[nbrParams] = nullptr;
            break;
        }

        if ((*linePtr) == '"')
        {
            // Have string literal
            linePtr++;
            params[nbrParams] = linePtr;
            nbrParams++;
            while ((*linePtr != 0) && (*linePtr != '"')) linePtr++;        // scan to terminator
            if (*linePtr == 0)
            {
                _lastStatus = InvalidStringLiteral;         // no closing "
                return;
            }
            *linePtr = 0;           // terminate quoted sub-string
            linePtr++;
        }
        else
        {
            params[nbrParams] = linePtr;
            nbrParams++;
            linePtr++;
            while ((*linePtr != 0) && (*linePtr != ' ')) linePtr++;        // scan to terminator
            if (*linePtr != 0)
            {
                *linePtr = 0;       // terminate substring (over trailing space delim)
                linePtr++;
            }
        }
    }

    if (nbrParams == maxParameters)
    {
        _lastStatus = TooManyParameters;
        return;
    }

    if ((nbrParams == 0) || (params[0] == nullptr))
    {
        _lastStatus = MissingCommand;
        return;
    }

    if ((strcmp("?", params[0]) == 0) || (strcmp("help", params[0]) ==0))
    {
        _stream->print("\n\rHelp:\n\r");
        ShowHelp();
        _lastStatus = CmdLine::Ok;
        return;
    }

    for (CmdLine::ProcessorDesc* pdPtr = _descs; pdPtr < _descs + _nbrOfDescs; pdPtr++)
    {
        if (strcmp(pdPtr->_cmdText, params[0]) == 0)
        {
            _lastStatus = pdPtr->_processor(*_stream, nbrParams, (char const**)&params[0], _context);
            return;
        }
    }

    _lastStatus = InvalidCommandName;
}

