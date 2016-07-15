//
// Created by Ugo Varetto on 7/13/16.
//
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <csignal>

#include <libwebsockets.h>

namespace {

    lws_context* contextG = nullptr;

void sighandler(int sig) {
    force_exit = 1;
    lws_cancel_service(contextG);
}
}

using namespace std;
int main(int, char**) {

    const int PORT = 7777;
    const int DEBUG_LEVEL = 7;
    static const string INTERFACE_NAME = "req-rep";

    vector< lws_protocols > protocols = {
    };


    signal(SIGINT, sighandler);
    lws_set_log_level(DEBUG_LEVEL, lwsl_emit_syslog);

    lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = PORT
    info.iface = INTERFACE_NAME.c_str();
    info.protocols = protocols;
    info.ssl_cert_filepath = NULL;
    info.ssl_private_key_filepath = NULL;


    return EXIT_SUCCESS;
}




