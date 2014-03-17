//author: Ugo Varetto
//Test driver for WebSocketService and reference implementation of Context 
//and Service interfaces
//Interface requirements:
//  * Context must provide reusable storage space to per-session service
//    instances to avoid reallocating memory at each request-reply
//  * Service is a per-session instance which handles requests and replies
//    through the Put and Get methods
#include <iostream>
#include <string>
#include <chrono>
#include <ctime>
#include <sstream>
#include "WebSocketService.h"
#include "ref-context-service.h"


class StreamService : public SessionService {
public:
    using DataFrame = SessionService::DataFrame;
    StreamService(Context* c) : SessionService(c), time_(0x100, 0) {}
    bool Data() const { return true; }
    const DataFrame& Get(int requestedChunkLength) {
        using namespace std::chrono;
        const system_clock::time_point now = system_clock::now();
        out_.str("");
        const std::time_t tt = system_clock::to_time_t(now);
        out_ << count++; //ctime(&tt);
        tmpstr_ = out_.str();
        std::copy(tmpstr_.begin(), tmpstr_.end(), time_.begin());
        df_= DataFrame(&time_[0], &(time_[0]) + time_.size(),
                       &time_[0], &(time_[0]) + time_.size(), false);
        return df_; 
    }
private:    
    std::vector< char > time_;
    DataFrame df_;
    std::ostringstream out_;
    std::string tmpstr_;
    int count = 0;
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
