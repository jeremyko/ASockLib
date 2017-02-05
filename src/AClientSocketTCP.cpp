
#include <thread>

#include "AClientSocketTCP.hpp"

///////////////////////////////////////////////////////////////////////////////
AClientSocketTCP::AClientSocketTCP() 
    : bCumBufferInit_(false),bConnected_(false)
{
}

///////////////////////////////////////////////////////////////////////////////
AClientSocketTCP::~AClientSocketTCP()
{
    if(pEpEvents_)
    {
        delete [] pEpEvents_;
    }
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
        pEpEvents_ = new struct epoll_event[1];
        memset(pEpEvents_, 0x00, sizeof(struct epoll_event) * 1);
        nEpfd_ = epoll_create1(0);

        std::thread client_thread(&AClientSocketTCP::ClientThreadRoutine, this);
        client_thread.detach();
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void AClientSocketTCP::ClientThreadRoutine()
{
    bClientThreadRunning_ = true;

    struct  epoll_event ev;
    memset(&ev, 0x00, sizeof(ev));
    ev.events = EPOLLIN | EPOLLERR;
    ev.data.fd = context_.socket_ ;
    epoll_ctl(nEpfd_, EPOLL_CTL_ADD, context_.socket_, &ev);

    char szTempData[asocklib::DEFAULT_PACKET_SIZE];

    while(bConnected_)
    {
        int n = epoll_wait(nEpfd_, pEpEvents_, 1, 1000 );
        if (n < 0 )
        {
            strErr_ = "epoll wait error [" + string(strerror(errno)) + "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            bClientThreadRunning_ = false;
            return;
        }

        if (pEpEvents_[0].events & EPOLLIN) 
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
        else if (pEpEvents_[0].events & EPOLLOUT) 
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
                            EpollCtlModify(&context_, EPOLLIN | EPOLLERR | EPOLLRDHUP );
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
                            close( context_.socket_);
                            OnDisConnected();
                            bClientThreadRunning_ = false;
                            return;
                        }
                    }
                }
                else
                {
                    EpollCtlModify(&context_, EPOLLIN | EPOLLERR | EPOLLRDHUP ); //just in case
                }
            }//while : EPOLLOUT
        }
        else if (pEpEvents_[0].events & EPOLLRDHUP || pEpEvents_[0].events & EPOLLERR) 
        {
            //############## close ############################
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] close"<< "\n"; 
            close( context_.socket_);
            OnDisConnected();
            break;
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

