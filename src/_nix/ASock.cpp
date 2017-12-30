/******************************************************************************
MIT License

Copyright (c) 2017 jung hyun, ko

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 *****************************************************************************/

#include "ASock.hpp"

using namespace asock ;

///////////////////////////////////////////////////////////////////////////////
ASock::~ASock()
{
    if(complete_packet_data_!=NULL)
    {
        delete [] complete_packet_data_ ;
        complete_packet_data_ = NULL;
    }

    if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT || sock_usage_ == SOCK_USAGE_IPC_CLIENT ) 
    {
#ifdef __APPLE__
        if(kq_events_ptr_)
        {
            delete kq_events_ptr_;
        }
#elif __linux__
        if(ep_events_)
        {
            delete ep_events_;
        }
#endif

        disconnect();
    }
    else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER || 
              sock_usage_ == SOCK_USAGE_IPC_SERVER  ) 
    {
#ifdef __APPLE__
        if ( kq_events_ptr_ )
        { 
            delete [] kq_events_ptr_;    
        }
#elif __linux__
        if (ep_events_)   
        { 
            delete [] ep_events_;    
        }
#endif

        CLIENT_UNORDERMAP_ITER_T itDel = client_map_.begin();
        while (itDel != client_map_.end()) 
        {
            delete itDel->second;
            itDel = client_map_.erase(itDel);
        }

        clear_client_cache();

#ifdef __APPLE__
        control_kq(listen_context_ptr_, EVFILT_READ, EV_DELETE );
#elif __linux__
        control_ep(listen_context_ptr_, EPOLLIN | EPOLLERR | EPOLLRDHUP, 
                                        EPOLL_CTL_DEL ); //just in case
#endif

#if defined __APPLE__ || defined __linux__ 
        delete listen_context_ptr_ ;
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::set_cb_on_calculate_packet_len(std::function<size_t(Context*)> cb)  
{
    //for composition usage 
    if(cb != nullptr)
    {
        cb_on_calculate_data_len_ = cb;
    }
    else
    {
        err_msg_ = "callback is null";
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::set_cb_on_recved_complete_packet(std::function<bool(Context*, char*, int)> cb) 
{
    //for composition usage 
    if(cb != nullptr)
    {
        cb_on_recved_complete_packet_ = cb;
    }
    else
    {
        err_msg_ = "callback is null";
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool  ASock::set_cb_on_disconnected_from_server(std::function<void()> cb)  
{
    //for composition usage 
    if(cb != nullptr)
    {
        cb_on_disconnected_from_server_ = cb;
    }
    else
    {
        err_msg_ = "callback is null";
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::set_cb_on_client_connected(std::function<void(Context*)> cb)  
{
    //for composition usage 
    if(cb != nullptr)
    {
         cb_on_client_connected_= cb;
    }
    else
    {
        err_msg_ = "callback is null";
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::set_cb_on_client_disconnected(std::function<void(Context*)> cb)  
{
    //for composition usage 
    if(cb != nullptr)
    {
        cb_on_client_disconnected_ = cb;
    }
    else
    {
        err_msg_ = "callback is null";
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::set_buffer_capacity(int max_data_len)
{
    if(max_data_len<0)
    {
        err_msg_ = "length is negative";
        return false;
    }

    if(max_data_len==0) 
    {
        recv_buffer_capcity_ = asock::DEFAULT_CAPACITY ;
    }
    else
    {
        recv_buffer_capcity_ = max_data_len; 
    }

    complete_packet_data_ = new (std::nothrow) char [recv_buffer_capcity_] ;
    if(complete_packet_data_ == NULL)
    {
        err_msg_ = "memory alloc failed!";
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool   ASock::set_socket_non_blocking(int sock_fd)
{
    int oldflags  ;

    if ((oldflags = fcntl( sock_fd,F_GETFL, 0)) < 0 )
    {
        err_msg_ = "fcntl F_GETFL error [" + std::string(strerror(errno))+ "]";
        return  false;
    }

    int ret  = fcntl( sock_fd,F_SETFL,oldflags | O_NONBLOCK) ;
    if ( ret < 0 )
    {
        err_msg_ = "fcntl O_NONBLOCK error [" + std::string(strerror(errno))+ "]";
        return  false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::recv_data(Context* context_ptr) 
{
    int nToRecvLen = asock::DEFAULT_PACKET_SIZE ;
    if(asock::DEFAULT_PACKET_SIZE > context_ptr->recvBuffer_.GetLinearFreeSpace() )
    {
        nToRecvLen = context_ptr->recvBuffer_.GetLinearFreeSpace() ; 
    }

    if(nToRecvLen==0) 
    {
        err_msg_ = "no linear free space left ";
        return false; 
    }

    int nRecvedLen = recv( context_ptr->socket_, 
                           context_ptr->recvBuffer_.GetLinearAppendPtr(), 
                           nToRecvLen, 0); 

    if( nRecvedLen > 0)
    {
        context_ptr->recvBuffer_.IncreaseData(nRecvedLen);

        while(context_ptr->recvBuffer_.GetCumulatedLen())
        {
            //invoke user specific implementation
            if(!context_ptr->is_packet_len_calculated_ )
            {
                //only when calculation is necessary
                if(cb_on_calculate_data_len_!=nullptr)
                {
                    //invoke user specific callback
                    context_ptr->complete_packet_len_ =cb_on_calculate_data_len_ ( context_ptr ); 
                }
                else
                {
                    //invoke user specific implementation
                    context_ptr->complete_packet_len_ = on_calculate_data_len( context_ptr ); 
                }
                context_ptr->is_packet_len_calculated_ = true;
            }

            if(context_ptr->complete_packet_len_ == asock::MORE_TO_COME)
            {
                context_ptr->is_packet_len_calculated_ = false;
                return true; //need to recv more
            }
            else if(context_ptr->complete_packet_len_ > context_ptr->recvBuffer_.GetCumulatedLen())
            {
                return true; //need to recv more
            }
            else
            {
                //got complete packet 
                if(cumbuffer_defines::OP_RSLT_OK!=
                        context_ptr->recvBuffer_.GetData(context_ptr->complete_packet_len_, 
                                                      complete_packet_data_ ))
                {
                    //error !
                    err_msg_ = context_ptr->recvBuffer_.GetErrMsg();
                    context_ptr->is_packet_len_calculated_ = false;
                    return false; 
                }
                
                if(cb_on_recved_complete_packet_!=nullptr)
                {
                    //invoke user specific callback
                    cb_on_recved_complete_packet_ (context_ptr, 
                                                   complete_packet_data_ , 
                                                   context_ptr->complete_packet_len_ ); 
                }
                else
                {
                    //invoke user specific implementation
                    on_recved_complete_data(context_ptr, 
                                            complete_packet_data_ , 
                                            context_ptr->complete_packet_len_ ); 
                }
                
                context_ptr->is_packet_len_calculated_ = false;
            }
        } //while
    }   
    else if( nRecvedLen == 0 )
    {
        err_msg_ = "recv 0, client disconnected , fd:" + std::to_string(context_ptr->socket_);
        return false ;
    }

    return true ;
}


///////////////////////////////////////////////////////////////////////////////
bool ASock::send_data (Context* context_ptr, const char* data_ptr, int len) 
{
    std::lock_guard<std::mutex> lock(context_ptr->send_lock_);

    char* data_position_ptr = const_cast<char*>(data_ptr) ;   
    int sent_bytes = 0;           

    while( sent_bytes < len ) 
    {
        int nSent = send(context_ptr->socket_, data_position_ptr, len-sent_bytes, 0);
        if(nSent > 0)
        {
            sent_bytes += nSent ;  
            data_position_ptr += nSent ;      
        }
        else if( nSent < 0 )
        {
            if ( errno == EWOULDBLOCK || errno == EAGAIN )
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
                //XXX this can cause infinite loop! XXX
                continue;

            }
            else if ( errno != EINTR )
            {
                err_msg_ = "send error [" + std::string(strerror(errno)) + "]";
                return false;
            }
        }
    }

    return true;
}

#ifdef __APPLE__
///////////////////////////////////////////////////////////////////////////////
bool ASock::control_kq(Context* context_ptr , uint32_t events, uint32_t fflags)
{
    struct  kevent kEvent;
    memset(&kEvent, 0, sizeof(struct kevent));
    EV_SET(&kEvent, context_ptr->socket_, events,fflags , 0, 0, context_ptr); //udata = context_ptr

    int nRslt = kevent(kq_fd_, &kEvent, 1, NULL, 0, NULL);
    if (nRslt == -1)
    {
        err_msg_ = "kevent error [" + std::string(strerror(errno)) + "]";
        return false; 
    }
    return true;
    //man:Re-adding an existing event will modify the parameters of the
    //    original event, and not result in a duplicate entry.
}
#elif __linux__
///////////////////////////////////////////////////////////////////////////////
bool ASock::control_ep(Context* context_ptr , uint32_t events, int op)
{
    struct  epoll_event evClient{};
    evClient.data.fd    = context_ptr->socket_;
    evClient.events     = events ;
    evClient.data.ptr   = context_ptr;

    if(epoll_ctl(ep_fd_, op, context_ptr->socket_, &evClient)<0)
    {
        err_msg_ = "kevent error [" + std::string(strerror(errno)) + "]";
        return false; 
    }
    return true;
}
#endif


///////////////////////////////////////////////////////////////////////////////
// SERVER
///////////////////////////////////////////////////////////////////////////////
bool ASock::init_tcp_server(const char* bind_ip, 
                            int         bind_port, 
                            int         max_client, 
                            int         max_data_len)
{
    sock_usage_ = SOCK_USAGE_TCP_SERVER  ;

    server_ip_ = bind_ip ; 
    server_port_ = bind_port ; 
    max_client_limit_ = max_client ; 
    if(max_client_limit_<0)
    {
        return false;
    }

    return set_buffer_capacity(max_data_len);
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::run_server()
{
#ifdef __APPLE__
    if ( kq_events_ptr_ )
#elif __linux__
    if (ep_events_)   
#endif
    { 
        err_msg_ = "error [server is already running]";
        return false;
    }

    listen_socket_ = socket(AF_INET,SOCK_STREAM,0) ;
    if( listen_socket_ < 0 )
    {
        err_msg_ = "init error [" + std::string(strerror(errno)) + "]";
        return false;
    }   

    if(!set_socket_non_blocking (listen_socket_))
    {
        return  false;
    }

    int opt_on=1;
    int nRtn = -1;

    if (setsockopt(listen_socket_,SOL_SOCKET,SO_REUSEADDR,&opt_on,sizeof(opt_on))==-1) 
    {
        err_msg_ = "setsockopt SO_REUSEADDR error ["  + std::string(strerror(errno)) + "]";
        return false;
    }

    if (setsockopt(listen_socket_,SOL_SOCKET,SO_KEEPALIVE, &opt_on, sizeof(opt_on))==-1) 
    {
        err_msg_ = "setsockopt SO_KEEPALIVE error ["  + std::string(strerror(errno)) + "]";
        return false;
    }

    SOCKADDR_IN    server_addr  ;
    memset((void *)&server_addr,0x00,sizeof(server_addr)) ;
    server_addr.sin_family      = AF_INET ;
    server_addr.sin_addr.s_addr = inet_addr(server_ip_.c_str()) ;
    server_addr.sin_port = htons(server_port_);

    nRtn = bind(listen_socket_,(SOCKADDR*)&server_addr,sizeof(server_addr)) ;
    if ( nRtn < 0 )
    {
        err_msg_ = "bind error ["  + std::string(strerror(errno)) + "]";
        return false ;
    }

    nRtn = listen(listen_socket_,SOMAXCONN) ;
    if ( nRtn < 0 )
    {
        err_msg_ = "listrn error [" + std::string(strerror(errno)) + "]";
        return false ;
    }

    struct sigaction act;
    act.sa_handler = SIG_IGN;
    sigemptyset( &act.sa_mask );
    act.sa_flags = 0;
    sigaction( SIGPIPE, &act, NULL );

#if defined __APPLE__ || defined __linux__ 
    listen_context_ptr_ = new (std::nothrow) Context();
    if(!listen_context_ptr_)
    {
        err_msg_ = "Context alloc failed !";
        return false;
    }

    listen_context_ptr_->socket_ = listen_socket_;
#endif

#ifdef __APPLE__
    kq_fd_ = kqueue();
    if (kq_fd_ == -1)
    {
        err_msg_ = "kqueue error ["  + std::string(strerror(errno)) + "]";
        return false;
    }
    if(!control_kq(listen_context_ptr_, EVFILT_READ, EV_ADD ))
    {
        return false;
    }
#elif __linux__
    ep_fd_ = epoll_create1(0);
    if (ep_fd_ == -1)
    {
        err_msg_ = "epoll create error ["  + std::string(strerror(errno)) + "]";
        return false;
    }

    if(!control_ep ( listen_context_ptr_, EPOLLIN | EPOLLERR , EPOLL_CTL_ADD )) 
    {
        return false;
    }
#endif

    //start server thread
    is_need_server_run_ = true;
    is_server_running_  = true;
    std::thread server_thread(&ASock::server_thread_routine, this);
    server_thread.detach();

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::stop_server()
{
    is_need_server_run_ = false;
}

///////////////////////////////////////////////////////////////////////////////
Context* ASock::pop_client_context_from_cache()
{
    if (!queue_client_cache_.empty())
    {
        Context* pRtn = queue_client_cache_.front();
        queue_client_cache_.pop();

        return pRtn;
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::push_client_context_to_cache(Context* context_ptr)
{
    CLIENT_UNORDERMAP_ITER_T itFound;
    itFound = client_map_.find(context_ptr->socket_);
    if (itFound != client_map_.end())
    {
        client_map_.erase(itFound);
    }

    //reset
    context_ptr->recvBuffer_.ReSet();
    context_ptr->socket_ = -1;
    context_ptr->is_packet_len_calculated_ = false;
    context_ptr->complete_packet_len_ = 0;

    queue_client_cache_.push(context_ptr);
}

///////////////////////////////////////////////////////////////////////////////
void ASock::clear_client_cache()
{
    while(!queue_client_cache_.empty() ) 
    {
        delete queue_client_cache_.front();
        queue_client_cache_.pop();
    }
}


///////////////////////////////////////////////////////////////////////////////
void ASock:: server_thread_routine()
{
#ifdef __APPLE__
    kq_events_ptr_ = new struct kevent[max_client_limit_];
    memset(kq_events_ptr_, 0x00, sizeof(struct kevent) * max_client_limit_);
    struct timespec ts;
    ts.tv_sec  =1;
    ts.tv_nsec =0;
#elif __linux__
    ep_events_ = new struct epoll_event[max_client_limit_];
    memset(ep_events_, 0x00, sizeof(struct epoll_event) * max_client_limit_);
#endif
    SOCKADDR_IN     clientAddr  ;

    char szTempData[asock::DEFAULT_PACKET_SIZE];

    while(is_need_server_run_)
    {
#ifdef __APPLE__
        int nEventCnt = kevent(kq_fd_, NULL, 0, kq_events_ptr_, max_client_limit_, &ts); 
        if (nEventCnt < 0)
        {
            err_msg_ = "kevent error ["  + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#elif __linux__
        int nEventCnt = epoll_wait(ep_fd_, ep_events_, max_client_limit_, 1000 );
        if (nEventCnt < 0)
        {
            err_msg_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#endif

        for (int i = 0; i < nEventCnt; i++)
        {
#ifdef __APPLE__
            if (kq_events_ptr_[i].ident   == listen_socket_) 
#elif __linux__
            //if (ep_events_[i].data.fd == listen_socket_)
            if (((Context*)ep_events_[i].data.ptr)->socket_ == listen_socket_)
#endif
            {
                //# accept #----------
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
                            err_msg_ = "accept error [" + std::string(strerror(errno)) + "]";
                            is_server_running_ = false;
                            return;
                        }
                    }
                    ++client_cnt_;
                    set_socket_non_blocking(newClientFd);

                    Context* client_context_ptr = pop_client_context_from_cache();
                    if(client_context_ptr==nullptr)
                    {
                        client_context_ptr = new (std::nothrow) Context();
                        if(!client_context_ptr)
                        {
                            err_msg_ = "Context alloc failed !";
                            is_server_running_ = false;
                            return ;
                        }

                        if  ( cumbuffer_defines::OP_RSLT_OK != 
                                  client_context_ptr->recvBuffer_.Init(recv_buffer_capcity_) )
                        {
                            err_msg_  = "cumBuffer Init error : " + 
                                        client_context_ptr->recvBuffer_.GetErrMsg();
                            is_server_running_ = false;
                            return ;
                        }
                    }
                    client_context_ptr->socket_ = newClientFd;

                    std::pair<CLIENT_UNORDERMAP_ITER_T, bool> clientMapRslt;
                    clientMapRslt = client_map_.insert(std::pair<int, Context*>(newClientFd, client_context_ptr));
                    if (!clientMapRslt.second)
                    {
                        err_msg_ = "client_map_ insert error [" + 
                                   std::to_string(newClientFd) + " already exist]";
                        break;
                    }

                    if(cb_on_client_connected_!=nullptr)
                    {
                        cb_on_client_connected_(client_context_ptr);
                    }
                    else
                    {
                        on_client_connected(client_context_ptr);
                    }
#ifdef __APPLE__
                    if(!control_kq(client_context_ptr, EVFILT_READ, EV_ADD ))
#elif __linux__
                    if(!control_ep( client_context_ptr, EPOLLIN |EPOLLRDHUP  , EPOLL_CTL_ADD ))
#endif
                    {
                        is_server_running_ = false;
                        return;
                    }

                }//while : accept
            }
            else
            {
                //# send/recv #----------
#ifdef __APPLE__
                Context* client_context_ptr = (Context*)kq_events_ptr_[i].udata;
#elif __linux__
                Context* client_context_ptr = (Context*)ep_events_[i].data.ptr ;
#endif

#ifdef __APPLE__
                if (kq_events_ptr_[i].flags & EV_EOF)
#elif __linux__
                if (ep_events_[i].events & EPOLLRDHUP || ep_events_[i].events & EPOLLERR) 
#endif
                {
                    //# close #----------
                    terminate_client(client_context_ptr); 
                }
#ifdef __APPLE__
                else if (EVFILT_READ == kq_events_ptr_[i].filter)
#elif __linux__
                else if (ep_events_[i].events & EPOLLIN) 
#endif
                {
                    //# recv #----------
                    if(! recv_data(client_context_ptr) ) 
                    {
                        terminate_client(client_context_ptr); 
                    }
                }
            }
        } //for
    } //while

    is_server_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
void  ASock::terminate_client(Context* context_ptr)
{
    client_cnt_--;
#ifdef __APPLE__
    control_kq(context_ptr, EVFILT_READ, EV_DELETE );
#elif __linux__
    control_ep(context_ptr, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_DEL ); //just in case
#endif

    close(context_ptr->socket_);
    if(cb_on_client_connected_!=nullptr)
    {
        cb_on_client_disconnected_(context_ptr);
    }
    else
    {
        on_client_disconnected(context_ptr);
    }

    push_client_context_to_cache(context_ptr);
}



///////////////////////////////////////////////////////////////////////////////
// CLIENT
///////////////////////////////////////////////////////////////////////////////
bool  ASock::init_tcp_client(const char* server_ip, 
                             int         server_port, 
                             int         connect_timeout_secs, 
                             int         max_data_len )
{
    sock_usage_ = SOCK_USAGE_TCP_CLIENT  ;

    disconnect(); 

    if(!set_buffer_capacity(max_data_len) )
    {
        return false;
    }

    context_.socket_ = socket(AF_INET,SOCK_STREAM,0) ;

    if( context_.socket_ < 0 )
    {
        err_msg_ = "init error [" + std::string(strerror(errno)) ;
        return false;
    }   

    if(!set_socket_non_blocking (context_.socket_))
    {
        return  false;
    }
    
    if(!is_buffer_init_ )
    {
        if  ( cumbuffer_defines::OP_RSLT_OK != context_.recvBuffer_.Init(recv_buffer_capcity_) )
        {
            err_msg_ = "cumBuffer Init error :" + context_.recvBuffer_.GetErrMsg();
            return false;
        }
        is_buffer_init_ = true;
    }
    else
    {
        //in case of reconnect
        context_.recvBuffer_.ReSet(); 
    }

    SOCKADDR_IN       server_addr ;
    memset((void *)&server_addr,0x00,sizeof(server_addr)) ;
    server_addr.sin_family      = AF_INET ;
    server_addr.sin_addr.s_addr = inet_addr( server_ip ) ;
    server_addr.sin_port = htons( server_port );

    struct timeval timeoutVal;
    timeoutVal.tv_sec  = connect_timeout_secs ;  
    timeoutVal.tv_usec = 0;

    int nRslt = connect(context_.socket_,(SOCKADDR *)&server_addr, (SOCKLEN_T )sizeof(SOCKADDR_IN)) ;

    if ( nRslt < 0)
    {
        if (errno != EINPROGRESS)
        {
            err_msg_ = "connect error [" + std::string(strerror(errno))+ "]";
            return false;
        }
    }

    if (nRslt == 0)
    {
        is_connected_ = true;
        return true;
    }

    fd_set   rset, wset;
    FD_ZERO(&rset);
    FD_SET(context_.socket_, &rset);
    wset = rset;

    nRslt = select(context_.socket_+1, &rset, &wset, NULL, &timeoutVal ) ;
    if (nRslt == 0 )
    {
        err_msg_ = "connect timeout";
        return false;
    }
    else if (nRslt< 0)
    {
        err_msg_ = "select :error [" + std::string(strerror(errno)) + "]";
        return false;
    }

    if (FD_ISSET(context_.socket_, &rset) || FD_ISSET(context_.socket_, &wset)) 
    {
        int  nSocketError = 0;
        socklen_t  len = sizeof(nSocketError);
        if (getsockopt(context_.socket_, SOL_SOCKET, SO_ERROR, &nSocketError, &len) < 0)
        {
            err_msg_ = "getsockopt :SO_ERROR :error [" + std::string(strerror(errno)) + "]";
            return false;
        }

        if (nSocketError) 
        {
            err_msg_ = "getsockopt :SO_ERROR :error [" + std::string(strerror(errno)) + "]";
            return false;
        }
    } 
    else
    {
        err_msg_ = "fd not set ";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< err_msg_ <<"\n"; 
        return false;
    }

    is_connected_ = true;

    if(!is_client_thread_running_ )
    {
#ifdef __APPLE__
        kq_events_ptr_ = new struct kevent;
        memset(kq_events_ptr_, 0x00, sizeof(struct kevent) );
        kq_fd_ = kqueue();
        if (kq_fd_ == -1)
        {
            err_msg_ = "kqueue error ["  + std::string(strerror(errno)) + "]";
            return false;
        }
#elif __linux__
        ep_events_ = new struct epoll_event;
        memset(ep_events_, 0x00, sizeof(struct epoll_event) );
        ep_fd_ = epoll_create1(0);
        if ( ep_fd_== -1)
        {
            err_msg_ = "epoll create error ["  + std::string(strerror(errno)) + "]";
            return false;
        }
#endif

        std::thread client_thread(&ASock::client_thread_routine, this);
        client_thread.detach();
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::client_thread_routine()
{
    is_client_thread_running_ = true;

#ifdef __APPLE__
    if(!control_kq(&context_, EVFILT_READ, EV_ADD ))
    {
        return;
    }
    struct timespec ts;
    ts.tv_sec  =1;
    ts.tv_nsec =0;
#elif __linux__
    if(!control_ep( &context_, EPOLLIN | EPOLLERR , EPOLL_CTL_ADD ))
    {
        return;
    }
#endif

    char szTempData[asock::DEFAULT_PACKET_SIZE];

    while(is_connected_)
    {
#ifdef __APPLE__
        int nEventCnt = kevent(kq_fd_, NULL, 0, kq_events_ptr_, 1, &ts); 
#elif __linux__
        int nEventCnt = epoll_wait(ep_fd_, ep_events_, 1, 1000 );
#endif
        if (nEventCnt < 0)
        {
#ifdef __APPLE__
            err_msg_ = "kevent error ["  + std::string(strerror(errno)) + "]";
#elif __linux__
            err_msg_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
#endif
            is_client_thread_running_ = false;
            return;
        }
#ifdef __APPLE__
        if (kq_events_ptr_->flags & EV_EOF)
#elif __linux__
        if (ep_events_->events & EPOLLRDHUP || ep_events_->events & EPOLLERR) 
#endif
        {
            //############## close ############################
            close( context_.socket_);
            if(cb_on_disconnected_from_server_!=nullptr)
            {
                cb_on_disconnected_from_server_();
            }
            else
            {
                on_disconnected_from_server();
            }
            break;
        }
#ifdef __APPLE__
        else if (EVFILT_READ == kq_events_ptr_->filter )
#elif __linux__
        else if (ep_events_->events & EPOLLIN) 
#endif
        {
            //############## recv ############################
            if(! recv_data(&context_) ) 
            {
                close( context_.socket_);
                if(cb_on_disconnected_from_server_!=nullptr)
                {
                    cb_on_disconnected_from_server_();
                }
                else
                {
                    on_disconnected_from_server();
                }
                break;
            }
        }

    } //while

    is_client_thread_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock:: send_to_server(const char* data, int len)
{
    if ( !is_connected_ )
    {
        err_msg_ = "not connected";
        return false;
    }

    return send_data(&context_, data, len);
}

///////////////////////////////////////////////////////////////////////////////
void ASock:: disconnect()
{
    if(context_.socket_ > 0 )
    {
        close(context_.socket_);
    }
    context_.socket_ = -1;
}

