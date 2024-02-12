// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// ConsoleTask implementation

#include "SpaHeaterCntl.h"
#include "ConsoleTask.hpp"


ConsoleTask     consoleTask(Serial);


ConsoleTask::ConsoleTask(Stream &StreamToUse)
    :   _stream(StreamToUse)
{  
}

ConsoleTask::~ConsoleTask() 
{ 
}

void ConsoleTask::Push(CmdLine::ProcessorDesc &Descs, int NbrOfDescs, char const *ContextStr)
{
    // if we have at tos element, end the cmdLine
    if (!_cmdLineStack.IsEmpty())
    {
        _cmdLine.end();
    }
    _cmdLineStack.Push(ProcessorDesc{&Descs, NbrOfDescs, ContextStr, this});
    _cmdLine.begin(_stream, &Descs, NbrOfDescs, ContextStr, this);
}

void ConsoleTask::Pop()
{
    $Assert(_cmdLineStack.Size() > 1); // we should never pop to an empty stack
    _cmdLineStack.Pop();

    // clear the current cmdLine state
    _cmdLine.end();

    // restore the previous cmdLine
    _cmdLine.begin(
        _stream, 
        _cmdLineStack.Top()._descs, 
        _cmdLineStack.Top()._nbrOfDescs, 
        _cmdLineStack.Top()._contextStr, 
        _cmdLineStack.Top()._context);
}

void ConsoleTask::setup()
{   
    logger.Printf(Logger::RecType::Info, "ConsoleTask is Active");
}

void ConsoleTask::begin(CmdLine::ProcessorDesc *Descs, int NbrOfDescs, char const *ContextStr)
{
    Push(*Descs, NbrOfDescs, ContextStr);
}

void ConsoleTask::loop() 
{
    _cmdLine.IsReady();
}
