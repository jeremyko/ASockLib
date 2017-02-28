
///////////////////////////////////////////////////////////////////////////////

#ifndef __A_SOCKET_BASE_HPP__
#define __A_SOCKET_BASE_HPP__

#ifdef WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <stdio.h>
    
    #pragma comment(lib, "Ws2_32.lib")

#elif __APPLE__
    #include <errno.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <sys/un.h>
    #include <signal.h>
    
    #include <sys/select.h>
    #include <sys/types.h>
    #include <sys/event.h>
    #include <sys/time.h>

#elif __linux__
    #include <errno.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <sys/un.h>
    #include <signal.h>

    #include <sys/epoll.h>
#endif

#include <string>
#include <mutex> 
#include "CumBuffer.h"

typedef struct  sockaddr_in SOCKADDR_IN ;
typedef struct  sockaddr_un SOCKADDR_UN ;
typedef struct  sockaddr    SOCKADDR ;
typedef         socklen_t   SOCKLEN_T ;

namespace asocklib
{
    const int       DEFAULT_PACKET_SIZE =2048;
    const int       DEFAULT_CAPACITY    =2048;
    const size_t    MORE_TO_COME        = -1;
    //const int DEFAULT_CAPACITY    =10;

    typedef enum
    {
        SOCKET_OK    = 1, 
        SELECT_TIME_OUT ,
        RECONNECT       ,
        RETRY           ,
        BUFFERED        ,
        MAX_SOCKET_RETURN_TYPE
    } ENUM_SOCKET_RETURN_TYPE ;
};

///////////////////////////////////////////////////////////////////////////////
class Context 
{
    public :

        CumBuffer       recvBuffer_;
        CumBuffer       sendBuffer_; 
        int             socket_{-1};
        std::mutex      clientSendLock_; 
};

///////////////////////////////////////////////////////////////////////////////
class ASockBase
{
    public :

        ASockBase(){};
        ASockBase(int nMaxMsgLen);
        virtual ~ASockBase() =default ;
        bool            SetBufferCapacity(int nMaxMsgLen);
        std::string     GetLastErrMsg(){return strErr_; }
        bool            SetNonBlocking(int nSockFd);

    protected :
        std::string     strErr_ ;
        char            szRecvBuff_     [asocklib::DEFAULT_PACKET_SIZE];
        char            szOnePacketData_[asocklib::DEFAULT_PACKET_SIZE];
        int             nBufferCapcity_ {-1};

#ifdef WIN32
        //TODO

#elif __APPLE__
        //kqueue
        struct          kevent*      pKqEvents_{nullptr};
        int             nKqfd_          {-1};

#elif __linux__
        //epoll
        struct          epoll_event* pEpEvents_{nullptr};
        int             nEpfd_          {-1};
#endif

    protected :
        bool            Recv(Context* pContext);
        bool            Send(Context* pContext, const char* pData, int nLen); 

#ifdef WIN32
        //TODO

#elif __APPLE__
        bool            KqueueCtl(int nFd , uint32_t events, uint32_t fflags);

#elif __linux__
        bool            EpollCtl (int nFd , uint32_t events, int op);
#endif

    private:
        virtual size_t  GetOnePacketLength(Context* pContext)=0; 
        virtual bool    OnRecvOnePacketData(Context* pContext, char* pOnePacket, int nPacketLen)=0; 
};

#endif 


#ifdef WIN32

#elif __APPLE__

#elif __linux__

#endif
