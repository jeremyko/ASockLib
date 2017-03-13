
///////////////////////////////////////////////////////////////////////////////

#ifndef __A_SOCKET_BASE_HPP__
#define __A_SOCKET_BASE_HPP__

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <signal.h>

#ifdef __APPLE__
#include <sys/select.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#elif __linux__
#include <sys/epoll.h>
#endif

#include <string>
#include <mutex> 
#include "CumBuffer.h"

typedef struct  sockaddr_in SOCKADDR_IN ;
typedef struct  sockaddr    SOCKADDR ;
typedef         socklen_t   SOCKLEN_T ;

namespace asocklib
{
    const int       DEFAULT_PACKET_SIZE =2048;
    const int       DEFAULT_CAPACITY    =2048;
    const size_t    MORE_TO_COME        = -1;

    ///////////////////////////////////////////////////////////////////////////////
    class Context 
    {
        public :

            CumBuffer       recvBuffer_;
            CumBuffer       sendBuffer_; 
            int             socket_{-1};
            std::mutex      clientSendLock_; 
    };
} //namespace asocklib

using Context = asocklib::Context ;
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

#ifdef __APPLE__
        //kqueue 
        struct          kevent*      pKqEvents_{nullptr};
        int             nKqfd_          {-1};
#elif __linux__
        //epoll
        struct          epoll_event* pEpEvents_{nullptr};
        int             nEpfd_          {-1};
#endif

#if defined __APPLE__ || defined __linux__ 
        Context*        pContextListen_{nullptr};
#endif

    protected :
        bool            Recv(Context* pContext);
        bool            Send(Context* pContext, const char* pData, int nLen); 
#ifdef __APPLE__
        bool            KqueueCtl(Context* pContext , uint32_t events, uint32_t fflags);
#elif __linux__
        bool            EpollCtl (Context* pContext , uint32_t events, int op);
#endif

    private:
        virtual size_t  GetOnePacketLength(Context* pContext)=0; 
        virtual bool    OnRecvOnePacketData(Context* pContext, char* pOnePacket, int nPacketLen)=0; 
};

#endif 


