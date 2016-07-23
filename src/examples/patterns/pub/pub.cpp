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
#include <WebSocketService.h>
#include <Context.h>
#include <DataFrame.h>
#include "../SyncQueue.h"
#include <functional>
#include <sstream>

#include <cassert>
#include <vector>
#include <string>

#include <thread>
#include <future>

#include <chrono>
#include <ctime>



//==============================================================================
namespace {
    static const bool BINARY_OPTION = true;
}

template < typename FunT, typename ContextT  >
class PublishService {
public:
    using Context = ContextT;
    using DataFrame = wsp::DataFrame;
public:
    PublishService() = delete;
    PublishService(Context* ctx, const char* protocol = nullptr)
           : replyDataFrame_(nullptr, nullptr, nullptr, nullptr,
                             BINARY_OPTION),
             fun_(ctx->GetServiceData().Get(protocol)),
             stop_(false) {
        auto f = [this]() {
            while(true/*!this->stop_*/) {
                //allow for handling requests to e.g. control the
                //data stream or request status information
                if(this->requests_.Empty()) {
                    this->replies_.Push(this->fun_());
                } else {
                    this->replies_.Push(this->fun_(this->requests_.Pop()));
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        };
        taskFuture_ = std::async(std::launch::async, f);
    }
    bool PreformattedBuffer() const { return false; }
    bool Data() const {
        //either there is data in reply buffer or there is data
        //in reply queue
        return !replies_.Empty() || !reply_.empty();
    }
    const DataFrame& Get(int requestedChunkLength) /*const*/ {
        //if data has been consumed remove entry and pop
        //new data if available
        //assert(Data());
        if(replyDataFrame_.bufferBegin == nullptr) {
            reply_ = replies_.Pop();
            replyDataFrame_.bufferBegin = reply_.data();
            replyDataFrame_.bufferEnd   = reply_.data() + reply_.size();
            replyDataFrame_.frameBegin  = replyDataFrame_.bufferBegin;
            replyDataFrame_.frameEnd    = replyDataFrame_.frameBegin +
                    std::min(requestedChunkLength, int(reply_.size()));
        } else if(replyDataFrame_.frameEnd < replyDataFrame_.bufferEnd) {
            DataFrame& df = replyDataFrame_;
            const int inc =
                    std::min(requestedChunkLength,
                             int(df.bufferEnd - df.frameEnd));
            df.frameEnd += inc;
        }
        return replyDataFrame_;
    }
    /// Called when libwebsockets receives data from clients
    void Put(void* p, size_t len, bool done) {
        if(p == nullptr || len == 0) return;
        const size_t prev = requestBuffer_.size();
        requestBuffer_.resize(requestBuffer_.size() + len);
        std::copy((const char*) p,
                  (const char*) p + len, requestBuffer_.data() + prev);
        //sync execution: data is transformed after read buffer is filled
        if(done) {
            requests_.Push(requestBuffer_);
            requestBuffer_.resize(0);
        }
    }
    void SetSuggestedOutChunkSize(int cs) {
        suggestedWriteChunkSize_ = cs;
    }
    int GetSuggestedOutChunkSize() const {
        return suggestedWriteChunkSize_;
    }
    bool Sending() const {
        return true;
    }
    void Destroy() {
        this->~PublishService();
    }
    /// Minimum delay between consecutive writes in seconds.
    std::chrono::duration< double >
    MinDelayBetweenWrites() const {
        return std::chrono::duration< double >(0);
    }
    /// Called by library after data is sent
    void UpdateOutBuffer(size_t bytesConsumed) {
        replyDataFrame_.frameBegin += bytesConsumed;
        replyDataFrame_.frameEnd = replyDataFrame_.frameBegin;
        if(replyDataFrame_.frameBegin >= replyDataFrame_.bufferEnd) {
            replyDataFrame_ = DataFrame(nullptr, nullptr, nullptr, nullptr,
                                        BINARY_OPTION);
        }
    }
private:
    /// destructor, never called through delete since instances of
    /// this class are always created through a placement new call
    /// and destroyed through a call to Destroy()
    virtual ~PublishService() {
        stop_ = true;
        if(taskFuture_.valid()) taskFuture_.get(); //get() forwards exceptions
    }
private:
    SyncQueue< std::vector< char > > requests_;
    SyncQueue< std::vector< char > >replies_;
    DataFrame replyDataFrame_;
    std::vector< char > requestBuffer_; //temporary request storage
    int suggestedWriteChunkSize_ = 4096;
    FunT fun_;
    std::future< void > taskFuture_;
    bool stop_ = false;
    std::vector< char > reply_;
};



//==============================================================================

//------------------------------------------------------------------------------

using namespace std;

vector< char > Now() {
    using namespace std::chrono;
    const system_clock::time_point now = system_clock::now();
    ostringstream oss("");
    const std::time_t tt = system_clock::to_time_t(now);
    oss << ctime(&tt);
    const string t = oss.str();
    return vector< char >(t.begin(), t.end());
}

vector< char > Empty(const vector< char >& ) { return vector< char >(); }

struct Time {
    function< vector< char > () > pub;
    function< vector< char > (const vector< char >& ) > req_rep;
    vector< char > operator()(const vector< char >& req) const {
        return req_rep(req);
    }
    vector< char > operator()() const {
        return pub();
    }
    Time() : pub(Now), req_rep(Empty) {}
};


//note: template is currently useless since the return type is the same
//for both reverse and echo

//service data provider: 2D index: [protocol name, type]
//this is used to initialize the service when the per-connection service
//instance is created
struct Functions {
    Functions(const Time& r)
            : curtime(r) {}
    Time curtime;
    //add one member function per return type
    const Time& Get(const char* protocol) const {
        if(protocol == string("time")) return this->curtime;
        else {
            throw std::domain_error("Protocol " + std::string(protocol)
                                    + " not supported");
        }
    }
};



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
    const int readBufferSize = 4096; //the default anyway

    using Service = PublishService< Time, Context< Functions > >;

    //init service
    ws.Init(9002, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            //context instance, will be copied internally
            MakeContext(Functions(Time())),
            //protocol->service mapping
            //sync request-reply: at each request a reply is immediately sent
            //to the client
            WSS::Entry< Service, WSS::ASYNC_REP >("time", readBufferSize)

    );
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(50, //ms
                 []{return true;} //continuation condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}
