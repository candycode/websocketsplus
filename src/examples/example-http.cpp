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

//g++ -std=c++11 ../src/WebSocketService.cpp ../src/http.cpp ../src/mimetypes.cpp 
//../src/examples/example-http.cpp -L /usr/local/libwebsockets/lib 
//-I /usr/local/libwebsockets/include -lwebsockets -pthread -O3 -o http.exe

//clang++ -std=c++11 -I ../src -I /usr/local/libwebsockets/include  
//../src/examples/example-http.cpp ../src/WebSocketService.cpp 
//-L /usr/local/libwebsockets/lib -lwebsockets ../src/http.cpp ../src/mimetypes.cpp

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

///Http request handler: one instance per request
class HttpService {
public:
    using HTTP = int; //mark as http service
    using DataFrame = wsp::DataFrame;
    ///Constructor: one instance per http request created
    ///@param c context
    ///@param req request bytes
    ///@param request length in number of bytes
    ///@param m request stored as a key-value store
    HttpService(wsp::Context<>* c, const char* req, size_t len,
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
    ///Check if requested passed to constructor is valid
    bool Valid() const { return true; }
    ///Return data frame containing data to send to client
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
    ///Return @c true if still in sending phase, @c false otherwise
    bool Sending() const { return sending_; }
    ///Update data frame boundaries
    ///@param bytesConsumed number of bytes actually sent to client
    void UpdateOutBuffer(int bytesConsumed) {
        df_.frameBegin += bytesConsumed;
        df_.frameEnd = df_.frameBegin;
    }
    ///Check if data available
    bool Data() const { return true; }
    ///Return suggested chunk size; the returned value can be adapted to the
    ///current communication channel by e.g. storing the number of bytes
    ///requested in the Get() method and comparing the number with the actual
    ///bytes sent
    int GetSuggestedOutChunkSize() const { return 0x1000; }
    ///Return file path of file to send; return empty string if no file is sent
    const string& FilePath() const { return filePath_; }
    ///Return file mime type
    const string& FileMimeType() const { return mimeType_; }
    ///Cleanup resources: object is allocated through placement new so cleanup
    ///takes place only through this method
    void Destroy() {}
    ///Signals start of a receive operation through http POST; the actual
    ///request and header are passed to the constructor and can be stored
    ///accordingly without the need to handle them from within this method.
    ///@param len copied over from libwebsockets, can be discarded
    ///@param in copied over from libwebsockets, can be discarded
    void ReceiveStart(size_t len, void* in) {}
    ///Called with a new chunk of data after the start of an http POST operation
    ///@param len size in bytes of received buffer
    ///@param in received buffer
    void Receive(size_t len, void* in) {}
    ///Signals the end of a receive operation through http POST
    ///@param len copied over from libwebsockets, can be discarded
    ///@param in copied over from libwebsockets, can be discarded
    void ReceiveComplete(int len, void* in) {}
private:
    ///Initialize output data frame boundaries
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

