//author: Ugo Varetto
#include "WebSocketService.h"

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