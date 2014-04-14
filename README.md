Websockets+
===========

C++11 web socket service based on [libwebsockets](https://github.com/warmcat/libwebsockets) 

Websocket+ is distributed under the terms of the GNU Lesser General Public License version 3.

Check out the examples under src/examples for usage information.  

The framework is split into three files:

* Context.h - context shared among per-session service instances
* WebSocketService.h - class that holds instances of libwebsockets types and
  manages the lifetime of per-session service instances
* WebSocketService.cpp - small file containing only the definition of 
  value <--> string maps used for configuring logging

The Context and Service types are template parameters used internally by
WebSocketService and it is therefore not required to use the provided Context
class.

The DataFrame.h file is a reference implementation for the compile time
interface WebSocketService expects Service::DataFrame to implement; you
can simply include this file from your own code and add a typedef/using
declaration inside the Service implementation.

The supported communication patterns are:

* synchronous request-reply: a reply is sent back immediately after the request
  is received
* asynchronous request-reply: as soon as a request is received a reply is 
  scheduled for future execution
* streaming: send only    

The publish-subscibe pattern has been removed, since there are already multiple
ways of implementing it:

* streaming with protocol: client subscribe by connecting to a specific protocol
* asynchronous request-reply with 'Sending' flag: subscribe at first request
  and keep on sending data by having the service 'Sending' method always
  return true for the duration of the subscription

It is possible (and advisable) to set the minimum time between send calls
through a throttling parameter (see src/examples/example-streaming.cpp).





