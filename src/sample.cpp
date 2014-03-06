//author: Ugo Varetto
//Test driver for WebSocketService and reference implementation of Context 
//and Service interfaces
//Interface requirements:
//  * Context must provide reusable storage space to per-session service
//    instances to avoid reallocating memory at each request-reply
//  * Service is a per-session instance which handles requests and replies
//    through the Put and Get methods
#include <iostream>
#include "WebSocketService.h"

using Buffer  = std::vector< char >;
using Buffers = std::vector< Buffer >;
using BufferMap = std::map< void*, Buffers >;

//------------------------------------------------------------------------------
/// Context implementation: provides storage space to per-session services.
/// A Context is created once when the libwebsockets context is created
/// and destroyed when the WebSocketService instance is destroyed.
class Context {
public:
    /// Create char buffers
    /// @param p pointer key indexing the per-session buffer arrays; this is
    ///        the parameter received in the libwebsockets handler function
    /// @param n number of buffers to create, default = 1
    /// @param s initial size of buffers, default = 1
    /// @param d initial value of buffer elements
    void CreateBuffers(void* p, int n = 1, std::size_t s = 1, char d = '\0') {
        buffers_[p] = Buffers(n, Buffer(s, d));
    }
    /// Return reference to buffer
    /// @param p pointer key indexing the per-session buffer arrays; this is
    ///        the parameter received in the libwebsockets handler function
    /// @param i buffer index
    Buffer& GetBuffer(void* p, int i) {
        assert(buffers_.find(p) != buffers_.end());
        assert(buffers_[p].size() > i);
        return buffers_[p][i];
    }
    /// Remove all buffers associated to a user session
    /// @param p pointer key indexing the per-session buffer arrays; this is
    ///        the parameter received in the libwebsockets handler function
    void RemoveBuffers(void* p) {
        assert(buffers_.find(p) != buffers_.end());
        buffers_.erase(buffers_.find(p));
    }
    /// Perform websocket protocol initialization
    void InitProtocol(const char*) {}    
private:
    /// per-session buffer map; maps per-session user data pointer
    /// to buffer array
    BufferMap buffers_;    
};

/// Per-session (between a socket open and close) service instance, this is
/// the class that handles incoming requests and sends back replies.
/// Service instances get created when a WebSocket connection is established
/// and deleted when the connection terminates.
class Service {
public:
    /// Data frame to be sent to client.
    /// <code>frameBegin == bufferEnd</code> signals the end of writing
    /// operations; <code>frameBegin == bufferBegin</code> signals the
    /// beginning of writing operations.
    struct DataFrame {
        /// Start of buffer
        const char* bufferBegin = nullptr;
        /// One element past last element of buffer
        const char* bufferEnd = nullptr;
        /// Start of sub-region of buffer to operate on
        const char* frameBegin = nullptr;
        /// One element past the end of the sub-region of the buffer to
        /// operate on
        const char* frameEnd = nullptr;
        /// @c true if data is binary, @c false if it is text
        bool binary = false;
        /// Default constructor
        DataFrame() = default;
        /// Constructor
        /// @param bb pointer to first element of buffer
        /// @param be pointer to first element after the end of the buffer
        /// @param fb pointer to first element of sub-buffer
        /// @param fe pointer to fitst element after the end of the sub-buffer
        ///         to send
        DataFrame(const char* bb, const char* be,
                  const char* fb, const char* fe,
                  bool b)
        : bufferBegin(bb), bufferEnd(be),
          frameBegin(fe), frameEnd(fe) {}
 }; 
public:
    /// Deleted default constructor; object must always be created by a
    /// placement new with the Service(Context*) constructor.
    Service() = delete;
    /// Constructor taking a reference to a Context instance. This constructor
    /// is invoked when a new connection is established through a call to 
    /// a placement new
    Service(Context*) {}
    /// libwebsockets requires the send buffer to be pre and post padded
    /// properly; in case this service returns a properly padded buffer then
    /// this method returns @c true @c false otherwise. In case the buffer is not
    /// pre-formatted WebSocketService takes care of the formatting by
    /// performing an additional copy of the data into another buffer.
    virtual bool PreformattedBuffer() const { return false; }
   
    /// Returns true if data is ready, false otherwise. In this case Data()
    /// returns @c false after each Get() to make sure that data in buffer
    /// is returned only once:
    virtual bool Data() const {
        return dataAvailable_;
    }
    /// Returns a reference to the data to send; called when libwebsockets needs
    /// to send data to clients;
    /// @param binary @true if data is in binary format, @false if it is text
    virtual const DataFrame& Get(int requestedChunkLength) const { 
        if(writeDataFrame_.frameBegin < writeDataFrame_.bufferEnd) {
            requestedChunkLength = std::min(requestedChunkLength,
                                           int(writeDataFrame_.bufferEnd - 
                                               writeDataFrame_.frameBegin));
            writeDataFrame_.frameEnd = writeDataFrame_.frameBegin
                                       + requestedChunkLength; 
        }
        else dataAvailable_ = false;
        return writeDataFrame_;
    }
    /// Called when libwebsockets receives data from clients
    virtual void Put(void* p, size_t len, bool done) {
        dataAvailable_ = done; //signal that data is available as soon as all
                               //data packets are received
        if(p == nullptr || len == 0) return;
        if(prevReadCompleted_) {
            buffer_.resize(0);
            prevReadCompleted_ = false;
        }
        const size_t prev = buffer_.size();
        buffer_.resize(buffer_.size() + len);
        UpdateWriteDataFrame();
        copy((const char*) p, (const char*) p + len, buffer_.begin() + prev);       
        if(done) {
            prevReadCompleted_ = true;
        }
    }
    /// Update write data frame 
    virtual void UpdateOutBuffer(int writtenBytes) {
        writeDataFrame_.frameBegin += writtenBytes;
    }
    /// 
    virtual int SetSuggestedOutChunkSize(int cs) {
        suggestedChunkSize_ = cs;
    }
    ///
    virtual int GetSuggestedOutChunkSize() const {
        return suggestedChunkSize_;
    } 
    /// Destroy service instance by cleaning up all used resources. Ususally
    /// this is implemented by explicitly invoking the destructor since no
    /// delete is never executed on this instance which is always created
    /// through a placement new in the first place
    virtual void Destroy() {
        this->~Service();
    }
private:
    void UpdateWriteDataFrame() {
        writeDataFrame_.bufferBegin = &buffer_[0];
        writeDataFrame_.bufferEnd   = &buffer_[0] + buffer_.size();
        writeDataFrame_.frameBegin  = writeDataFrame_.bufferBegin;
        writeDataFrame_.frameEnd    = writeDataFrame_.frameBegin;
    }
    /// virtual destructor, never called through delete since instances of
    /// this class are always created through a placement new call
    virtual ~Service() {}    
private:
    /// Data buffer, filled in Put() method
    Buffer buffer_;
    /// @c false : text data only
    bool binary_ = false;
    /// @c true if data ready
    mutable bool dataAvailable_ = false;
    /// 
    mutable DataFrame writeDataFrame_;
    ///
    bool prevReadCompleted_ = true;
    ///
    int suggestedChunkSize_ = 4096;
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
    WSS::SetLogger(log);
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
            WSS::Entry< Service, WSS::ASYNC_REP >("myprotocol-async")
    );
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(50, //ms
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

