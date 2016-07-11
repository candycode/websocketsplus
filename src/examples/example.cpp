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

//clang++ -std=c++11 -I ../src -I /usr/local/libwebsockets/include  \
//../src/examples/example.cpp ../src/WebSocketService.cpp \
//-L /usr/local/libwebsockets/lib -lwebsockets

//to use binary data compile with -DBINARY_DATA

//Test driver for WebSocketService 
//Interface requirements:
//  * Context must provide reusable storage space to per-session service
//    instances to avoid reallocating memory at each request-reply
//  * Service is a per-session instance which handles requests and replies
//    through the Put and Get methods
#include <iostream>
#include "../WebSocketService.h"
#include "../Context.h"
#include "SessionService.h"


//------------------------------------------------------------------------------
/// Simple echo test driver, check below 'main' for matching html client code 
int main(int, char**) {
    using namespace wsp;
    using WSS = WebSocketService;
    WSS ws;
    WSS::ResetLogLevels(); // clear all loggers
    //create and set logger for 'INFO' logs
    auto log = [](int level, const char* msg) {
        std::cout << WSS::Level(level) << "> " << msg << std::endl;
    };
    WSS::SetLogger(log, "NOTICE", "WARNING", "ERROR");
    using Service = SessionService< Context<> >;
    const int readBufferSize = 4096; //the default anyway
    //init service
    ws.Init(9001, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            Context<>(), //context instance, will be copied internally
            //protocol->service mapping
            //sync request-reply: at each request a reply is immediately sent
            //to the client
            WSS::Entry< Service, WSS::REQ_REP >("myprotocol", readBufferSize),
            //async: requests and replies are handled asynchronously
            WSS::Entry< Service, WSS::ASYNC_REP >("myprotocol-async"),
            //sync request with greedy send: all the data chunks are sent in
            //a loop
            WSS::Entry< Service, WSS::REQ_REP,
                        WSS::SendMode::SEND_GREEDY >("myprotocol-greedy"),
             //async request with greedy send
            WSS::Entry< Service, WSS::ASYNC_REP,
                        WSS::SendMode::SEND_GREEDY >("myprotocol-async-greedy")
    );
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(50, //ms
                 []{return true;} //continuation condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}

//------------------------------------------------------------------------------
// Sample html code to test service, use either 'myprotocol' for req-rep or
// myprotocol-async' for async processing.
// When testing with async processing also try to have Service::Data() always
// return true or return true a fixed number of time  after each Put
// <!DOCTYPE html>
// <html>
//    <head>
//        <meta charset="utf-8">
//        <script src=
//   "http://ajax.googleapis.com/ajax/libs/jquery/1.7.2/jquery.min.js"></script>
//        <script type="text/javascript">
//            $(function() {
//                window.WebSocket = window.WebSocket || window.MozWebSocket;

//                var websocket = new WebSocket('ws://127.0.0.1:9000',
//                                              'myprotocol-async');

//                websocket.onopen = function () {
//                    $('h1').css('color', 'green');
//                };

//                websocket.onerror = function () {
//                    $('h1').css('color', 'red');
//                };

//                websocket.onmessage = function (message) {
//                    console.log(message.data);
//                    $('div').append($('<p>', { text: message.data }));
//                };
               

//                $('button').click(function(e) {
//                    e.preventDefault();
//                    websocket.send($('input').val());
//                    $('input').val('');
//                });
//            });
//        </script>
//        </head>
//    <body>
//        <h1>WebSockets test</h1>
//        <form>
//            <input type="text" />
//            <button>Send</button>
//        </form>
//        <div></div>
//    </body>
// </html>

