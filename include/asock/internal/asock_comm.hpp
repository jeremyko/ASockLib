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

#include <string.h>

typedef struct  sockaddr_in SOCKADDR_IN ;
typedef struct  sockaddr    SOCKADDR ;

namespace asock {
#define COLOR_RED  "\x1B[31m"
#define COLOR_GREEN "\x1B[32m" 
#define COLOR_BLUE "\x1B[34m"
#define COLOR_RESET "\x1B[0m"

#define LOG_WHERE "("<<__FILE__<<":"<<__func__<<":"<<__LINE__<<") "
#define LOG(x)  std::cout<<LOG_WHERE << x << "\n"

#if defined __APPLE__ || defined __linux__ 
#define ELOG(x) std::cerr<<LOG_WHERE << "error : " << COLOR_RED<< x << COLOR_RESET << "\n"
#endif //__APPLE__ , __linux__

#ifdef WIN32
#define ELOG(x) std::cerr<<LOG_WHERE << x << "\n"
#endif //__APPLE__ , __linux__

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

const char* GetSockUsageName(ENUM_SOCK_USAGE usage) {
    switch (usage) {
        case SOCK_USAGE_TCP_SERVER: 
            return "Tcp Server";
        case SOCK_USAGE_UDP_SERVER: 
            return "Udp Server";
        case SOCK_USAGE_IPC_SERVER: 
            return "Ipc Server";
        case SOCK_USAGE_TCP_CLIENT: 
            return "Tcp Client";
        case SOCK_USAGE_UDP_CLIENT: 
            return "Udp Client";
        case SOCK_USAGE_IPC_CLIENT: 
            return "Ipc Client";
        default:
            return "Unknown";
    }
}

typedef struct _ST_HEADER_ {
    char msg_len[10];
} ST_HEADER ;
#define HEADER_SIZE sizeof(ST_HEADER)

const size_t  DEFAULT_BUFFER_SIZE = HEADER_SIZE + 1400 ;

} //namespace asock 
#endif 

