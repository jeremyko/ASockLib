#ifndef ASOCKIPCSERVER_HPP
#define ASOCKIPCSERVER_HPP

#if defined __APPLE__ || defined __linux__
#include "asock/internal/asock_nix_server.hpp"
#elif WIN32
#include "asock/internal/asock_win_server.hpp"
#endif

namespace asock {
class ASockIpcServer: public ASockServerBase {
public :
    // - If you know the maximum data size you will be sending and receiving in advance, 
    //   it is better to allocate a buffer large enough to match that.
    // - If you do not know the size in advance or if it exceeds the buffer, 
    //   dynamic memory allocation occurs internally. 
    bool RunIpcServer (const char* sock_path, 
                       size_t max_data_len=asock::DEFAULT_BUFFER_SIZE,
                       size_t max_event=asock::DEFAULT_MAX_EVENT){
        server_ipc_socket_path_ = sock_path;
        max_event_ = max_event;
        if(max_event_==0){
            ELOG("max client is 0");
            return false;
        }
        SetUsage();
        if(!SetBufferCapacity(max_data_len)) {
            return false;
        }
        return RunServer();
    }

    void SetUsage() override {
        sock_usage_ = SOCK_USAGE_IPC_SERVER;
    }
};
} //namespace
#endif
