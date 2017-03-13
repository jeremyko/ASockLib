
#include <thread>

#include "../../include/AClientSocketTCP.hpp"

using namespace asocklib ;

///////////////////////////////////////////////////////////////////////////////
AClientSocketTCP::AClientSocketTCP() 
    : bCumBufferInit_(false),bConnected_(false)
{
}

///////////////////////////////////////////////////////////////////////////////
AClientSocketTCP::~AClientSocketTCP()
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


///////////////////////////////////////////////////////////////////////////////
bool AClientSocketTCP::Connect(const char* connIP, int nPort, int nConnectTimeoutSecs)
{
    Disconnect(); 

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
        if  ( 
                cumbuffer_defines::OP_RSLT_OK != context_.recvBuffer_.Init(nBufferCapcity_) ||
                cumbuffer_defines::OP_RSLT_OK != context_.sendBuffer_.Init(nBufferCapcity_)
            )
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
        context_.sendBuffer_.ReSet(); 
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

        std::thread client_thread(&AClientSocketTCP::ClientThreadRoutine, this);
        client_thread.detach();
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void AClientSocketTCP::ClientThreadRoutine()
{
    bClientThreadRunning_ = true;

#ifdef __APPLE__
    if(!KqueueCtl(context_.socket_, EVFILT_READ, EV_ADD ))
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
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] close"<< "\n"; 
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
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] close : "<< GetLastErrMsg() <<"\n"; 
                close( context_.socket_);
                OnDisConnected();
                break;
            }
        }
#ifdef __APPLE__
        else if ( EVFILT_WRITE == pKqEvents_->filter )
#elif __linux__
        else if (pEpEvents_->events & EPOLLOUT) 
#endif
        {
            //############## send ############################
            std::lock_guard<std::mutex> guard( context_.clientSendLock_);
            while(true) 
            {
                size_t nBufferdLen = context_.sendBuffer_.GetCumulatedLen() ;
                if( nBufferdLen > 0 )
                {
                    //need to send
                    size_t nPeekLen= std::min(nBufferdLen, sizeof(szTempData));

                    if(cumbuffer_defines::OP_RSLT_OK!=context_.sendBuffer_.PeekData( nPeekLen, szTempData))
                    {
                        strErr_  = "cumBuffer error : " + context_.sendBuffer_.GetErrMsg();
                        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
                        close( context_.socket_);
                        OnDisConnected();
                        bClientThreadRunning_ = false;
                        return;
                    }

                    int nSent = send(context_.socket_, szTempData, nPeekLen, 0) ;

                    if( nSent > 0 )
                    {
                        context_.sendBuffer_.ConsumeData(nSent);
                        if(context_.sendBuffer_.GetCumulatedLen()==0)
                        {
                            //sent all data
#ifdef __APPLE__
                            if(!KqueueCtl(&context_, EVFILT_WRITE, EV_DELETE ) ||
                               !KqueueCtl(&context_, EVFILT_READ, EV_ADD ) )
#elif __linux__
                            if(!EpollCtl(&context_, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_MOD ))
#endif
                            {
                                close( context_.socket_);
                                OnDisConnected();
                                bClientThreadRunning_ = false;
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
                            close( context_.socket_);
                            OnDisConnected();
                            bClientThreadRunning_ = false;
                            return;
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

    } //while

    bClientThreadRunning_ = false;
}

///////////////////////////////////////////////////////////////////////////////
bool AClientSocketTCP:: SendToServer(const char* pData, int nLen)
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
void AClientSocketTCP:: Disconnect()
{
    if(context_.socket_ > 0 )
    {
        close(context_.socket_);
    }
    context_.socket_ = -1;
}

