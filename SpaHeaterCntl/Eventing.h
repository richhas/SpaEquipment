/*
    C++ Eventing framework definitions 

    Copyright TinyBus 2020
*/
#pragma once

#include <Arduino.h>
#include "Common.h"

//* Event source type. Instances are declared as Event<T>.
template <typename TEventArgs>
class Event
{
public:
    // Each reception point for events are declared as Event<T>::Handler. These are bound to a corresponding 
    // Event<T> via the += operator - see below.
    //
    // Each Handler instance is bound to at most one callback function via the Event<T>Handler::SetCallback() function
    //
    class HandlerBase
    {
    protected:
        //* Private (for derivation only) HandlerBase state
        friend class Event;
        void*           _context;
        HandlerBase*    _nextHandler;

    protected:
        HandlerBase()
            :   _context(nullptr),
                _nextHandler(nullptr)
        {}

        HandlerBase(void* Context)
            :   _context(Context),
                _nextHandler(nullptr)
        {}

        //* Derivation interface
        virtual void OnSignal(Event const& From, TEventArgs const& EventArg, void* Context) = 0;

    public:
        ~HandlerBase()
        {
            $FailFast();         // Not valid to dtor Handlers at this point
        }

        HandlerBase(HandlerBase&) = delete;                     // Disable copy ctor
        HandlerBase(HandlerBase&&) = delete;                    // Disable move ctor
        HandlerBase& operator=(HandlerBase&) = delete;          // Disable copy assignment
        HandlerBase& operator=(HandlerBase&&) = delete;         // Disable move assignment

    private:
        //* Local event dispatcher - supports derivation
        void OnEventReceived(Event const& From, TEventArgs const& EventArg)
        {
            OnSignal(From, EventArg, _context);     // Invoke optional derivation 
        }
    };

    class Handler : public HandlerBase
    {
    public:
        // Function signature for a Event<T>::Handler optional Callback function
        //
        //  From = Source (Signalling) Event<T> instance
        //  EventArg = Event value
        //  Context = optional Context value set with SetCallback() - used for example to pass the this * so a
        //            static method can forward the event notification callback into an instance method
        //
        typedef void (*Callback)(Event const& From, TEventArgs const& EventArg, void* Context);

    private:
        //* Private Handler state
        friend class Event;
        Callback        _callback;

    public:
        Handler()
            :   HandlerBase(),
                _callback(nullptr)
        {}

        Handler(Callback ToCall, void* Context)
            :   HandlerBase(),
                _callback(nullptr)
        {
            SetCallback(ToCall, Context);
        }

        ~Handler()
        {
            $FailFast();         // Not valid to dtor Handlers at this point
        }

        Handler(Handler&) = delete;
        Handler(Handler&&) = delete;
        Handler& operator=(Handler&) = delete;
        Handler& operator=(Handler&&) = delete;

        // Bind a Callback and optional Context to a current Handler 
        __noinline void SetCallback(Callback ToCall, void* Context = nullptr)
        {
            _callback = ToCall;
            HandlerBase::_context = Context;
        }

    protected:
        //* Derivation interface - Note if a custom derivation of Handler<> overrides OnSignal() this
        //  Callback call will not occur unless the derivation calls Event<>::Handler::OnSignal(). In
        //  Most cases a Handler<> derivation that overrides does not need the Callback behavior.
        void OnSignal(Event const& From, TEventArgs const& EventArg, void* Context) override
        {
            if (_callback != nullptr)
            {
                // Invoke optional Callback
                _callback(From, EventArg, HandlerBase::_context);
            }
        }
    };

    //* C++ Member Event<> helper that is close to the semantics of a C# delegate type
    //
    template <typename TCallbackClass>
    class Delegate : public Event<TEventArgs>::HandlerBase
    {
    public:
        typedef void (TCallbackClass::* CallbackMethod)(Event<TEventArgs> const& From, TEventArgs const& EventArg);

    private:
        CallbackMethod      _callbackMethod;
        // TCallbackClass*  _context;

    public:
        __noinline void SetCallback(TCallbackClass& CallbackObject, CallbackMethod MethodToCall)
        {
            _callbackMethod = MethodToCall;             // Bind instance method class-relative address to this delegate
            HandlerBase::_context = &CallbackObject;    // Capture instance address as "context"
                                                        // Together _context::_callbackMethod form an invokable C++ instance method address
        }

        Delegate()
            :   HandlerBase()
        {}

        Delegate(CallbackMethod MethodToCall, TCallbackClass& OnInstance)
            :   HandlerBase(nullptr)
        {
            SetCallback(OnInstance, MethodToCall);
        }

    private:
        void OnSignal(Event<TEventArgs> const& FromEvent, TEventArgs const& EventVal, void* Context) override
        {
            if (HandlerBase::_context != nullptr)
            {
                (((TCallbackClass*)Context)->*_callbackMethod)(FromEvent, EventVal);
            }
        }
    };

private:
    //* Event internal state
    HandlerBase*    _handlers;

public:
    Event()
        :   _handlers(nullptr)
    {}

    Event(Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&) = delete;
    Event& operator=(Event&&) = delete;

    ~Event()
    {
        $FailFast();         // Not valid to dtor Events at this time
    }

    // Bind a given Handler to the event notification list
    __noinline Event& operator+=(HandlerBase& RxHandler)
    {
        $Assert(RxHandler._nextHandler == nullptr);         // RxHandler can't already be on a list
        RxHandler._nextHandler = _handlers;
        _handlers = &RxHandler;
        return *this;
    }

    // Unbind a given Handler to the event notification list
    __noinline Event& operator-=(HandlerBase& RxHandler)
    {
        HandlerBase*    currHandler = _handlers;
        HandlerBase*    prevHandler = nullptr;

        while (currHandler != nullptr)
        {
            if (currHandler == &RxHandler)
            {
                // Found Handler to remove
                if (prevHandler == nullptr)
                {
                    // removing first Handler on list
                    _handlers = RxHandler._nextHandler;
                }
                else
                {
                    // De-link RxHandler from list
                    prevHandler->_nextHandler = RxHandler._nextHandler;
                }
                
                currHandler->_nextHandler = nullptr;
                break;
            }
            prevHandler = currHandler;
            currHandler = currHandler->_nextHandler;
        }

        return *this;
    }

    // Distrubute event to all subscribed Handlers
    void Signal(TEventArgs const& PostedEvent)
    {
        HandlerBase* currHandler = _handlers;
        while (currHandler != nullptr)
        {
            currHandler->OnEventReceived(*this, PostedEvent);
            currHandler = currHandler->_nextHandler;
        }
    }
};

