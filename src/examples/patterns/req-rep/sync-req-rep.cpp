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
#include <deque>
#include <functional>

#include <cassert>
#include <vector>
#include <string>


//==============================================================================
namespace {
    static const bool BINARY_OPTION = false;
}

template < typename FunT, typename ContextT  >
class FunService {
public:
    using Context = ContextT;
    using DataFrame = wsp::DataFrame;
public:
    FunService() = delete;
    FunService(Context* ctx, const char* protocol = nullptr)
           : replyDataFrame_(nullptr, nullptr, nullptr, nullptr,
                             BINARY_OPTION),
             fun_(ctx->GetServiceData().template Get< FunT >(protocol)) {

    }
    bool PreformattedBuffer() const { return false; }
    bool Data() const {
        //either there is data in reply buffer or there is data
        //in reply queue
        return !replies_.empty();
    }
    const DataFrame& Get(int requestedChunkLength) /*const*/ {
        //if data has been consumed remove entry and pop
        //new data if available
        if(Data()) {
            //only remove data if already used
            if(wsp::Consumed(replyDataFrame_)) replies_.pop_front();
            //if data in queue make dataframe point to front of queue
            if(!replies_.empty()) {
                wsp::Init(replyDataFrame_, replies_.front().data(),
                          replies_.front().size());
            }
        }
        //if data available update data frame
        if(!wsp::Update(replyDataFrame_, requestedChunkLength)) {
            wsp::Reset(replyDataFrame_);
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
            replies_.push_back(fun_(requestBuffer_));
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
        this->~FunService();
    }
    /// Minimum delay between consecutive writes in seconds.
    std::chrono::duration< double >
    MinDelayBetweenWrites() const {
        return std::chrono::duration< double >(0);
    }
    void UpdateOutBuffer(size_t requestedChunkLength) {
        wsp::Update(replyDataFrame_, requestedChunkLength);
    }
private:
    /// destructor, never called through delete since instances of
    /// this class are always created through a placement new call
    /// and destroyed through a call to Destroy()
    virtual ~FunService() {}
private:
    std::deque< std::vector< char > > replies_;
    DataFrame replyDataFrame_;
    std::vector< char > requestBuffer_; //temporary request storage
    int suggestedWriteChunkSize_ = 4096;
    FunT fun_;
};



//==============================================================================

//------------------------------------------------------------------------------
///
using namespace std;

using FunT = function< vector< char > (const vector< char >&) >;
FunT reverse = [](const vector< char >& v) {
    vector< char > ret(v.size());
    copy(v.rbegin(), v.rend(), ret.begin());
    return ret;
};
FunT echo = [](const vector< char >& v) {
    return v;
};

//note template is currently useless since the return type is the same

struct Functions {
    Functions(const FunT& r, const FunT& e)
            : reverse(r), echo(e) {}
    FunT reverse;
    FunT echo;
    template < typename T >
    const T& Get(const char* protocol) const;
};


template <>
const FunT& Functions::Get< FunT >(const char* protocol) const {
    if(protocol == string("reverse")) return this->reverse;
    else if(protocol == string("echo")) return this->echo;
    else {
        static const FunT f;
        return f;
    }
}



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

    using ReverseService = FunService< FunT, Context< Functions > >;
    using EchoService    = FunService< FunT, Context< Functions > >;

    //init service
    ws.Init(9001, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            //context instance, will be copied internally
            MakeContext(Functions(::reverse, echo)),
            //protocol->service mapping
            //sync request-reply: at each request a reply is immediately sent
            //to the client
            WSS::Entry< ReverseService, WSS::REQ_REP >("reverse", readBufferSize),
            WSS::Entry< EchoService, WSS::REQ_REP >("echo", readBufferSize)

    );
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(50, //ms
                 []{return true;} //continuation condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}
