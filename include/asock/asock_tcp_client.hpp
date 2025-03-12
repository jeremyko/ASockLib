#ifndef ASOCKTCPCLIENT_HPP
#define ASOCKTCPCLIENT_HPP

#if defined __APPLE__ || defined __linux__
#include "asock/internal/asock_nix_client.hpp"
#elif WIN32
#include "asock/internal/asock_win_client.hpp"
#endif

namespace asock {
class ASockTcpClient: public ASockClientBase {
public :
    //-------------------------------------------------------------------------
    // - If you know the maximum data size you will be sending and receiving in advance,
    //   it is better to allocate a buffer large enough to match that.
    // - If you do not know the size in advance or if it exceeds the buffer, 
    //   dynamic memory allocation occurs internally. 
    bool InitTcpClient(const char* server_ip, unsigned short server_port,
                       int connect_timeout_secs=10,
                       size_t max_data_len = asock::DEFAULT_BUFFER_SIZE) {

#if defined __APPLE__ || defined __linux__
        connect_timeout_secs_ = connect_timeout_secs;
        SetUsage();
        if(!SetBufferCapacity(max_data_len) ) {
            return false;
        }
        client_ctx_.socket = socket(AF_INET,SOCK_STREAM,0);
        memset((void *)&tcp_server_addr_,0x00,sizeof(tcp_server_addr_));
        tcp_server_addr_.sin_family = AF_INET;
        tcp_server_addr_.sin_addr.s_addr = inet_addr(server_ip);
        tcp_server_addr_.sin_port = htons(server_port);
        return ConnectToServer();
#elif WIN32
        server_ip_   = server_ip ;
        server_port_ = server_port;
        is_connected_ = false;
        if(!InitWinsock()) {
            return false;
        }
        connect_timeout_secs_ = connect_timeout_secs;
        SetUsage();
        if (!SetBufferCapacity(max_data_len)) {
            return false;
        }
        if (client_ctx_.per_recv_io_ctx != NULL) {
            delete client_ctx_.per_recv_io_ctx;
        }
        client_ctx_.per_recv_io_ctx = new (std::nothrow) PER_IO_DATA;
        if (client_ctx_.per_recv_io_ctx == nullptr) {
            DBG_ELOG("mem alloc failed");
            return false;
        }
        client_ctx_.socket = socket(AF_INET, SOCK_STREAM, 0);
        memset((void *)&tcp_server_addr_, 0x00, sizeof(tcp_server_addr_));
        tcp_server_addr_.sin_family = AF_INET;
        inet_pton(AF_INET, server_ip_.c_str(), &(tcp_server_addr_.sin_addr));
        tcp_server_addr_.sin_port = htons(server_port_);
        return ConnectToServer();
#endif
    }

    void SetUsage() override {
        sock_usage_ = SOCK_USAGE_TCP_CLIENT  ;
    }
};
} //namespace
#endif
