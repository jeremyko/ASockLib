#ifndef ASOCKIPCCLIENT_HPP
#define ASOCKIPCCLIENT_HPP

#if defined __APPLE__ || defined __linux__
#include "asock/internal/asock_nix.hpp"
#elif WIN32
#include "asock/internal/asock_win.hpp"
#endif

namespace asock {
class ASockIpcClient: public asock::ASockBase {
public :
#if defined __APPLE__ || defined __linux__
    // - If you know the maximum data size you will be sending and receiving in advance, 
    //   it is better to allocate a buffer large enough to match that.
    // - If you do not know the size in advance or if it exceeds the buffer, 
    //   dynamic memory allocation occurs internally. 
    bool InitIpcClient(const char* sock_path,
                       int connect_timeout_secs=10,
                       size_t max_data_len=asock::DEFAULT_BUFFER_SIZE) {
        connect_timeout_secs_ = connect_timeout_secs;
        SetUsage();
        if(!SetBufferCapacity(max_data_len)) {
            return false;
        }
        server_ipc_socket_path_ = sock_path;
        context_.socket = socket(AF_UNIX,SOCK_STREAM,0);
        memset((void *)&ipc_conn_addr_,0x00,sizeof(ipc_conn_addr_));
        ipc_conn_addr_.sun_family = AF_UNIX;
        snprintf(ipc_conn_addr_.sun_path, sizeof(ipc_conn_addr_.sun_path),
                 "%s",sock_path); 
        return ConnectToServer();
    }

    void SetUsage() override {
        sock_usage_ = SOCK_USAGE_IPC_CLIENT;
    }
#elif WIN32
    // TODO: Not implemented on Windows.
    bool InitIpcClient(const char* sock_path,
                       int connect_timeout_secs=10,
                       size_t max_data_len=asock::DEFAULT_BUFFER_SIZE) {
        err_msg_ = "not implemented";
        ELOG(err_msg_);
        return false;
    }

    void SetUsage() override {
        err_msg_ = "not implemented";
        ELOG(err_msg_);
        sock_usage_ = SOCK_USAGE_UNKNOWN;
    }
#endif
};
} //namespace
#endif
