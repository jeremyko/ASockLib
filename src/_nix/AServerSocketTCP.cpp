
#include "../../include/AServerSocketTCP.hpp"
#include <thread>   
#include <string> 
#include <algorithm>

using namespace asocklib ;

///////////////////////////////////////////////////////////////////////////////
AServerSocketTCP::AServerSocketTCP (const char* connIP, 
                                    int         nPort, 
                                    int         nMaxClient, 
                                    int         nMaxMsgLen/*=0*/)
    : ASockBase(nMaxMsgLen), 
      strServerIp_(connIP), 
      nMaxClientNum_(nMaxClient), 
      nServerPort_(nPort) 
{
}

///////////////////////////////////////////////////////////////////////////////
AServerSocketTCP::~AServerSocketTCP()
{
#ifdef __APPLE__
    if ( pKqEvents_ )
    { 
        delete [] pKqEvents_;    
        pKqEvents_   = nullptr; 
    }
#elif __linux__
    if (pEpEvents_)   
    { 
        delete [] pEpEvents_;    
        pEpEvents_   = nullptr; 
    }
#endif

    ClearClientInfoToCache();
   
    CLIENT_UNORDERMAP_ITER_T itDel = clientMap_.begin();
    while (itDel != clientMap_.end()) 
    {
        delete itDel->second;
        itDel = clientMap_.erase(itDel);
    }
}

///////////////////////////////////////////////////////////////////////////////
bool AServerSocketTCP::SetConnInfo (const char* connIP, int nPort, int nMaxClient, int nMaxMsgLen)
{
    strServerIp_    =   connIP; 
    nServerPort_    =   nPort; 

    nMaxClientNum_  =   nMaxClient; 
    if(nMaxClientNum_<0)
    {
        return false;
    }

    return SetBufferCapacity(nMaxMsgLen);
}

///////////////////////////////////////////////////////////////////////////////
bool AServerSocketTCP::RunServer()
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

    if(nBufferCapcity_ <0)
    {
        strErr_ = "init error [packet length is negative]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return  false;
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

#ifdef __APPLE__
    nKqfd_ = kqueue();
    if (nKqfd_ == -1)
    {
        strErr_ = "kqueue error ["  + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }

    if(!KqueueCtl(listen_socket_, EVFILT_READ, EV_ADD ))
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

    if(!EpollCtl ( listen_socket_, EPOLLIN | EPOLLERR , EPOLL_CTL_ADD ))
    {
        return false;
    }
#endif

    //start server thread
    bServerRun_ = true;
    std::thread server_thread(&AServerSocketTCP::ServerThreadRoutine, this, 0);
    server_thread.detach();

    bServerRunning_ = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void AServerSocketTCP::StopServer()
{
    bServerRun_ = false;
}

///////////////////////////////////////////////////////////////////////////////
Context* AServerSocketTCP::PopClientContextFromCache()
{
    if (!clientInfoCacheQueue_.empty())
    {
        asocklib::Context* pRtn = clientInfoCacheQueue_.front();
        clientInfoCacheQueue_.pop();

        return pRtn;
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
void AServerSocketTCP::PushClientInfoToCache(Context* pClientContext)
{
    CLIENT_UNORDERMAP_ITER_T itFound;
    itFound = clientMap_.find(pClientContext->socket_);
    if (itFound != clientMap_.end())
    {
        clientMap_.erase(itFound);
    }

    clientInfoCacheQueue_.push(pClientContext);
}

///////////////////////////////////////////////////////////////////////////////
void AServerSocketTCP::ClearClientInfoToCache()
{
    while(!clientInfoCacheQueue_.empty() ) 
    {
        delete clientInfoCacheQueue_.front();
        clientInfoCacheQueue_.pop();
    }
}


///////////////////////////////////////////////////////////////////////////////
void AServerSocketTCP:: ServerThreadRoutine(int nCoreIndex)
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
            if (pEpEvents_[i].data.fd == listen_socket_)
#endif
            {
                //############## accept ############################
                while(1)
                {
                    SOCKLEN_T socklen=0;
                    int newClientFd = accept(listen_socket_,(SOCKADDR*)&serverAddr_,&socklen ) ;

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

                    //---- add client map
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

                        pClientContext->socket_ = newClientFd;
                        if  (
                                cumbuffer_defines::OP_RSLT_OK != pClientContext->recvBuffer_.Init(nBufferCapcity_) ||
                                cumbuffer_defines::OP_RSLT_OK != pClientContext->sendBuffer_.Init(nBufferCapcity_)
                            )
                        {
                            strErr_  = "cumBuffer Init error : " + pClientContext->recvBuffer_.GetErrMsg();
                            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                            bServerRunning_ = false;
                            return ;
                        }
                    }

                    std::pair<CLIENT_UNORDERMAP_ITER_T,bool> clientMapRslt;
                    clientMapRslt = clientMap_.insert ( std::pair<int, Context*>(newClientFd, pClientContext) );
                    if (!clientMapRslt.second) 
                    {
                        strErr_ = "clientMap_ insert error [" + std::to_string(newClientFd)+ " already exist]";
                        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                        bServerRunning_ = false;
                        return;
                    }

                    OnClientConnected(pClientContext);
#ifdef __APPLE__
                    if(!KqueueCtl(newClientFd, EVFILT_READ, EV_ADD ))
#elif __linux__
                    if(!EpollCtl ( newClientFd, EPOLLIN |EPOLLRDHUP  , EPOLL_CTL_ADD ))
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
                CLIENT_UNORDERMAP_ITER_T itFound;
#ifdef __APPLE__
                itFound = clientMap_.find(pKqEvents_[i].ident );
#elif __linux__
                itFound = clientMap_.find(pEpEvents_[i].data.fd);
#endif
                if (itFound == clientMap_.end())
                {
                    strErr_ = "clientMap_ error [" + std::to_string(pEpEvents_[i].data.fd)+ " not found]";
                    std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                    bServerRunning_ = false;
                    return;
                }
                
                //now, found client
                Context* pClientContext = itFound->second;
#ifdef __APPLE__
                if (pKqEvents_[i].flags & EV_EOF)
#elif __linux__
                if (pEpEvents_[i].events & EPOLLRDHUP || pEpEvents_[i].events & EPOLLERR) 
#endif
                {
                    //############## close ############################
                    std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] Terminate client"<< "\n"; 
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
                        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] Terminate client : "<< GetLastErrMsg() <<"\n"; 
                        TerminateClient(i, pClientContext); 
                    }
                }
#ifdef __APPLE__
                else if (EVFILT_WRITE == pKqEvents_[i].filter )
#elif __linux__
                else if (pEpEvents_[i].events & EPOLLOUT) 
#endif
                {
                    //############## send ############################
                    std::lock_guard<std::mutex> guard(pClientContext->clientSendLock_);
                    while(true) 
                    {
                        size_t nBufferdLen = pClientContext->sendBuffer_.GetCumulatedLen() ;
                        if( nBufferdLen > 0 )
                        {
                            //need to send
                            size_t nPeekLen= std::min(nBufferdLen, sizeof(szTempData));

                            if(cumbuffer_defines::OP_RSLT_OK!=pClientContext->sendBuffer_.PeekData( nPeekLen, szTempData))
                            {
                                strErr_  = "cumBuffer error : " + pClientContext->sendBuffer_.GetErrMsg();
                                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                                TerminateClient(i, pClientContext); //error ... disconnect client
                                break;
                            }

                            int nSent = send(pClientContext->socket_, szTempData, nPeekLen, 0) ;

                            if( nSent > 0 )
                            {
                                pClientContext->sendBuffer_.ConsumeData(nSent);
                                if(pClientContext->sendBuffer_.GetCumulatedLen()==0)
                                {
                                    //sent all data
#ifdef __APPLE__
                                    if(!KqueueCtl(pClientContext->socket_, EVFILT_WRITE, EV_DELETE ) ||
                                       !KqueueCtl(pClientContext->socket_, EVFILT_READ, EV_ADD ) )
#elif __linux__
                                    if(!EpollCtl (pClientContext->socket_, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_MOD ))
#endif
                                    {
                                        bServerRunning_ = false;
                                        return;
                                    }
                                    break;
                                }
                            }
                            else if( nSent < 0 )
                            {
                                if ( errno == EWOULDBLOCK || errno == EAGAIN )
                                {
                                    break; //next time
                                }
                                else if ( errno != EINTR )
                                {
                                    strErr_ = "send error ["  + std::string(strerror(errno)) + "]";
                                    std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< GetLastErrMsg() <<"\n"; 
                                    TerminateClient(i, pClientContext); 
                                    break;
                                }
                            }
                        } //if( nBufferdLen > 0 )
                        else
                        {
                            //something wrong...: no data but write event? -> should not happen
                            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] write event, no data\n"; 
                            break;
                        }
                    }//while : EPOLLOUT
                }
            }
        } //for
    } //while

    bServerRunning_ = false;
}

///////////////////////////////////////////////////////////////////////////////
void  AServerSocketTCP::TerminateClient(int nClientIndex, Context* pClientContext)
{
    --nClientCnt_;
#ifdef __APPLE__
    KqueueCtl(pClientContext->socket_, EVFILT_READ, EV_DELETE );
#elif __linux__
    EpollCtl (pClientContext->socket_, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_DEL ); //just in case
#endif

    close(pClientContext->socket_);
    OnClientDisConnected(pClientContext);
    PushClientInfoToCache(pClientContext);
}

///////////////////////////////////////////////////////////////////////////////
int  AServerSocketTCP::GetCountOfClients()
{
    return nClientCnt_ ; 
}

