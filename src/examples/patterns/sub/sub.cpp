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
class SubscriptionService {
public:
    using Context = ContextT;
    using DataFrame = wsp::DataFrame;
public:
    SubscriptionService() = delete;
    SubscriptionService(Context* ctx, const char* protocol = nullptr)
           : replyDataFrame_(nullptr, nullptr, nullptr, nullptr,
                             BINARY_OPTION),
             fun_(ctx->GetServiceData().Get(protocol)),
             stop_(false) {
        auto f = [this]() {
            while(!this->stop_) {
                const std::vector< char > req = requests_.Pop();
                const std::vector< char > res
                    = this->fun_(req);
                if(!res.empty()) this->replies_.Push(res);
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
        std::cout << "!" << std::endl;
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
        return false;
    }
    void Destroy() {
        this->~SubscriptionService();
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
    virtual ~SubscriptionService() {
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


using Print = function< vector< char > (const vector< char >&) >;

//note: template is currently useless since the return type is the same
//for both reverse and echo

//service data provider: 2D index: [protocol name, type]
//this is used to initialize the service when the per-connection service
//instance is created
struct Functions {
    Functions(const Print& r)
            : print(r) {}
    Print print;
    //add one member function per return type
    const Print& Get(const char* protocol) const {
        if(protocol == string("recv")) return this->print;
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

    using Service = SubscriptionService< Print, Context< Functions > >;

    //init service
    ws.Init(9003, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            //context instance, will be copied internally
            MakeContext(Functions([](const vector< char >& msg) {
                cout << string(begin(msg), end(msg)) << endl;
                return vector< char >();
            })),
            //protocol->service mapping
            //sync request-reply: at each request a reply is immediately sent
            //to the client
            WSS::Entry< Service, WSS::REQ_REP >("recv", readBufferSize)

    );
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(50, //ms
                 []{return true;} //continuation condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}
