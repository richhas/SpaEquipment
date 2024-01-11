/*
    State Machine framework Definitions 

    Copyright TinyBus 2020
*/
#ifndef _STATEMACHINE_
#define _STATEMACHINE_

#include <Arduino.h>
#include "Common.h"


//* Generalized state-machine class for Arduino-like (polled) environments
//
//      TSMImp - deriving class name
//      TStates - enum class type name for allowed SM states; with MAX being required as last enum value
template <typename TSMImp, typename TStates>
class StateMachine
{
public:
    //* Abstract state machine state interface
    class StateImp
    {
    public:
        // Derivation implementation interface
        virtual void Entry(TSMImp& Parent) {};      // Called once when a new state is being entered
        virtual void Exit(TSMImp& Parent) {};       // Called once when a current state is being left
        virtual void Process(TSMImp& Parent) {};    // Continously called while in the current state
    };

    StateMachine();
    void Process();

    __inline StateImp& GetCurrentState() { return *_currentState; }
    void ChangeState(TStates NewState);

protected:
    // Derivation API
    void SetStateImp(TStates State, StateImp& Imp)
    {
        $Assert(_currentState == nullptr);          // Can only be done until first ChangeState call
        $Assert(IsValidState(State));
        _states[(int)State] = &Imp;
    }

    virtual void OnProcess() {}

private:
    bool IsValidState(TStates State)
    {
        const int numberOfStates = sizeof(_states) / sizeof(StateImp*);

        return (((int)State <= numberOfStates));
    }

private:
    StateImp*   _states[(int)TStates::MAX];
    StateImp*   _currentState;
};

//* Inline StateMachine<> Imp
template <typename TSMImp, typename TStates>
StateMachine<TSMImp, TStates>::StateMachine()
{
    _currentState = nullptr;
    for (int ix = 0; ix < (int)TStates::MAX; ix++)
    {
        _states[ix] = nullptr; 
    }
}

template <typename TSMImp, typename TStates>
void StateMachine<TSMImp, TStates>::Process()
{
    $Assert(_currentState != nullptr);

    OnProcess();                    // Allow derivation common state-independent time
    _currentState->Process((TSMImp&)(*this));
}

template <typename TSMImp, typename TStates>
void StateMachine<TSMImp, TStates>::ChangeState(TStates NewState)
{
    $Assert(IsValidState(NewState));
    if (_currentState == nullptr)
    {
        // Initial setup
        for (int ix = 0; ix < (int)TStates::MAX; ix++)
        {
           $Assert(_states[ix] != nullptr);             // All states must be initialized
        }
    }
    else
    {
        _currentState->Exit((TSMImp&)(*this));
    }
    
    _currentState = _states[(int)NewState];
    _currentState->Entry((TSMImp&)(*this));
}

#endif /* _STATEMACHINE_ */
