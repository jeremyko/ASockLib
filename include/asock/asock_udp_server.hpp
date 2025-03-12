#ifndef ASOCKUDPSERVER_HPP
#define ASOCKUDPSERVER_HPP

#if defined __APPLE__ || defined __linux__
#include "asock/internal/asock_nix_server.hpp"
#elif WIN32
#include "asock/internal/asock_win_server.hpp"
#endif

namespace asock {
class ASockUdpServer: public ASockServerBase {
public :
    virtual ~ASockUdpServer(){}
    // In case of UDP, you need to know the maximum receivable size in advance
    // and allocate a buffer.
    bool RunUdpServer(const char* bind_ip, size_t bind_port, size_t max_data_len,
                      size_t max_event=asock::DEFAULT_MAX_EVENT) {
        SetUsage();
        server_ip_ = bind_ip;
        server_port_ = (int)bind_port;
        max_event_ = max_event;
        if(max_event_==0) {
            err_msg_="max event is 0";
            return false;
        }
        if(!SetBufferCapacity(max_data_len)) {
            return false;
        }
        return RunServer();
    }

    void SetUsage() override {
        sock_usage_ = SOCK_USAGE_UDP_SERVER  ;
    }
};
} //namespace
#endif
