// SPA Heater Controller for Maxie HA system 2024 (c)TinyBus
// ConsoleTask Definitions

#pragma once
#include "clilib.hpp"
#include "SpaHeaterCntl.hpp"
#include "ConsoleTask.hpp"


//* Admin console Task that can be redirected to any Stream; supports stacking of command processors
class ConsoleTask : public ArduinoTask
{
public:
    ConsoleTask();
    ConsoleTask(Stream &StreamToUse);
    ~ConsoleTask();

    void SetStream(Stream &StreamToUse); 

    void begin(CmdLine::ProcessorDesc *Descs, int NbrOfDescs, char const *ContextStr = "");

    void Push(CmdLine::ProcessorDesc &Descs, int NbrOfDescs, char const *ContextStr = "");
    void Pop();

    void StartBoilerConfig();
    void EndBoilerConfig();
    void StartBoilerControl();
    void EndBoilerControl();

protected:
    virtual void setup() override;
    virtual void loop() override;

private:
    Stream* _stream;
    CmdLine _cmdLine;

    struct ProcessorDesc
    {
        CmdLine::ProcessorDesc *_descs;
        int _nbrOfDescs;
        char const *_contextStr;
        void *_context;
    };

    Stack<ProcessorDesc, 4> _cmdLineStack; // maximum of 4 stacked command processors
};

//** Cross module references
extern class ConsoleTask consoleTask;
