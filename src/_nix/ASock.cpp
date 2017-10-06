
#include "ASock.hpp"

using namespace asocklib ;

///////////////////////////////////////////////////////////////////////////////
ASock::ASock() 
{
    //SockUsage_ = SOCK_USAGE_UNKNOWN ;
}

///////////////////////////////////////////////////////////////////////////////
ASock::~ASock()
{
    if(OnePacketDataPtr_!=NULL)
    {
        delete [] OnePacketDataPtr_ ;
        OnePacketDataPtr_ = NULL;
    }

    if   (
            SockUsage_ == SOCK_USAGE_TCP_CLIENT || 
            SockUsage_ == SOCK_USAGE_IPC_CLIENT  
         ) 
    {
#ifdef __APPLE__
        if(pKqEvents_)
        {
            delete pKqEvents_;
        }
#elif __linux__
        if(pEpEvents_)
        {
            delete pEpEvents_;
        }
#endif

        Disconnect();
    }
    else if (
                SockUsage_ == SOCK_USAGE_TCP_SERVER || 
                SockUsage_ == SOCK_USAGE_IPC_SERVER  
            ) 
    {
#ifdef __APPLE__
        if ( pKqEvents_ )
        { 
            delete [] pKqEvents_;    
        }
#elif __linux__
        if (pEpEvents_)   
        { 
            delete [] pEpEvents_;    
        }
#endif

        CLIENT_UNORDERMAP_ITER_T itDel = clientMap_.begin();
        while (itDel != clientMap_.end()) 
        {
            delete itDel->second;
            itDel = clientMap_.erase(itDel);
        }

        ClearClientInfoToCache();

#ifdef __APPLE__
        KqueueCtl(pContextListen_, EVFILT_READ, EV_DELETE );
#elif __linux__
        EpollCtl (pContextListen_, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_DEL ); //just in case
#endif

#if defined __APPLE__ || defined __linux__ 
        delete pContextListen_ ;
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SetBufferCapacity(int nMaxMsgLen)
{
    if(nMaxMsgLen<0)
    {
        strErr_ = "length is negative";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< strErr_<<"\n";
        return false;
    }

    if(nMaxMsgLen==0) 
    {
        nBufferCapcity_ = asocklib::DEFAULT_CAPACITY ;
    }
    else
    {
        nBufferCapcity_ = nMaxMsgLen*2; 
    }

    OnePacketDataPtr_ = new (std::nothrow) char [nBufferCapcity_] ;
    if(OnePacketDataPtr_ == NULL)
    {
        strErr_ = "memory alloc failed!";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< strErr_<<"\n";
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool   ASock::SetNonBlocking(int nSockFd)
{
    int oldflags  ;

    if ((oldflags = fcntl( nSockFd,F_GETFL, 0)) < 0 )
    {
        strErr_ = "fcntl F_GETFL error [" + std::string(strerror(errno))+ "]";
        return  false;
    }

    int ret  = fcntl( nSockFd,F_SETFL,oldflags | O_NONBLOCK) ;
    if ( ret < 0 )
    {
        strErr_ = "fcntl O_NONBLOCK error [" + std::string(strerror(errno))+ "]";
        return  false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::Recv(Context* pContext) 
{
    int nToRecvLen = asocklib::DEFAULT_PACKET_SIZE ;
    if(asocklib::DEFAULT_PACKET_SIZE > pContext->recvBuffer_.GetLinearFreeSpace() )
    {
        nToRecvLen = pContext->recvBuffer_.GetLinearFreeSpace() ; 
    }

    if(nToRecvLen==0) 
    {
        strErr_ = "no linear free space left ";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< strErr_<<"\n";
        return false; 
    }

    int nRecvedLen = recv( pContext->socket_, pContext->recvBuffer_.GetLinearAppendPtr(), nToRecvLen, 0); 

    if( nRecvedLen > 0)
    {
        pContext->recvBuffer_.IncreaseData(nRecvedLen);

        while(pContext->recvBuffer_.GetCumulatedLen())
        {
            //invoke user specific implementation
            if(!pContext->bPacketLenCalculated )
            {
                //only when calculation is necessary
                pContext->nOnePacketLength = GetOnePacketLength( pContext ); 
                pContext->bPacketLenCalculated = true;
            }

            if(pContext->nOnePacketLength == asocklib::MORE_TO_COME)
            {
                pContext->bPacketLenCalculated = false;
                return true; //need to recv more
            }
            else if(pContext->nOnePacketLength > pContext->recvBuffer_.GetCumulatedLen())
            {
                return true; //need to recv more
            }
            else
            {
                //got complete packet 
                if(cumbuffer_defines::OP_RSLT_OK!=pContext->recvBuffer_.GetData(pContext->nOnePacketLength, OnePacketDataPtr_ ))
                {
                    //error !
                    strErr_ = pContext->recvBuffer_.GetErrMsg();
                    std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< strErr_ <<"\n";
                    pContext->bPacketLenCalculated = false;
                    return false; 
                }
                
                //invoke user specific implementation
                OnRecvOnePacketData(pContext, OnePacketDataPtr_ , pContext->nOnePacketLength ); 
                pContext->bPacketLenCalculated = false;
            }
        } //while
    }   
    else if( nRecvedLen == 0 )
    {
        strErr_ = "recv 0, client disconnected , fd:" + std::to_string(pContext->socket_);
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< strErr_<<"\n";
        return false ;
    }

    return true ;
}


///////////////////////////////////////////////////////////////////////////////
bool ASock::Send (Context* pClientContext, const char* pData, int nLen) 
{
    char* pDataPosition = const_cast<char*>(pData) ;   
    int nSendBytes = 0;           

    while( nSendBytes < nLen ) 
    {
        int nSent = send(pClientContext->socket_, pDataPosition, nLen-nSendBytes, 0);
        if(nSent > 0)
        {
            nSendBytes += nSent ;  
            pDataPosition += nSent ;      
        }
        else if( nSent < 0 )
        {
            if ( errno == EWOULDBLOCK || errno == EAGAIN )
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
                continue;

            }
            else if ( errno != EINTR )
            {
                strErr_ = "send error [" + std::string(strerror(errno)) + "]";
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< GetLastErrMsg() <<"\n"; 
                return false;
            }
        }
    }

    return true;
}

#ifdef __APPLE__
///////////////////////////////////////////////////////////////////////////////
bool ASock::KqueueCtl(Context* pContext , uint32_t events, uint32_t fflags)
{
    struct  kevent kEvent;
    memset(&kEvent, 0, sizeof(struct kevent));
    EV_SET(&kEvent, pContext->socket_, events,fflags , 0, 0, pContext); //udata = pContext

    int nRslt = kevent(nKqfd_, &kEvent, 1, NULL, 0, NULL);
    if (nRslt == -1)
    {
        strErr_ = "kevent error [" + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< GetLastErrMsg() <<"\n"; 
        return false; 
    }
    return true;
    //man:Re-adding an existing event will modify the parameters of the
    //    original event, and not result in a duplicate entry.
}
#elif __linux__
///////////////////////////////////////////////////////////////////////////////
bool ASock::EpollCtl(Context* pContext , uint32_t events, int op)
{
    struct  epoll_event evClient{};
    evClient.data.fd    = pContext->socket_;
    evClient.events     = events ;
    evClient.data.ptr   = pContext;

    if(epoll_ctl(nEpfd_, op, pContext->socket_, &evClient)<0)
    {
        strErr_ = "kevent error [" + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< GetLastErrMsg() <<"\n"; 
        return false; 
    }
    return true;
}
#endif


///////////////////////////////////////////////////////////////////////////////
// SERVER
///////////////////////////////////////////////////////////////////////////////
bool ASock::InitTcpServer ( const char* connIP, 
                            int         nPort, 
                            int         nMaxClient, 
                            int         nMaxMsgLen)
{
    SockUsage_ = SOCK_USAGE_TCP_SERVER  ;

    strServerIp_ = connIP ; 
    nServerPort_ = nPort ; 
    nMaxClientNum_ = nMaxClient ; 
    if(nMaxClientNum_<0)
    {
        return false;
    }

    return SetBufferCapacity(nMaxMsgLen);
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RunServer()
{
#ifdef __APPLE__
    if ( pKqEvents_ )
#elif __linux__
    //nCores_ = sysconf(_SC_NPROCESSORS_ONLN) ; //TODO
    if (pEpEvents_)   
#endif
    { 
        strErr_ = "error [server is already running]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }

    listen_socket_ = socket(AF_INET,SOCK_STREAM,0) ;
    if( listen_socket_ < 0 )
    {
        strErr_ = "init error [" + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }   

    if(!SetNonBlocking (listen_socket_))
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return  false;
    }

    int reuseaddr=1;
    int nRtn = -1;

    if (setsockopt(listen_socket_,SOL_SOCKET,SO_REUSEADDR,&reuseaddr,sizeof(reuseaddr))==-1) 
    {
        strErr_ = "setsockopt SO_REUSEADDR error ["  + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }

    memset((void *)&serverAddr_,0x00,sizeof(serverAddr_)) ;
    serverAddr_.sin_family      = AF_INET ;
    serverAddr_.sin_addr.s_addr = inet_addr(strServerIp_.c_str()) ;
    serverAddr_.sin_port = htons(nServerPort_);

    nRtn = bind(listen_socket_,(SOCKADDR*)&serverAddr_,sizeof(serverAddr_)) ;
    if ( nRtn < 0 )
    {
        strErr_ = "bind error ["  + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false ;
    }

    nRtn = listen(listen_socket_,SOMAXCONN) ;
    if ( nRtn < 0 )
    {
        strErr_ = "listrn error [" + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false ;
    }

    struct sigaction act;
    act.sa_handler = SIG_IGN;
    sigemptyset( &act.sa_mask );
    act.sa_flags = 0;
    sigaction( SIGPIPE, &act, NULL );

#if defined __APPLE__ || defined __linux__ 
    pContextListen_ = new (std::nothrow) Context();
    if(!pContextListen_)
    {
        strErr_ = "Context alloc failed !";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }

    pContextListen_->socket_ = listen_socket_;
#endif

#ifdef __APPLE__
    nKqfd_ = kqueue();
    if (nKqfd_ == -1)
    {
        strErr_ = "kqueue error ["  + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }
    if(!KqueueCtl(pContextListen_, EVFILT_READ, EV_ADD ))
    {
        return false;
    }
#elif __linux__
    nEpfd_ = epoll_create1(0);
    if (nEpfd_ == -1)
    {
        strErr_ = "epoll create error ["  + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }

    if(!EpollCtl ( pContextListen_, EPOLLIN | EPOLLERR , EPOLL_CTL_ADD )) 
    {
        return false;
    }
#endif

    //start server thread
    bServerRun_ = true;
    std::thread server_thread(&ASock::ServerThreadRoutine, this, 0);
    server_thread.detach();

    bServerRunning_ = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::StopServer()
{
    bServerRun_ = false;
}

///////////////////////////////////////////////////////////////////////////////
Context* ASock::PopClientContextFromCache()
{
    if (!clientInfoCacheQueue_.empty())
    {
        Context* pRtn = clientInfoCacheQueue_.front();
        clientInfoCacheQueue_.pop();

        return pRtn;
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::PushClientInfoToCache(Context* pClientContext)
{
    CLIENT_UNORDERMAP_ITER_T itFound;
    itFound = clientMap_.find(pClientContext->socket_);
    if (itFound != clientMap_.end())
    {
        clientMap_.erase(itFound);
    }

    //reset
    pClientContext->recvBuffer_.ReSet();
    pClientContext->socket_ = -1;
    pClientContext->bPacketLenCalculated = false;
    pClientContext->nOnePacketLength = 0;

    clientInfoCacheQueue_.push(pClientContext);
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ClearClientInfoToCache()
{
    while(!clientInfoCacheQueue_.empty() ) 
    {
        delete clientInfoCacheQueue_.front();
        clientInfoCacheQueue_.pop();
    }
}


///////////////////////////////////////////////////////////////////////////////
void ASock:: ServerThreadRoutine(int nCoreIndex)
{
#ifdef __APPLE__
    pKqEvents_ = new struct kevent[nMaxClientNum_];
    memset(pKqEvents_, 0x00, sizeof(struct kevent) * nMaxClientNum_);
    struct timespec ts;
    ts.tv_sec  =1;
    ts.tv_nsec =0;
#elif __linux__
    pEpEvents_ = new struct epoll_event[nMaxClientNum_];
    memset(pEpEvents_, 0x00, sizeof(struct epoll_event) * nMaxClientNum_);
#endif
    SOCKADDR_IN     clientAddr  ;

    char szTempData[asocklib::DEFAULT_PACKET_SIZE];

    while(bServerRun_)
    {
#ifdef __APPLE__
        int nEventCnt = kevent(nKqfd_, NULL, 0, pKqEvents_, nMaxClientNum_, &ts); 
        if (nEventCnt < 0)
        {
            strErr_ = "kevent error ["  + std::string(strerror(errno)) + "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            bServerRunning_ = false;
            return;
        }
#elif __linux__
        int nEventCnt = epoll_wait(nEpfd_, pEpEvents_, nMaxClientNum_, 1000 );
        if (nEventCnt < 0)
        {
            strErr_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            bServerRunning_ = false;
            return;
        }
#endif

        for (int i = 0; i < nEventCnt; i++)
        {
#ifdef __APPLE__
            if (pKqEvents_[i].ident   == listen_socket_) 
#elif __linux__
            //if (pEpEvents_[i].data.fd == listen_socket_)
            if (((Context*)pEpEvents_[i].data.ptr)->socket_ == listen_socket_)
#endif
            {
                //############## accept ############################
                while(1)
                {
                    SOCKLEN_T socklen=0;
                    int newClientFd = accept(listen_socket_,(SOCKADDR*)&clientAddr,&socklen ) ;

                    if (newClientFd == -1)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            //all accept done...
                            break;
                        }
                        else if (errno == ECONNABORTED)
                        {
                            break;
                        }
                        else
                        {
                            strErr_ = "accept error [" + std::string(strerror(errno)) + "]";
                            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                            bServerRunning_ = false;
                            return;
                        }
                    }
                    ++nClientCnt_;
                    SetNonBlocking(newClientFd);

                    Context* pClientContext = PopClientContextFromCache();
                    if(pClientContext==nullptr)
                    {
                        pClientContext = new (std::nothrow) Context();
                        if(!pClientContext)
                        {
                            strErr_ = "Context alloc failed !";
                            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                            bServerRunning_ = false;
                            return ;
                        }

                        if  ( cumbuffer_defines::OP_RSLT_OK != pClientContext->recvBuffer_.Init(nBufferCapcity_) )
                        {
                            strErr_  = "cumBuffer Init error : " + pClientContext->recvBuffer_.GetErrMsg();
                            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                            bServerRunning_ = false;
                            return ;
                        }
                    }
                    pClientContext->socket_ = newClientFd;

                    std::pair<CLIENT_UNORDERMAP_ITER_T, bool> clientMapRslt;
                    clientMapRslt = clientMap_.insert(std::pair<int, Context*>(newClientFd, pClientContext));
                    if (!clientMapRslt.second)
                    {
                        strErr_ = "clientMap_ insert error [" + std::to_string(newClientFd) + " already exist]";
                        std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " << GetLastErrMsg() << "\n";
                        break;
                    }

                    OnClientConnected(pClientContext);
#ifdef __APPLE__
                    if(!KqueueCtl(pClientContext, EVFILT_READ, EV_ADD ))
#elif __linux__
                    if(!EpollCtl ( pClientContext, EPOLLIN |EPOLLRDHUP  , EPOLL_CTL_ADD ))
#endif
                    {
                        bServerRunning_ = false;
                        return;
                    }

                }//while : accept
            }
            else
            {
                //############## send/recv ############################
#ifdef __APPLE__
                Context* pClientContext = (Context*)pKqEvents_[i].udata;
#elif __linux__
                Context* pClientContext = (Context*)pEpEvents_[i].data.ptr ;
#endif

#ifdef __APPLE__
                if (pKqEvents_[i].flags & EV_EOF)
#elif __linux__
                if (pEpEvents_[i].events & EPOLLRDHUP || pEpEvents_[i].events & EPOLLERR) 
#endif
                {
                    //############## close ############################
                    //std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] Terminate client"<< "\n"; 
                    TerminateClient(i, pClientContext); 
                }
#ifdef __APPLE__
                else if (EVFILT_READ == pKqEvents_[i].filter)
#elif __linux__
                else if (pEpEvents_[i].events & EPOLLIN) 
#endif
                {
                    //############## recv ############################
                    if(! Recv(pClientContext) ) 
                    {
                        //std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] Terminate client : "<< GetLastErrMsg() <<"\n"; 
                        TerminateClient(i, pClientContext); 
                    }
                }
            }
        } //for
    } //while

    bServerRunning_ = false;
}

///////////////////////////////////////////////////////////////////////////////
void  ASock::TerminateClient(int nClientIndex, Context* pClientContext)
{
    --nClientCnt_;
#ifdef __APPLE__
    KqueueCtl(pClientContext, EVFILT_READ, EV_DELETE );
#elif __linux__
    EpollCtl (pClientContext, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_DEL ); //just in case
#endif

    close(pClientContext->socket_);
    OnClientDisConnected(pClientContext);
    PushClientInfoToCache(pClientContext);
}

///////////////////////////////////////////////////////////////////////////////
int  ASock::GetCountOfClients()
{
    return nClientCnt_ ; 
}


///////////////////////////////////////////////////////////////////////////////
// CLIENT
///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitTcpClient( const char* connIP, 
                            int         nPort, 
                            int         nConnectTimeoutSecs,
                            int         nMaxMsgLen)
{
    SockUsage_ = SOCK_USAGE_TCP_CLIENT  ;

    Disconnect(); 

    if(!SetBufferCapacity(nMaxMsgLen) )
    {
        return false;
    }

    context_.socket_ = socket(AF_INET,SOCK_STREAM,0) ;

    if( context_.socket_ < 0 )
    {
        strErr_ = "init error [" + std::string(strerror(errno)) ;
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }   

    if(!SetNonBlocking (context_.socket_))
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return  false;
    }
    
    if(!bCumBufferInit_ )
    {
        if  ( cumbuffer_defines::OP_RSLT_OK != context_.recvBuffer_.Init(nBufferCapcity_) )
        {
            strErr_ = "cumBuffer Init error :" + context_.recvBuffer_.GetErrMsg();
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            return false;
        }
        bCumBufferInit_ = true;
    }
    else
    {
        //in case of reconnect
        context_.recvBuffer_.ReSet(); 
    }

    memset((void *)&connAddr_,0x00,sizeof(connAddr_)) ;
    connAddr_.sin_family      = AF_INET ;
    connAddr_.sin_addr.s_addr = inet_addr( connIP ) ;
    connAddr_.sin_port = htons( nPort );

    struct timeval timeoutVal;
    timeoutVal.tv_sec  = nConnectTimeoutSecs ;  
    timeoutVal.tv_usec = 0;

    int nRslt = connect(context_.socket_,(SOCKADDR *)&connAddr_, (SOCKLEN_T )sizeof(SOCKADDR_IN)) ;

    if ( nRslt < 0)
    {
        if (errno != EINPROGRESS)
        {
            strErr_ = "connect error [" + std::string(strerror(errno))+ "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            return false;
        }
    }

    if (nRslt == 0)
    {
        bConnected_ = true;
        return true;
    }

    fd_set   rset, wset;
    FD_ZERO(&rset);
    FD_SET(context_.socket_, &rset);
    wset = rset;

    nRslt = select(context_.socket_+1, &rset, &wset, NULL, &timeoutVal ) ;
    if (nRslt == 0 )
    {
        strErr_ = "connect timeout";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }
    else if (nRslt< 0)
    {
        strErr_ = "select :error [" + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }

    if (FD_ISSET(context_.socket_, &rset) || FD_ISSET(context_.socket_, &wset)) 
    {
        int  nSocketError = 0;
        socklen_t  len = sizeof(nSocketError);
        if (getsockopt(context_.socket_, SOL_SOCKET, SO_ERROR, &nSocketError, &len) < 0)
        {
            strErr_ = "getsockopt :SO_ERROR :error [" + std::string(strerror(errno)) + "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            return false;
        }

        if (nSocketError) 
        {
            strErr_ = "getsockopt :SO_ERROR :error [" + std::string(strerror(errno)) + "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            return false;
        }
    } 
    else
    {
        strErr_ = "select error : unexpected [" + std::string(strerror(errno))+ "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }

    bConnected_ = true;

    if(!bClientThreadRunning_ )
    {
#ifdef __APPLE__
        pKqEvents_ = new struct kevent;
        memset(pKqEvents_, 0x00, sizeof(struct kevent) );
        nKqfd_ = kqueue();
        if (nKqfd_ == -1)
        {
            strErr_ = "kqueue error ["  + std::string(strerror(errno)) + "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            return false;
        }
#elif __linux__
        pEpEvents_ = new struct epoll_event;
        memset(pEpEvents_, 0x00, sizeof(struct epoll_event) );
        nEpfd_ = epoll_create1(0);
        if ( nEpfd_== -1)
        {
            strErr_ = "epoll create error ["  + std::string(strerror(errno)) + "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            return false;
        }
#endif

        std::thread client_thread(&ASock::ClientThreadRoutine, this);
        client_thread.detach();
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ClientThreadRoutine()
{
    bClientThreadRunning_ = true;

#ifdef __APPLE__
    if(!KqueueCtl(&context_, EVFILT_READ, EV_ADD ))
    {
        return;
    }
    struct timespec ts;
    ts.tv_sec  =1;
    ts.tv_nsec =0;
#elif __linux__
    if(!EpollCtl ( &context_, EPOLLIN | EPOLLERR , EPOLL_CTL_ADD ))
    {
        return;
    }
#endif

    char szTempData[asocklib::DEFAULT_PACKET_SIZE];

    while(bConnected_)
    {
#ifdef __APPLE__
        int nEventCnt = kevent(nKqfd_, NULL, 0, pKqEvents_, 1, &ts); 
#elif __linux__
        int nEventCnt = epoll_wait(nEpfd_, pEpEvents_, 1, 1000 );
#endif
        if (nEventCnt < 0)
        {
#ifdef __APPLE__
            strErr_ = "kevent error ["  + std::string(strerror(errno)) + "]";
#elif __linux__
            strErr_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
#endif
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            bClientThreadRunning_ = false;
            return;
        }
#ifdef __APPLE__
        if (pKqEvents_->flags & EV_EOF)
#elif __linux__
        if (pEpEvents_->events & EPOLLRDHUP || pEpEvents_->events & EPOLLERR) 
#endif
        {
            //############## close ############################
            //std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] close"<< "\n"; 
            close( context_.socket_);
            OnDisConnected();
            break;
        }
#ifdef __APPLE__
        else if (EVFILT_READ == pKqEvents_->filter )
#elif __linux__
        else if (pEpEvents_->events & EPOLLIN) 
#endif
        {
            //############## recv ############################
            if(! Recv(&context_) ) 
            {
                //std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] close : "<< GetLastErrMsg() <<"\n"; 
                close( context_.socket_);
                OnDisConnected();
                break;
            }
        }

    } //while

    bClientThreadRunning_ = false;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock:: SendToServer(const char* pData, int nLen)
{
    if ( !bConnected_ )
    {
        strErr_ = "not connected";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error : "<< GetLastErrMsg() <<"\n"; 
        return false;
    }

    return Send(&context_, pData, nLen);
}

///////////////////////////////////////////////////////////////////////////////
void ASock:: Disconnect()
{
    if(context_.socket_ > 0 )
    {
        close(context_.socket_);
    }
    context_.socket_ = -1;
}

