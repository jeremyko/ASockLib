
#ifndef __ASOCK_HPP__
#define __ASOCK_HPP__

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
#include <atomic>
#include <thread>
#include <queue>
#include <unordered_map>
#include <mutex> 
#include "CumBuffer.h"

typedef struct  sockaddr_in SOCKADDR_IN ;
typedef struct  sockaddr    SOCKADDR ;
typedef         socklen_t   SOCKLEN_T ;


namespace asocklib
{
    const int       DEFAULT_PACKET_SIZE =4096;
    const int       DEFAULT_CAPACITY    =4096;
    const size_t    MORE_TO_COME        = -1;

    typedef struct _Context_
    {
        CumBuffer       recvBuffer_;
        int             socket_{-1};
        std::mutex      clientSendLock_; 
        bool            bPacketLenCalculated {false};
        size_t          nOnePacketLength {0} ;
    } Context ;

    typedef enum _ENUM_SOCK_USAGE_
    {
        SOCK_USAGE_UNKNOWN = 0 ,
        SOCK_USAGE_TCP_SERVER ,
        SOCK_USAGE_IPC_SERVER ,
        SOCK_USAGE_TCP_CLIENT ,
        SOCK_USAGE_IPC_CLIENT 

    } ENUM_SOCK_USAGE ;
} 

using Context = asocklib::Context ;
using ENUM_SOCK_USAGE = asocklib::ENUM_SOCK_USAGE ;
using CLIENT_UNORDERMAP_T      = std::unordered_map<int, Context*> ;
using CLIENT_UNORDERMAP_ITER_T = std::unordered_map<int, Context*>::iterator ;

///////////////////////////////////////////////////////////////////////////////
class ASock
{
    public :

        ASock();
        virtual ~ASock()  ;

        bool            SetBufferCapacity(int nMaxMsgLen);
        std::string     GetLastErrMsg(){return strErr_; }
        bool            SetNonBlocking(int nSockFd);

    protected :
        std::string     strErr_ ;
        char            szRecvBuff_     [asocklib::DEFAULT_PACKET_SIZE];
        char*           OnePacketDataPtr_ {nullptr}; 
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
        ENUM_SOCK_USAGE SockUsage_ {asocklib::SOCK_USAGE_UNKNOWN};

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
        //TODO : composition support

    // CLIENT -------------------------------------------------
    public :
        bool            InitTcpClient(  const char* connIP, 
                                        int         nPort, 
                                        int         nConnectTimeoutSecs=10, 
                                        int         nMaxMsgLen = 0 );
        //bool            InitIpcClient(const char* sock_path); //TODO
        bool            SendToServer (const char* packet, int sendSize) ; 
        void            Disconnect() ;
        int             GetSocket () { return  context_.socket_ ; }
        bool            IsConnected() { return bConnected_;}

    private :
        std::atomic<bool>    bClientThreadRunning_ {false};
        bool            bCumBufferInit_ {false};
        SOCKADDR_IN     connAddr_ ;
        bool            bConnected_ {false};
        Context         context_;

    private :
        void            ClientThreadRoutine();
        virtual void    OnDisConnected() {}; 

    // SERVER -------------------------------------------------
    public :
        bool            InitTcpServer (const char* connIP, int nPort, int nMaxClient, int nMaxMsgLen=0);
        //bool            InitIpcServer (const char* sock_path, int nMaxClient, int nMaxMsgLen=0); //TODO
        bool            RunServer();
        bool            IsServerRunnig(){return bServerRunning_;};
        void            StopServer();
        int             GetMaxClientNum(){return nMaxClientNum_ ; };
        int             GetCountOfClients();

    private :
        int                  nCores_         {0};
        SOCKADDR_IN          serverAddr_  ;
        std::string          strServerIp_    {""};
        int                  listen_socket_  {-1};
        int                  nMaxClientNum_  {-1};
        int                  nServerPort_    {-1};
        std::atomic<int>     nClientCnt_     {0}; 
        std::atomic<bool>    bServerRun_     {false};
        std::atomic<bool>    bServerRunning_ {false};

        CLIENT_UNORDERMAP_T     clientMap_;
        std::queue<Context*>    clientInfoCacheQueue_;

    private :
        void            ServerThreadRoutine(int nCoreIndex);
        void            TerminateClient(int nClientIndex, Context* pClientContext);
        Context*        PopClientContextFromCache();
        void            PushClientInfoToCache(Context* pClientContext);
        void            ClearClientInfoToCache();

        virtual void    OnClientConnected(Context* pClientContext) {}; 
        virtual void    OnClientDisConnected(Context* pClientContext) {} ;  
};

#endif 


