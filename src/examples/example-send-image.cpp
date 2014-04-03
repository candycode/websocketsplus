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
#include "SessionService.h"

using namespace std;

using Image = std::vector< char >;

struct Images {
    std::vector< Image > images;
    void Load(const string& prefix,
              int numFrames) {
        int numDigits = 0;
        int n = numFrames;
        while(n > 0) {
            n /= 10;
            ++numDigits;
        }
        for(int i; i < numFrames; ++i) {
            //file name
            string fname = prefix;
            const string fn = to_string(i);
            for(int z = 0; z != numDigits - fn.size(); ++z)
                fname += to_string(z);
            //file size
            std::ifstream in(filename, std::ifstream::in
                             | std::ifstream::binary);
            if(!in) throw std::runtime_error("cannot open file");
            in.seekg(0, ios::end);
            const size_t fileSize = in.tellg();
            Image img;
            img.reserve(fileSize);
            //read
            in.seekg(0, ios::beg);
            img.assign(istreambuf_iterator<char>(in),
                       istreambuf_iterator<char>());

            images.push_back(img);
        }
    }
};

/------------------------------------------------------------------------------
/// Time service: streams current date and time
class ImageService : public SessionService< wsp::Context< Images > > {
    using Context = wsp::Context< Images >;
public:
    using DataFrame = SessionService::DataFrame;
    StreamService(Context* c) :
     SessionService(c), ctx_(c), frameCounter_(0) {
        InitDataFrame();
    }
    bool Data() const { return true; }
    const DataFrame& Get(int requestedChunkLength) {
        if(df_.frameEnd < df_.bufferEnd) {
            df.frameBegin = df.frameEnd;
            df_.frameEnd += min(requestedChunkLength, 
                                df.bufferEnd - df.frameBegin);
        } else {
            frameCounter_ = (frameCounter_ + 1) % ctx_->UserData().size();  
            InitDataFrame();
        }  
    }
    std::chrono::duration< double > 
    MinDelayBetweenWrites() const {
        return std::chrono::duration< double >(0.01);
    }
private:
    void InitDataFrame() {
        df_.bufferBegin = &ctx_->UserData()[frameCounter_][0];
        df_.bufferEnd = df_.bufferBegin + ctx_->UserData()[frameCounter_].size();
        df_.frameBegin = df.bufferBegin;
        df_.frameEnd = df.frameBegin;
    }    
private:    
    DataFrame df_;
    Context* ctx_ = nullptr;
    unsigned int frameCounter_ = 0;
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
    using Service = ImageService< ImageContext >;
    const int readBufferSize = 4096; //the default anyway
    //init service
    ws.Init(9001, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            Context< Images >(), //context instance, will be copied internally
            //protocol->service mapping
            //sync request-reply: at each request a reply is immediately sent
            //to the client
            WSS::Entry< Service, WSS::REQ_REP >("image", readBufferSize));
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(10, //ms
                 []{return true;} //termination condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}
