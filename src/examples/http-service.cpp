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

//g++ -std=c++11 ../src/WebSocketService.cpp ../src/http.cpp ../src/mimetypes.cpp ../src/examples/example-http.cpp -L /usr/local/libwebsockets/lib -I /usr/local/libwebsockets/include -lwebsockets -pthread -O3 -o http.exe

//PLACE HOLDER FOR DEFAULT HTTP SERVICE IMPLEMENTATION

#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstring>
#include <sstream>
#include "../WebSocketService.h"
#include "../Context.h"
#include "../DataFrame.h"
#include "../http.h"

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

std::string GetHomeDir() {
    const std::string h 
#ifdef WIN32
        = std::string(getenv("HOMEDRIVE")
                      + "/" //passing path to libwebsockets no need to convert
                      + std::string(getenv("HOMEPATH"));
#else
        = getenv("HOME");
#endif 
    return h;       
}

class HttpService {
public:
    using HTTP = int; //mark as http service
    using DataFrame = wsp::DataFrame;
    HttpService(wsp::Context<>* , const char* req, size_t len,
                const wsp::Request& m) :
    df_(nullptr, nullptr, nullptr, nullptr, false), reqHeader_(m) {
        request_.resize(len + 1);
        request_.assign(req, req + len);
        request_.push_back('\0');
        const string filePathRoot = GetHomeDir();
        std::string uri;
        if(wsp::Has(m, "GET URI")) uri = "GET URI";
        else if(wsp::Has(m, "POST URI")) uri = "POST URI";
        if(!uri.empty()) {
            if(!wsp::FileExtension(wsp::Get(m, uri)).empty()) {
                mimeType_ = wsp::GetMimeType(
                    wsp::FileExtension(wsp::Get(m, uri)));
                filePath_ = filePathRoot + wsp::Get(m, uri);
            } else ComposeResponse(MapToString(m) + "<br/>" + string(BODY)); 
        }
        
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
    const string& FileMimeType() const { return mimeType_; }
    void Destroy() {}
    void ReceiveStart(size_t len, void* in) {}
    void Receive(size_t len, void* in) {}
    void ReceiveComplete(int len, void* in) {}
private:
    void InitDataFrame() const {
        df_.bufferBegin = &response_[0];
        df_.bufferEnd = &response_[0] + response_.size();
        df_.frameBegin = df_.bufferBegin;
        df_.frameEnd = df_.frameBegin;
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
    //HTTP SERVICE MUST ALWAYS BE THE FIRST !!!!
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

