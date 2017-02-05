
#ifndef __A_SERVER_SOCKET_TCP_HPP__
#define __A_SERVER_SOCKET_TCP_HPP__

#include "ASockBase.hpp"
#include <queue> 
#include <atomic> 
#include <unordered_map> 

#define ROLE_SERVER

using CLIENT_UNORDERMAP_T      = std::unordered_map<int, Context*> ;
using CLIENT_UNORDERMAP_ITER_T = std::unordered_map<int, Context*>::iterator ;

///////////////////////////////////////////////////////////////////////////////
class AServerSocketTCP : public ASockBase 
{
    public :
        AServerSocketTCP (){};
        AServerSocketTCP (const char* connIP, int nPort, int nMaxClient, int nMaxMsgLen=0);
        virtual ~AServerSocketTCP() ;

        bool            SetConnInfo (const char* connIP, int nPort, int nMaxClient, int nMaxMsgLen=0);
        bool            RunServer();
        bool            IsServerRunnig(){return bServerRunning_;};
        void            StopServer();
        int             GetMaxClientNum(){return nMaxClientNum_ ; };
        int             GetCountOfClients();

    private :
        int             nCores_         {0};
        SOCKADDR_IN     serverAddr_  ;
        string          strServerIp_    {""};
        int             listen_socket_  {-1};
        int             nMaxClientNum_  {-1};
        int             nServerPort_    {-1};
        atomic<int>     nClientCnt_     {0}; 
        atomic<bool>    bServerRun_     {false};
        atomic<bool>    bServerRunning_ {false};

        CLIENT_UNORDERMAP_T     clientMap_; 
        std::queue<Context*>    clientInfoCacheQueue_;

    private :
        void            ServerThreadRoutine(int nCoreIndex);
        void            TerminateClient(int nClientIndex, Context* pClientContext);
        Context*        PopClientContextFromCache();
        void            PushClientInfoToCache(Context* pClientContext);
        void            ClearClientInfoToCache();

        //socket events
        virtual void    OnClientConnected(Context* pClientContext)=0; 
        virtual void    OnClientDisConnected(Context* pClientContext)=0; 
};

#endif 

