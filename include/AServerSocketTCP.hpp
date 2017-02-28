
#ifndef __A_SERVER_SOCKET_TCP_HPP__
#define __A_SERVER_SOCKET_TCP_HPP__

#include "ASockBase.hpp"
#include <queue> 
#include <atomic> 
#include <unordered_map> 

using CLIENT_UNORDERMAP_T      = std::unordered_map<int, asocklib::Context*> ;
using CLIENT_UNORDERMAP_ITER_T = std::unordered_map<int, asocklib::Context*>::iterator ;

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
        std::string     strServerIp_    {""};
        int             listen_socket_  {-1};
        int             nMaxClientNum_  {-1};
        int             nServerPort_    {-1};
        std::atomic<int>     nClientCnt_     {0}; 
        std::atomic<bool>    bServerRun_     {false};
        std::atomic<bool>    bServerRunning_ {false};

        CLIENT_UNORDERMAP_T     clientMap_; 
        std::queue<asocklib::Context*>    clientInfoCacheQueue_;

    private :
        void            ServerThreadRoutine(int nCoreIndex);
        void            TerminateClient(int nClientIndex, asocklib::Context* pClientContext);
        asocklib::Context*        PopClientContextFromCache();
        void            PushClientInfoToCache(asocklib::Context* pClientContext);
        void            ClearClientInfoToCache();

        virtual void    OnClientConnected(asocklib::Context* pClientContext)=0; 
        virtual void    OnClientDisConnected(asocklib::Context* pClientContext)=0; 
};

#endif 

