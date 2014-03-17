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

//Test driver for WebSocketService streaming feature
//Interface requirements:
//  * Context must provide reusable storage space to per-session service
//    instances to avoid reallocating memory at each request-reply
//  * Context must also provice a place to store time of last per-session
//    write in order to support throttling features  
//  * Service is a per-session instance which streams data at each Get request
#include <iostream>
#include <string>
#include <chrono>
#include <ctime>
#include <sstream>
#include "WebSocketService.h"
#include "Context.h"
#include "SessionService.h"


//------------------------------------------------------------------------------
/// Time service: streams current date and time
class StreamService : public SessionService< wsp::Context > {
public:
    using DataFrame = SessionService::DataFrame;
    StreamService(Context* c) : SessionService(c), time_(0x100, 0) {}
    bool Data() const { return true; }
    const DataFrame& Get(int requestedChunkLength) {
        using namespace std::chrono;
        const system_clock::time_point now = system_clock::now();
        out_.str("");
        const std::time_t tt = system_clock::to_time_t(now);
        out_ << ctime(&tt);
        tmpstr_ = out_.str();
        std::copy(tmpstr_.begin(), tmpstr_.end(), time_.begin());
        df_= DataFrame(&time_[0], &(time_[0]) + time_.size(),
                       &time_[0], &(time_[0]) + time_.size(), false);
        return df_; 
    }
    std::chrono::duration< double > 
    MinDelayBetweenWrites() const {
        return std::chrono::duration< double >(1);
    }
private:    
    std::vector< char > time_;
    DataFrame df_;
    std::ostringstream out_;
    std::string tmpstr_;
};


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
    //init service
    ws.Init(9002, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            Context(), //context instance, will be copied internally
            WSS::Entry< StreamService, WSS::STREAM >("myprotocol-stream"));
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(5000, //ms
                 []{return true;} //termination condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}

//------------------------------------------------------------------------------
// <!DOCTYPE html>
// <html>
//    <head>
//        <meta charset="utf-8">
//        <script src=
//   "http://ajax.googleapis.com/ajax/libs/jquery/1.9.1/jquery.min.js"></script>
//        <script type="text/javascript">
//           var websocket; 
//           $(function() {
//               window.WebSocket = window.WebSocket || window.MozWebSocket;
//               $('button').click(function(e) {
//                 try {
//                   websocket = new WebSocket('ws://127.0.0.1:9002',
//                                             'myprotocol-stream');
//                   if(!websocket) alert('');
//                   websocket.onopen = function () {
//                     $('h1').css('color', 'green');
//                   };

//                   websocket.onerror = function (e) {
//                     $('h1').css('color', 'red');
//                     console.log(e);
//                   };
//                   websocket.onmessage = function (e) {
//                      $("[name='output']").text(e.data);
//                   };
//                 } catch(except) {
//                     console.log(except);
//                  }
//                });
//            });
//        </script>
//        </head>
//    <body>
//        <h1>WebSockets test</h1>
//        <form>
//            <button>Receive</button>
//        </form>
//        <div><p name="output"></p></div>
//    </body>
// </html>

