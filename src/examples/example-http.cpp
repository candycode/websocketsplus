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

//Http service example

#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstring>
#include <sstream>
#include "../WebSocketService.h"
#include "../Context.h"
#include "../DataFrame.h"

using namespace std;


std::string MapToString(
    const std::unordered_map< std::string, std::string >& m,
    const std::string& pre = "",
    const std::string& post = "<br/>") {
    std::ostringstream oss;
    for(auto& i: m) {
        oss << pre << i.first << ": " << i.second << post;
    }
    return oss.str();
}

class HttpService {
public:
    static const int HTTP = 1; //value can be anything; HTTP member checked
                               //at compile time to determine if service is
                               //http
    using DataFrame = wsp::DataFrame;
    HttpService(wsp::Context<>* , const char* req, size_t len,
                const unordered_map< string, string >& m) :
    df_(nullptr, nullptr, nullptr, nullptr, false), reqHeader_(m) {
        request_.resize(len + 1);
        request_.assign(req, req + len);
        request_.push_back('\0');
#if SERVE_FILE 
        filePath_ = "../src/examples/example-streaming.html";
        mimeType_ = "text/html";
#else        
        ComposeResponse(MapToString(m) + "<br/>" + string(BODY));
#endif        
    }
    //Constructor(Context, unordered_map<string, string> headers)
    bool Valid() const { return true; }
    //return data frame and update frame end
    const DataFrame& Get(int requestedChunkLength) {
        if(df_.frameEnd < df_.bufferEnd) {
           sending_ = true;
           //frameBegin *MUST* be updated in the UpdateOutBuffer method
           //because in case the consumed data is less than requestedChunkLength
           df_.frameEnd += min((ptrdiff_t) requestedChunkLength, 
                               df_.bufferEnd - df_.frameEnd);
        } else {
           InitDataFrame();
           sending_ = false;
        }
        return df_;  
    }
    bool Sending() const { return sending_; }
    //update frame begin/end
    void UpdateOutBuffer(int bytesConsumed) {
        df_.frameBegin += bytesConsumed;
        df_.frameEnd = df_.frameBegin;
    }
    bool Data() const { return true; }
    int GetSuggestedOutChunkSize() const { return 0x1000; }
    const string& FilePath() const { return filePath_; }
    const string& Headers() const { return headers_; }
    const string& FileMimeType() const { return mimeType_; }
private:
    void InitDataFrame() const {
        df_.bufferBegin = &response_[0];
        df_.bufferEnd = &response_[0] + response_.size();
        df_.frameBegin = df_.bufferBegin;
        df_.frameEnd = df_.frameBegin;
    }
    void ParseFilePath(const std::vector< char >& req) {
        // std::vector< char >::reverse_const_iterator i = 
        //     std::find(req.rbegin(), req.rend(), '.');
        // if(i == req.rend()) return;
        // const char* c = &(--i.base());
        // const std::string ext = (c + 1, &req[req.size() - 1]);
        // if(ext == "jpg") {
        //     mimeType_ = "image/jpeg";
        // } 

    }    
    void ComposeResponse(const std::string& data) {
         const string h = string("HTTP/1.0 200 OK\x0d\x0a"
                         "Server: websockets+\x0d\x0a"
                         "Content-Type: text/html\x0d\x0a" 
                         "Content-Length: ") 
                         + to_string(data.size())
                         + string("\x0d\x0a\x0d\x0a") + data;   

        response_.resize(h.size());
        response_.assign(h.begin(), h.end());
        InitDataFrame();   
    }
private:
    mutable bool sending_ = false; 
    string filePath_;
    string mimeType_;
    string headers_;
    vector< char > request_;
    vector< char > response_;
    std::unordered_map< string, string > reqHeader_;
    static const char* BODY;
    mutable DataFrame df_;
};

const char* HttpService::BODY =
        "<!DOCTYPE html><head></head><html><body><p><em>Hello</em></p></body><"
        "/html>"; 
//------------------------------------------------------------------------------
int main(int, char**) {
    using WSS = wsp::WebSocketService;
    WSS ws;
    using Service = HttpService;
    //init service
    ws.Init(8001, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            wsp::Context<>(), //context instance, will be copied internally
            WSS::Entry< Service, WSS::ASYNC_REP >("http-only"));
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(50, //ms
                 []{return true;} //termination condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
    return 0;
}

