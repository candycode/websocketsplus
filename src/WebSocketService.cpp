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

#include "WebSocketService.h"
#include <algorithm>
#include <sstream>

namespace wsp {


std::function< void (int, const char*) > WebSocketService::logger_;    
const std::map< lws_log_levels, std::string > WebSocketService::levels_ = 
                                                    {{LLL_ERR,    "ERROR"},
                                                     {LLL_WARN,   "WARNING"},
                                                     {LLL_NOTICE, "NOTICE"},
                                                     {LLL_INFO,   "INFO"},
                                                     {LLL_DEBUG,  "DEBUG"},
                                                     {LLL_PARSER, "HEADER"},
                                                     {LLL_EXT,    "EXTENSION"},
                                                     {LLL_CLIENT, "CLIENT"},
                                                     {LLL_LATENCY,"LATENCY"}};
const std::map< std::string, lws_log_levels > WebSocketService::levelNames_ = 
                                                    {{"ERROR", LLL_ERR},
                                                     {"WARNING", LLL_WARN},
                                                     {"NOTICE", LLL_NOTICE},
                                                     {"INFO", LLL_INFO},
                                                     {"DEBUG", LLL_DEBUG},
                                                     {"HEADER", LLL_PARSER},
                                                     {"EXTENSION", LLL_EXT},
                                                     {"CLIENT", LLL_CLIENT},
                                                     {"LATENCY", LLL_LATENCY}};

std::unordered_map< std::string, std::string >
ParseHttpHeader(libwebsocket *wsi) {
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
    std::unordered_map< std::string, std::string > hm;
    for (int n = 0; n < sizeof(tokenNames) / sizeof(tokenNames[0]); ++n) {
        if(!lws_hdr_total_length(wsi, lws_token_indexes(n))) continue;         
        lws_hdr_copy(wsi, &buf[0], buf.size(), lws_token_indexes(n));
        hm[tokenNames[n]] = &buf[0];
    }
    return hm;
}


std::string MapToString(
    const std::unordered_map< std::string, std::string >& m,
    const std::string& pre,
    const std::string& post) {
    std::ostringstream oss;
    for(auto& i: m) {
        oss << pre << i.first << ": " << i.second << post;
    }
    return oss.str();
}


} //namespace wsp