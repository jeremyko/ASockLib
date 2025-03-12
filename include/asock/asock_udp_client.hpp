#ifndef ASOCKUDPCLIENT_HPP
#define ASOCKUDPCLIENT_HPP

#if defined __APPLE__ || defined __linux__
#include "asock/internal/asock_nix_client.hpp"
#elif WIN32
#include "asock/internal/asock_win_client.hpp"
#endif

namespace asock {
class ASockUdpClient: public ASockClientBase {
public :
    virtual ~ASockUdpClient(){}
    // In case of UDP, you need to know the maximum receivable size in advance and allocate a buffer.
    bool InitUdpClient(const char* server_ip, unsigned short server_port, size_t  max_data_len) {

#if defined __APPLE__ || defined __linux__
        SetUsage();
        if(!SetBufferCapacity(max_data_len) ) {
            return false;
        }
        client_ctx_.socket = socket(AF_INET,SOCK_DGRAM,0);
        memset((void *)&udp_server_addr_,0x00,sizeof(udp_server_addr_));
        udp_server_addr_.sin_family  = AF_INET;
        udp_server_addr_.sin_addr.s_addr = inet_addr(server_ip);
        udp_server_addr_.sin_port = htons(server_port);
        return ConnectToServer();
#elif WIN32
        SetUsage();
        server_ip_   = server_ip ;
        server_port_ = server_port;
        is_connected_ = false;
        if(!InitWinsock()) {
            return false;
        }
        if(!SetBufferCapacity(max_data_len) ) {
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
        client_ctx_.socket = socket(AF_INET, SOCK_DGRAM, 0);
        memset((void *)&udp_server_addr_, 0x00, sizeof(udp_server_addr_));
        udp_server_addr_.sin_family = AF_INET;
        inet_pton(AF_INET, server_ip_.c_str(), &(udp_server_addr_.sin_addr));
        udp_server_addr_.sin_port = htons(server_port_);
        return ConnectToServer();
#endif
    }

    void SetUsage() override {
        sock_usage_ = SOCK_USAGE_UDP_CLIENT;
    }
};
} //namespace
#endif
