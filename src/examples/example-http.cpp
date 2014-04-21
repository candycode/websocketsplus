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
#include <unordered_map>
#include <string>
#include <vector>
#include <cstring>
#include "../WebSocketService.h"
#include "../Context.h"
#include "../DataFrame.h"

using namespace std;

class HttpService {
public:
    static const int HTTP = 1; //value can be anything; HTTP member checked
                               //at compile time to determine if service is
                               //http
    using DataFrame = wsp::DataFrame;
    HttpService(wsp::Context<>* , const char* req, size_t len,
                const unordered_map< string, string >&) :
    df_(BODY, BODY + strlen(BODY), BODY, BODY, false) {
        request_.resize(len + 1);
        request_.assign(req, req + len);
        request_.back() = '\0';
    }
    //Constructor(Context, unordered_map<string, string> headers)
    bool Valid() const { return true; }
    void InitBody() {}
    const DataFrame& Get(int chunkSize) const {
        df_.frameBegin = df_.frameEnd;
        if(df_.frameBegin >= df_.bufferEnd) InitDataFrame();
        else {
            df_.frameEnd = min(df_.frameBegin + chunkSize, df_.bufferEnd);
        }
        return df_;
    }
    bool Data() const { return true; }
    int GetSuggestedOutChunkSize() const { return 0x1000; }
    const string& FilePath() const { return filePath_; }
    const string& Headers() const { return headers_; }
    const string& FileMimeType() const { return mimeType_; }
private:
    void InitDataFrame() const {
        df_.bufferBegin = BODY;
        df_.bufferEnd = BODY + strlen(BODY);
        df_.frameBegin = df_.bufferBegin;
        df_.frameEnd = df_.frameBegin;
    }    
private:
    string filePath_;
    string mimeType_;
    string headers_;
    vector< char > request_;
    static const char* BODY;
    mutable DataFrame df_;
};

const char* HttpService::BODY =
        "<!DOCTYPE html><html><body><p><em>Hello</em></p></body></html>"; 
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

