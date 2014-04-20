// Websockets+ : C++11 server-side websocket library based on libwebsockets;
//               supports easy creation of services and built-in throttling
// Copyright (C) 2014  Ugo Varetto
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//Test driver for WebSocketService 
//Interface requirements:
//  * Context must provide reusable storage space to per-session service
//    instances to avoid reallocating memory at each request-reply
//  * Service is a per-session instance which handles requests and replies
//    through the Put and Get methods
#include <iostream>
#include "../WebSocketService.h"
#include "../Context.h"
#include "../DataFrame.h"

static const char* HEADER = 


static const char* BODY = 

class HttpService {
public:
    static const int HTTP = 1; //value can be anything;
    using DataFrame = wsp::DataFrame;
    HttpService(wsp::Context<>* , const unordered_map&< string, string >&) {}
    //Constructor(Context, unordered_map<string, string> headers)
    bool Valid() const { return true; }
    void InitBody() {}
    const DataFrame& Get(int chunkSize) const {}
    bool Data() const { return true; }
    int GetSuggestedChunkSize() const { return 0x1000; }
};

//Valid
//InitBody
//Get
//Data
//GetSuggestedChunkSize
//HTTP const to identify it as an http service
};


//------------------------------------------------------------------------------
/// Simple echo test driver, check below 'main' for matching html client code 
int main(int, char**) {
    using namespace wsp;
    using WSS = WebSocketService;
    WSS ws;
    using Service = HttpService< Context<> >;
    const int readBufferSize = 4096; //the default anyway
    //init service
    ws.Init(8001, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            Context<>(), //context instance, will be copied internally
            WSS::Entry< Service, WSS::ASYNC_REP >("http-only"));
    );
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(50, //ms
                 []{return true;} //termination condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}

