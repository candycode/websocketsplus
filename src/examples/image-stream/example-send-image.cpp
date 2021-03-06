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

//clang++ -std=c++11 -I ../src -I /usr/local/libwebsockets/include  \
//../src/examples/example-send-image.cpp ../src/WebSocketService.cpp \
//-L /usr/local/libwebsockets/lib -lwebsockets

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <iterator>
#include "../../WebSocketService.h"
#include "../../Context.h"
#include "../SessionService.h"

using namespace std;

using Image = std::vector< char >;

struct Images {
    std::vector< Image > images;
    void Load(int numFrames, const string& prefix,
              int loadFrames, const string& format) {
        int numDigits = 0;
        int n = numFrames;
        while(n > 0) {
            n /= 10;
            ++numDigits;
        }
        for(int i = 0; i < loadFrames; ++i) {
            //file name
            string fname = prefix;
            const string fn = to_string(i);
            for(int z = 0; z != numDigits - fn.size(); ++z) fname += "0";
            fname += to_string(i) + "." + format;
            cout << fname << endl;
            //file size
            std::ifstream in(fname, std::ifstream::in
                             | std::ifstream::binary);
            if(!in) throw std::runtime_error("cannot open file");
            in.seekg(0, ios::end);
            const size_t fileSize = in.tellg();
            in.seekg(0, ios::beg);
            Image img;
            
            //read
            //slow:
            //img.reserve(fileSize);
            //img.assign(istreambuf_iterator<char>(in),
            //           istreambuf_iterator<char>());
            //fast:
            img.resize(fileSize);
            in.read(&img[0], img.size());
            images.push_back(img);
        }
    }
};

//------------------------------------------------------------------------------
/// Image service: streams a sequence of images
class ImageService : public SessionService< wsp::Context< Images > > {
    using Context = wsp::Context< Images >;
public:
    using DataFrame = SessionService::DataFrame;
    ImageService(Context* c) :
     SessionService(c), ctx_(c), frameCounter_(0) {
        InitDataFrame();
    }
    bool Data() const override { return true; }
    //return data frame and update frame end
    const DataFrame& Get(int requestedChunkLength) {
        if(df_.frameEnd < df_.bufferEnd) {
           //frameBegin *MUST* be updated in the UpdateOutBuffer method
           //because in case the consumed data is less than requestedChunkLength
           // 
           //df_.frameBegin = df_.frameEnd;
           df_.frameEnd += min((ptrdiff_t) requestedChunkLength, 
                               df_.bufferEnd - df_.frameEnd);
        } else {
            frameCounter_ = (frameCounter_ + 1) 
                            % ctx_->GetServiceData().images.size();  
            InitDataFrame();
        }
        return df_;  
    }
    //update frame begin/end
    void UpdateOutBuffer(int bytesConsumed) {
        df_.frameBegin += bytesConsumed;
        df_.frameEnd = df_.frameBegin;
    }
    //streaming: always in send mode, no receive
    bool Sending() const override { return true; }
    void Put(void* p, size_t len, bool done) override {}
    std::chrono::duration< double > 
    MinDelayBetweenWrites() const {
        return std::chrono::duration< double >(0.001);
    }
private:
    void InitDataFrame() {
        df_.bufferBegin = &(ctx_->GetServiceData().images[frameCounter_][0]);
        df_.bufferEnd = df_.bufferBegin 
                        + ctx_->GetServiceData().images[frameCounter_].size();
        df_.frameBegin = df_.bufferBegin;
        df_.frameEnd = df_.frameBegin;
        df_.binary = true;
    }    
private:    
    DataFrame df_;
    Context* ctx_ = nullptr;
    unsigned int frameCounter_ = 0;
};



//------------------------------------------------------------------------------
/// Stream sequence of images in a loop 
int main(int argc, char** argv) {
    if(argc < 5) {
        cout << "usage: " << argv[0] 
             << "<total number of images> <image full prefix "
                "e.g. /path/to/images/imageprefix> <number of images to load> "
                "<image file extension>"
             << endl;
        return 0;     
    }
    using namespace wsp;
    using WSS = WebSocketService;
    WSS ws;
    WSS::ResetLogLevels(); // clear all loggers
    //create and set logger for 'INFO' logs
    auto log = [](int level, const char* msg) {
        std::cout << WSS::Level(level) << "> " << msg << std::endl;
    };
    WSS::SetLogger(log, "NOTICE", "WARNING", "ERROR");
    Images images;
    images.Load(stoi(argv[1]), argv[2], stoi(argv[3]), argv[4]);
    //init service
    ws.Init(5000, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            Context< Images >(images), //context instance,
                                       //will be copied internally
            WSS::Entry< ImageService, WSS::ASYNC_REP >("image-stream"));
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(10, //ms
                 []{return true;} //termination condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}
