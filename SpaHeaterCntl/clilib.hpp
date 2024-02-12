/*  Common Command Line Interface library definitions

    (C) 2020 TinyBus

*/
#pragma once

#include <Arduino.h>


// CommandLine processing class
class CmdLine
{
public:
    enum Status : int           // bugbug: make enum class
    {
        Ok = 0,
        MissingCommand = -1,
        TooManyParameters = -2,
        InvalidCommandName = -3,
        InvalidStringLiteral = -4,
        UnexpectedParameterCount = -5,
        InvalidParameter = -6,
        CommandFailed = -7,
    };
    constexpr static char const* StatusToText(Status S)
    {
        switch (S)
        {
            case Ok: return "Ok";
            case MissingCommand: return "MissingCommand";
            case TooManyParameters: return "TooManyParameters";
            case InvalidCommandName: return "InvalidCommandName";
            case InvalidStringLiteral: return "InvalidStringLiteral";
            case UnexpectedParameterCount: return "UnexpectedParameterCount";
            case CommandFailed: return "CommandFailed";
            case InvalidParameter: return "InvalidParameter";
            default: return "Unknown";
        }
    }

    typedef CmdLine::Status (*Processor)(Stream& CmdStream, int Argc, char const** Args, void* Context);        // Command processor function type

    // Each possible command line command is defined by a ProcessorDesc instance table
    struct ProcessorDesc
    {
        CmdLine::Processor  _processor;
        char const*         _cmdText;
        char const*         _helpText;
    };

    CmdLine();
    ~CmdLine();
    bool begin(Stream& CmdStream, ProcessorDesc* Descs, int NbrOfDescs, char const* ContextStr = "", void* Context = nullptr);
    void end();
    bool IsReady();         // returns true if command line was processed and is ready for next command
    void ShowHelp();
    
    __inline CmdLine::Status GetLastStatus() { return _lastStatus; }
    __inline void SetContextStr(char const* ContextStr) { _currentContext = ContextStr; }
    __inline void* GetContext() { return _context; }

private:
    void ProcessCommandLine();

private:
    void*               _context;
    Stream*             _stream;
    CmdLine::ProcessorDesc* _descs;
    int                 _nbrOfDescs;
    CmdLine::Status     _lastStatus;
    char const*         _currentContext;
    bool                _sol;

    static const int    _maxCmdLineSize = 128;
    char                _cmdLine[_maxCmdLineSize + 1];
    int                 _cmdLineIx;
};