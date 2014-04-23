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

#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <sstream>

#include <libwebsockets.h>

#include "http.h"

namespace wsp {
Request ParseHttpHeader(libwebsocket *wsi) {
    //copied from test-server.c part of libwebsockets distribution.
    static const char *tokenNames[] = {
        /*[WSI_TOKEN_GET_URI]       =*/ "GET URI",
        /*[WSI_TOKEN_POST_URI]      =*/ "POST URI",
        /*[WSI_TOKEN_HOST]      =*/ "Host",
        /*[WSI_TOKEN_CONNECTION]    =*/ "Connection",
        /*[WSI_TOKEN_KEY1]      =*/ "key 1",
        /*[WSI_TOKEN_KEY2]      =*/ "key 2",
        /*[WSI_TOKEN_PROTOCOL]      =*/ "Protocol",
        /*[WSI_TOKEN_UPGRADE]       =*/ "Upgrade",
        /*[WSI_TOKEN_ORIGIN]        =*/ "Origin",
        /*[WSI_TOKEN_DRAFT]     =*/ "Draft",
        /*[WSI_TOKEN_CHALLENGE]     =*/ "Challenge",

        /* new for 04 */
        /*[WSI_TOKEN_KEY]       =*/ "Key",
        /*[WSI_TOKEN_VERSION]       =*/ "Version",
        /*[WSI_TOKEN_SWORIGIN]      =*/ "Sworigin",

        /* new for 05 */
        /*[WSI_TOKEN_EXTENSIONS]    =*/ "Extensions",

        /* client receives these */
        /*[WSI_TOKEN_ACCEPT]        =*/ "Accept",
        /*[WSI_TOKEN_NONCE]     =*/ "Nonce",
        /*[WSI_TOKEN_HTTP]      =*/ "Http",
        "Accept:",
        "If-Modified-Since:",
        "Accept-Encoding:",
        "Accept-Language:",
        "Pragma:",
        "Cache-Control:",
        "Authorization:",
        "Cookie:",
        "Content-Length:",
        "Content-Type:",
        "Date:",
        "Range:",
        "Referer:",
        "Uri-Args:",
        /*[WSI_TOKEN_MUXURL]    =*/ "MuxURL",
    };
    std::vector< char > buf(0x400);
    Request hm;
    for (int n = 0; n < sizeof(tokenNames) / sizeof(tokenNames[0]); ++n) {
        if(!lws_hdr_total_length(wsi, lws_token_indexes(n))) continue;         
        lws_hdr_copy(wsi, &buf[0], buf.size(), lws_token_indexes(n));
        hm[tokenNames[n]] = &buf[0];
    }
    return hm;
}


std::vector< std::string > Split(const std::string& p,
                                 const std::string& c) {
    std::vector< std::string > v;
    if(p.size() < 2) return v;
    size_t b = p.find(c);
    size_t e = p.find(c, b + c.size());
    while(b != std::string::npos) {
        v.push_back(std::string(p, b + c.size(), e));
        b = e + c.size();
        p.find(b, b + c.size());
    }
    return v;
}

URIPath UriPath(const std::string& p) {
    return Split(p, "/");   
}

URIParameters UriParameters(const std::string& p) {
    std::vector< std::string > params(Split(p, "&"));
    URIParameters m;
    for(auto& i: params) {
        const std::vector< std::string > kv = Split(i, "=");
        if(kv.size() < 1) throw std::range_error("Invalid URI parameter");
        m.insert({kv[0], kv.size() > 1 ? kv[1] : ""});
    }
    return m;
}

std::string FileExtension(const std::string& p) {
    const size_t d = p.find('.');
    if(d == std::string::npos) return "";
    return std::string(p, d + 1, std::string::npos);    
}

bool Has(const Request& req, const std::string& k) {
    if(req.find(k) == req.end()) return false;
    return true;
}

const std::string& Get(const Request& req, const std::string& k) {
    static const std::string emptyString;
    if(!Has(req, k)) return emptyString;
    return req.find(k)->second;
}

int GetContentSize(const Request& req) {
    if(!Has(req, "Content-Length:")) return -1;
    const std::string l = Get(req, "Content-length:");
    if(l.empty()) return -1;
    return std::stoi(l);
}

//note: when name=made_write_conn cookie lasts until the browser is closed
//      when name=reg_fb_gate=deleted and date in the past cookie is deleted
//      immediately by client 
std::string CreateCookie(const std::unordered_map< std::string, std::string>& m)
{
    std::ostringstream os;
    if(m.find("name") == m.end())
        throw std::logic_error("No name specified for cookie");
    if(m.find("value") == m.end())
        throw std::logic_error("No name value specified for cookie");
    os << m.find("name")->second << "=" << m.find("value")->second << ";";
    if(m.find("path") != m.end()) os << " Path=" << m.find("path")->second << ";";
    if(m.find("domain") != m.end()) os << " Domain=" << m.find("domain")->second << ";";
    if(m.find("expires") != m.end()) os << " Expires=" << m.find("expires")->second << ";";
    if(m.find("secure") != m.end()) os << " Secure;";
    if(m.find("http-only") != m.end()) os << " HttpOnly;";
    return os.str();
}

std::string CreateTime(int h, int m, int s) {
    using namespace std;
    using namespace std::chrono;
    const long int offsetInSeconds(24 * h + 60 * m + s);
    system_clock::time_point now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(
                                                now + seconds(offsetInSeconds));
    return ctime(&t);
}


}