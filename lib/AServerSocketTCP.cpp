
#include "AServerSocketTCP.hpp"
#include <thread>   
#include <string> 
#include <algorithm>

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
    if (pEpEvents_)   
    { 
        delete [] pEpEvents_;    
        pEpEvents_   = nullptr; 
    }

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
    nCores_ = sysconf(_SC_NPROCESSORS_ONLN) ; //TODO

    if (pEpEvents_)   
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
        strErr_ = "init error [" + string(strerror(errno)) + "]";
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
        strErr_ = "setsockopt SO_REUSEADDR error ["  + string(strerror(errno)) + "]";
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
        strErr_ = "bind error ["  + string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false ;
    }

    nRtn = listen(listen_socket_,SOMAXCONN) ;
    if ( nRtn < 0 )
    {
        strErr_ = "listrn error [" + string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false ;
    }

    nEpfd_ = epoll_create1(0);
    
    struct sigaction act;
    act.sa_handler = SIG_IGN;
    sigemptyset( &act.sa_mask );
    act.sa_flags = 0;
    sigaction( SIGPIPE, &act, NULL );

    struct  epoll_event ev;
    memset(&ev, 0x00, sizeof(ev));
    ev.events = EPOLLIN | EPOLLERR;
    ev.data.fd = listen_socket_;
    epoll_ctl(nEpfd_, EPOLL_CTL_ADD, listen_socket_, &ev);

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
        Context* pRtn = clientInfoCacheQueue_.front();
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
    pEpEvents_ = new struct epoll_event[nMaxClientNum_];
    memset(pEpEvents_, 0x00, sizeof(struct epoll_event) * nMaxClientNum_);

    char szTempData[asocklib::DEFAULT_PACKET_SIZE];

    while(bServerRun_)
    {
        int n = epoll_wait(nEpfd_, pEpEvents_, nMaxClientNum_, 1000 );
        if (n < 0 )
        {
            strErr_ = "epoll wait error [" + string(strerror(errno)) + "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            bServerRunning_ = false;
            return;
        }

        for (int i = 0; i < n; i++)
        {
            if (pEpEvents_[i].data.fd == listen_socket_)
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
                        else
                        {
                            strErr_ = "accept error [" + string(strerror(errno)) + "]";
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
                        strErr_ = "clientMap_ insert error [" + to_string(newClientFd)+ " already exist]";
                        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                        bServerRunning_ = false;
                        return;
                    }

                    OnClientConnected(pClientContext);

                    struct  epoll_event evClient{};
                    evClient.data.fd = newClientFd;
                    evClient.events = EPOLLIN | EPOLLRDHUP   ; 
                    epoll_ctl(nEpfd_, EPOLL_CTL_ADD, newClientFd, &evClient);

                }//while : accept
            }
            else
            {
                //############## send/recv ############################
                CLIENT_UNORDERMAP_ITER_T itFound;
                itFound = clientMap_.find(pEpEvents_[i].data.fd);
                if (itFound == clientMap_.end())
                {
                    strErr_ = "clientMap_ error [" + to_string(pEpEvents_[i].data.fd)+ " not found]";
                    std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                    bServerRunning_ = false;
                    return;
                }
                
                //now, found client
                Context* pClientContext = itFound->second;


                if (pEpEvents_[i].events & EPOLLIN) 
                {
                    //############## recv ############################
                    if(! Recv(pClientContext) ) 
                    {
                        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] Terminate client : "<< GetLastErrMsg() <<"\n"; 
                        TerminateClient(i, pClientContext); 
                    }
                }
                else if (pEpEvents_[i].events & EPOLLOUT) 
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
                                    EpollCtlModify(pClientContext, EPOLLIN | EPOLLERR | EPOLLRDHUP );
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
                                    strErr_ = "send error ["  + string(strerror(errno)) + "]";
                                    std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] "<< GetLastErrMsg() <<"\n"; 
                                    TerminateClient(i, pClientContext); 
                                    break;
                                }
                            }
                        }
                        else
                        {
                            EpollCtlModify(pClientContext, EPOLLIN | EPOLLERR | EPOLLRDHUP ); //just in case
                        }
                    }//while : EPOLLOUT
                }
                else if (pEpEvents_[i].events & EPOLLRDHUP || pEpEvents_[i].events & EPOLLERR) 
                {
                    //############## close ############################
                    std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] Terminate client"<< "\n"; 
                    TerminateClient(i, pClientContext); 
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
    close(pEpEvents_[nClientIndex].data.fd);
    epoll_ctl(nEpfd_, EPOLL_CTL_DEL, pEpEvents_[nClientIndex].data.fd, pEpEvents_);
    OnClientDisConnected(pClientContext);
    PushClientInfoToCache(pClientContext);
}

///////////////////////////////////////////////////////////////////////////////
int  AServerSocketTCP::GetCountOfClients()
{
    return nClientCnt_ ; 
}

