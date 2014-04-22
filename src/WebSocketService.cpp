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


} //namespace wsp