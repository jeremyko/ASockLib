/******************************************************************************
MIT License

Copyright (c) 2025 jung hyun, ko

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 *****************************************************************************/

#ifndef ASOCKCOMM_HPP
#define ASOCKCOMM_HPP

#include <deque>
#include <mutex>
#include <string.h>

//-----------------------------------------------------------------------------
#if defined __APPLE__ || defined __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/un.h>

#ifdef __APPLE__
#include <sys/select.h>
#include <sys/types.h>
#include <sys/event.h>
#endif

#if __linux__
#include <sys/epoll.h>
#endif

#endif //__APPLE__ , __linux__

//-----------------------------------------------------------------------------
#ifdef WIN32
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <afunix.h> 
#endif //WIN32

//-----------------------------------------------------------------------------
#include "asock_buffer.hpp"

namespace asock {
typedef struct  sockaddr_in SOCKADDR_IN ;
typedef struct  sockaddr    SOCKADDR ;
const size_t  DEFAULT_MAX_EVENT =100;
const size_t  MORE_TO_COME        =0;

typedef enum _ENUM_SOCK_USAGE_ {
    SOCK_USAGE_UNKNOWN = 0 ,
    SOCK_USAGE_TCP_SERVER ,
    SOCK_USAGE_UDP_SERVER ,
    SOCK_USAGE_IPC_SERVER , 
    SOCK_USAGE_TCP_CLIENT ,
    SOCK_USAGE_UDP_CLIENT ,
    SOCK_USAGE_IPC_CLIENT 
} ENUM_SOCK_USAGE ;

typedef struct _ST_HEADER_ {
    char msg_len[10];
} ST_HEADER ;
#define HEADER_SIZE sizeof(ST_HEADER)

const size_t  DEFAULT_BUFFER_SIZE = HEADER_SIZE + 1400 ;

typedef struct _PENDING_SENT_ {
    char*       pending_sent_data ; 
    size_t      pending_sent_len  ;
    SOCKADDR_IN udp_remote_addr   ; //for udp pending sent 
} PENDING_SENT ;

//-----------------------------------------------------------------------------
#if defined __APPLE__ || defined __linux__ 
typedef struct  sockaddr_un SOCKADDR_UN ;
typedef         socklen_t   SOCKLEN_T ;
typedef int SOCKET_T;
typedef struct _Context_ {
    SOCKET_T        socket  {-1};
    CumBuffer* GetBuffer() {
        return & recv_buffer;
    }
    CumBuffer       recv_buffer;
    std::mutex      ctx_lock ; 
    bool            is_packet_len_calculated {false};
    size_t          complete_packet_len {0} ;
    std::deque<PENDING_SENT> pending_send_deque ; 
    bool            is_sent_pending {false}; 
    bool            is_connected {false}; 
    SOCKADDR_IN     udp_remote_addr ; //for udp
} Context ;
#endif //__APPLE__ , __linux__

//-----------------------------------------------------------------------------
#ifdef WIN32
typedef int SOCKLEN_T ;
typedef     SOCKET SOCKET_T;

enum class EnumIOType {
    IO_ACCEPT,
    IO_SEND,
    IO_RECV,
    IO_QUIT,
    IO_UNKNOWN
};

typedef struct _PER_IO_DATA_ {
    OVERLAPPED  overlapped ;
    WSABUF      wsabuf;
    EnumIOType  io_type;
    CumBuffer   cum_buffer;
    size_t      total_send_len {0} ;
    size_t      complete_recv_len {0} ;
    size_t      sent_len {0} ; //XXX
    bool        is_packet_len_calculated {false};
} PER_IO_DATA; //XXX 1200 bytes.... 

typedef struct _Context_ {
    SOCKET_T  socket{INVALID_SOCKET};
    SOCKET_T  sock_id_copy{INVALID_SOCKET};
    PER_IO_DATA* per_recv_io_ctx { NULL }; 
    std::mutex   ctx_lock ; 
    std::atomic<int>  recv_ref_cnt{ 0 }; 
    std::atomic<int>  send_ref_cnt{ 0 }; 
    CumBuffer* GetBuffer() {
        return & (per_recv_io_ctx->cum_buffer);
    }
    bool         is_connected {false}; 
    SOCKADDR_IN  udp_remote_addr ; //for udp
    std::deque<PENDING_SENT> pending_send_deque ; //client¸¸ »ç¿ë
    bool         is_sent_pending {false}; 
} Context ;

#endif //WIN32

//-----------------------------------------------------------------------------
#define LOG_WHERE "("<<__FILE__<<":"<<__func__<<":"<<__LINE__<<") "

#if defined __APPLE__ || defined __linux__ 
#define COLOR_RED  "\x1B[31m"
#define COLOR_GREEN "\x1B[32m"
#define COLOR_BLUE "\x1B[34m"
#define COLOR_RESET "\x1B[0m"
#endif //__APPLE__ , __linux__

//-----------------------------------------------------------------------------
#ifdef DEBUG_PRINT
#if defined __APPLE__ || defined __linux__ 
#define DBG_LOG(x) std::cout<<LOG_WHERE << COLOR_BLUE<< x << COLOR_RESET << "\n"
#define DBG_ELOG(x) std::cerr<<LOG_WHERE << COLOR_RED<< x << COLOR_RESET << "\n"
#define DBG_GREEN_LOG(x) std::cout<<LOG_WHERE << COLOR_GREEN<< x << COLOR_RESET << "\n"
#define DBG_BLUE_LOG(x) std::cout<<LOG_WHERE << COLOR_BLUE<< x << COLOR_RESET << "\n"
#define DBG_RED_LOG(x) std::cout<<LOG_WHERE << COLOR_RED<< x << COLOR_RESET << "\n"
#endif //__APPLE__ , __linux__

//windows --> no color support
#ifdef WIN32
#define  DBG_LOG(x)  std::cout<<LOG_WHERE << x << "\n"
#define  DBG_ELOG(x) std::cerr<<LOG_WHERE << x << "\n"
#define DBG_GREEN_LOG(x) std::cout<<LOG_WHERE << x << "\n"
#define DBG_BLUE_LOG(x) std::cout<<LOG_WHERE << x << "\n"
#define DBG_RED_LOG(x) std::cout<<LOG_WHERE << x << "\n"
#endif // WIN32

#else // --- DEBUG_PRINT
#define  DBG_LOG(x)
#define  DBG_ELOG(x)
#define  DBG_RED_LOG(x)
#define  DBG_BLUE_LOG(x)
#define  DBG_GREEN_LOG(x)
#endif //DEBUG_PRINT

//-----------------------------------------------------------------------------
#define LOG(x)  std::cout<<LOG_WHERE << x << "\n"

#if defined __APPLE__ || defined __linux__ 
#define ELOG(x) std::cerr<<LOG_WHERE << "error : " << COLOR_RED<< x << COLOR_RESET << "\n"
#endif //__APPLE__ , __linux__

#ifdef WIN32
#define ELOG(x) std::cerr<<LOG_WHERE << x << "\n"
#endif //__APPLE__ , __linux__

} //namespace asock
#endif

