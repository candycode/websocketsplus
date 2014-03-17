//author: Ugo Varetto
//Test driver for WebSocketService 
//Interface requirements:
//  * Context must provide reusable storage space to per-session service
//    instances to avoid reallocating memory at each request-reply
//  * Service is a per-session instance which handles requests and replies
//    through the Put and Get methods
#include <iostream>
#include "WebSocketService.h"
#include "Context.h"
#include "ServiceSession.h"


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
    using Service = SessionService< wsp::Context >;
    //init service
    ws.Init(9001, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            Context(), //context instance, will be copied internally
            //protocol->service mapping
            //sync request-reply: at each request a reply is immediately sent
            //to the client
            WSS::Entry< Service, WSS::REQ_REP >("myprotocol"),
            //async: requests and replies are handled asynchronously
            WSS::Entry< Service, WSS::ASYNC_REP >("myprotocol-async"),
             //async: requests and replies are handled asynchronously
            WSS::Entry< Service, WSS::REQ_REP,
                        WSS::SendMode::SEND_GREEDY >("myprotocol-greedy"),
             //async: requests and replies are handled asynchronously
            WSS::Entry< Service, WSS::ASYNC_REP,
                        WSS::SendMode::SEND_GREEDY >("myprotocol-async-greedy")//, 16384)
    );
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(1, //ms
                 []{return true;} //termination condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}

//------------------------------------------------------------------------------
// Sample html code to test service, use either 'myprotocol' for req-rep or
// myprotocol-async' for async processing.
// When testing with async processing also try to have Service::Data() always
// return true or return true a fixed number of time after each Put
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

