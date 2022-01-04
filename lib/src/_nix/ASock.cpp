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
ASock::ASock()
{
    is_connected_ = false;
}

ASock::~ASock()
{
    if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT || 
         sock_usage_ == SOCK_USAGE_UDP_CLIENT ||   
         sock_usage_ == SOCK_USAGE_IPC_CLIENT ) {
#ifdef __APPLE__
        if(kq_events_ptr_) {
            delete kq_events_ptr_;
        }
#elif __linux__
        if(ep_events_) {
            delete ep_events_;
        }
#endif
        Disconnect();
    } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER || 
                sock_usage_ == SOCK_USAGE_UDP_SERVER ||  
                sock_usage_ == SOCK_USAGE_IPC_SERVER  ) {
#ifdef __APPLE__
        if ( kq_events_ptr_ ) { 
            delete [] kq_events_ptr_;    
        }
#elif __linux__
        if (ep_events_)   { 
            delete [] ep_events_;    
        }
#endif
        ClearClientCache();
#ifdef __APPLE__
        ControlKq(listen_context_ptr_, EVFILT_READ, EV_DELETE );
#elif __linux__
        ControlEpoll(listen_context_ptr_, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_DEL ); //just in case
#endif

#if defined __APPLE__ || defined __linux__ 
        delete listen_context_ptr_ ;
#endif
        if ( sock_usage_ == SOCK_USAGE_IPC_SERVER  ) {
            unlink(server_ipc_socket_path_.c_str());
        }
    }
}


///////////////////////////////////////////////////////////////////////////////
bool   ASock::SetSocketNonBlocking(int sock_fd)
{
    int oldflags  ;
    if ((oldflags = fcntl( sock_fd,F_GETFL, 0)) < 0 ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "fcntl F_GETFL error [" + std::string(strerror(errno))+ "]";
        return  false;
    }
    int ret  = fcntl( sock_fd,F_SETFL,oldflags | O_NONBLOCK) ;
    if ( ret < 0 ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "fcntl O_NONBLOCK error [" + std::string(strerror(errno))+ "]";
        return  false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SetSockoptSndRcvBufUdp(SOCKET_T socket)
{
    size_t opt_cur ; 
    int opt_val=max_data_len_ ; 
    int opt_len = sizeof(opt_cur) ;
    if (getsockopt(socket,SOL_SOCKET,SO_SNDBUF,&opt_cur, (SOCKLEN_T *) &opt_len)==-1) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "gsetsockopt SO_SNDBUF error ["  + std::string(strerror(errno)) + "]";
        return false;
    }
    if(max_data_len_ > opt_cur ) {
        if (setsockopt(socket,SOL_SOCKET,SO_SNDBUF,(char*)&opt_val, sizeof(opt_val))==-1) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "setsockopt SO_SNDBUF error ["  + std::string(strerror(errno)) + "]";
            return false;
        }
    }
    if (getsockopt(socket,SOL_SOCKET,SO_RCVBUF,&opt_cur, (SOCKLEN_T *)&opt_len)==-1) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "setsockopt SO_RCVBUF error ["  + std::string(strerror(errno)) + "]";
        return false;
    }
    if(max_data_len_ > opt_cur ) {
        if (setsockopt(socket,SOL_SOCKET,SO_RCVBUF,(char*)&opt_val, sizeof(opt_val))==-1) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "setsockopt SO_RCVBUF error ["  + std::string(strerror(errno)) + "]";
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RecvfromData(Context* ctx_ptr) //XXX context 가 지금 서버 것.
{
    if(max_data_len_ > ctx_ptr->recv_buffer.GetLinearFreeSpace() ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "no linear free space left " + 
            std::to_string( ctx_ptr->recv_buffer.GetLinearFreeSpace()) ;
        return false; 
    }

    SOCKLEN_T addrlen = sizeof(ctx_ptr->udp_remote_addr);
    int recved_len = recvfrom(ctx_ptr->socket, //--> is listen_socket_
                              ctx_ptr->recv_buffer.GetLinearAppendPtr(), 
                              max_data_len_ , 
                              0,
                              (struct sockaddr *)&ctx_ptr->udp_remote_addr, 
                              &addrlen ); 
    if( recved_len > 0) {
        ctx_ptr->recv_buffer.IncreaseData(recved_len);
        //udp got complete packet 
        char* complete_packet_data = new (std::nothrow) char [max_data_len_] ; 
        if(complete_packet_data == NULL) {
            err_msg_ = "memory alloc failed!";
            return false;
        }
        if(cumbuffer::OP_RSLT_OK!= 
           ctx_ptr->recv_buffer.GetData(recved_len, complete_packet_data )) {
            //error !
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = ctx_ptr->recv_buffer.GetErrMsg();
            std::cerr << err_msg_ << "\n";
            ctx_ptr->is_packet_len_calculated = false;
            delete[] complete_packet_data; //XXX
            return false; 
        }
        //XXX UDP 이므로 받는 버퍼를 초기화해서, linear free space를 초기화 상태로 
        ctx_ptr->recv_buffer.ReSet(); //this is udp. all data has arrived!

        if(cb_on_recved_complete_packet_!=nullptr) {
            //invoke user specific callback
            cb_on_recved_complete_packet_ (ctx_ptr, complete_packet_data , recved_len ); 
        } else {
            //invoke user specific implementation
            OnRecvedCompleteData(ctx_ptr,complete_packet_data , recved_len ); 
        }
        delete[] complete_packet_data; //XXX
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RecvData(Context* ctx_ptr) 
{	
    int want_recv_len = max_data_len_ ;
    if(max_data_len_ > ctx_ptr->recv_buffer.GetLinearFreeSpace() ) {
        want_recv_len = ctx_ptr->recv_buffer.GetLinearFreeSpace() ; 
    }
    if(want_recv_len==0) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "no linear free space left ";
        return false; 
    }
    int recved_len = recv( ctx_ptr->socket, ctx_ptr->recv_buffer.GetLinearAppendPtr(), want_recv_len, 0); 
    if( recved_len > 0) {
        ctx_ptr->recv_buffer.IncreaseData(recved_len);
        while(ctx_ptr->recv_buffer.GetCumulatedLen()) {
            //invoke user specific implementation
            if(!ctx_ptr->is_packet_len_calculated ) {
                //only when calculation is necessary
                if(cb_on_calculate_data_len_!=nullptr) {
                    //invoke user specific callback
                    ctx_ptr->complete_packet_len =cb_on_calculate_data_len_ ( ctx_ptr ); 
                } else {
                    //invoke user specific implementation
                    ctx_ptr->complete_packet_len = OnCalculateDataLen( ctx_ptr ); 
                }
                ctx_ptr->is_packet_len_calculated = true;
            }
            if(ctx_ptr->complete_packet_len == asock::MORE_TO_COME) {
                ctx_ptr->is_packet_len_calculated = false;
                return true; //need to recv more
            } else if(ctx_ptr->complete_packet_len > 
                      ctx_ptr->recv_buffer.GetCumulatedLen()) {
                return true; //need to recv more
            } else {
                //got complete packet 
                char* complete_packet_data = new (std::nothrow) char [max_data_len_] ; //XXX 
                if(complete_packet_data == NULL) {
                    err_msg_ = "memory alloc failed!";
                    return false;
                }
                if(cumbuffer::OP_RSLT_OK!= ctx_ptr->recv_buffer.GetData(ctx_ptr->complete_packet_len, 
                                                         complete_packet_data )) {
                    //error !
                    {
                        std::lock_guard<std::mutex> lock(err_msg_lock_);
                        err_msg_ = ctx_ptr->recv_buffer.GetErrMsg();
                    }
                    ctx_ptr->is_packet_len_calculated = false;
                    delete[] complete_packet_data; //XXX
                    return false; 
                }
                if(cb_on_recved_complete_packet_!=nullptr) {
                    //invoke user specific callback
                    cb_on_recved_complete_packet_ (ctx_ptr, complete_packet_data , ctx_ptr->complete_packet_len ); 
                } else {
                    //invoke user specific implementation
                    OnRecvedCompleteData(ctx_ptr, complete_packet_data , ctx_ptr->complete_packet_len ); 
                }
                ctx_ptr->is_packet_len_calculated = false;
                delete[] complete_packet_data; //XXX
            }
        } //while
    } else if( recved_len == 0 ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "recv 0, client disconnected , fd:" + std::to_string(ctx_ptr->socket);
        return false ;
    }
    return true ;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SendData (Context* ctx_ptr, const char* data_ptr, size_t len) 
{
    std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock);
    char* data_position_ptr = const_cast<char*>(data_ptr) ;   
    size_t total_sent = 0;           

    //if sent is pending, just push to queue. 
    if(ctx_ptr->is_sent_pending){ 
        PENDING_SENT pending_sent;
        pending_sent.pending_sent_data = new char [len]; 
        pending_sent.pending_sent_len  = len;
        memcpy(pending_sent.pending_sent_data, data_ptr, len);
        if(sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            //udp(server) 인 경우엔, 데이터와 client remote addr 정보를 함께 queue에 저장
            memcpy( &pending_sent.udp_remote_addr, &ctx_ptr->udp_remote_addr, sizeof(pending_sent.udp_remote_addr));
        }
        ctx_ptr->pending_send_deque.push_back(pending_sent);
#ifdef __APPLE__
        if(!ControlKq(ctx_ptr, EVFILT_WRITE, EV_ADD|EV_ENABLE )) {
#elif __linux__
        if(!ControlEpoll (ctx_ptr, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_MOD)){
#endif
            delete [] pending_sent.pending_sent_data ;
            ctx_ptr->pending_send_deque.pop_back();
            return false;
        }
        return true;
    }
    while( total_sent < len ) {
        int sent_len =0;
        if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            //XXX UDP : all or nothing. no partial sent!
            sent_len = sendto(ctx_ptr->socket,  data_position_ptr, len-total_sent , 0, 
                              (struct sockaddr*)& ctx_ptr->udp_remote_addr,   
                              sizeof(ctx_ptr->udp_remote_addr)) ;
        } else if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
            //XXX UDP : all or nothing. no partial sent!
            sent_len = sendto(ctx_ptr->socket,  data_position_ptr, len-total_sent , 0, 
                              0, //XXX client : already set! (via connect)  
                              sizeof(ctx_ptr->udp_remote_addr)) ;
        } else {
            sent_len = send(ctx_ptr->socket, data_position_ptr, len-total_sent, 0);
        }

        if(sent_len > 0) {
            total_sent += sent_len ;  
            data_position_ptr += sent_len ;      
        } else if( sent_len < 0 ) {
            if ( errno == EWOULDBLOCK || errno == EAGAIN ) {
                //send later
                PENDING_SENT pending_sent;
                pending_sent.pending_sent_data = new char [len-total_sent]; 
                pending_sent.pending_sent_len  = len-total_sent;
                memcpy(pending_sent.pending_sent_data, data_position_ptr, len-total_sent);
                if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
                    //udp(server) 인 경우엔, 데이터와 client remote addr 정보를 함께 queue에 저장
                    memcpy( &pending_sent.udp_remote_addr, 
                            &ctx_ptr->udp_remote_addr, sizeof(pending_sent.udp_remote_addr));
                }
                ctx_ptr->pending_send_deque.push_back(pending_sent);
#ifdef __APPLE__
                if(!ControlKq(ctx_ptr, EVFILT_WRITE, EV_ADD|EV_ENABLE )){
#elif __linux__
                if(!ControlEpoll (ctx_ptr, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_MOD)){
#endif
                    delete [] pending_sent.pending_sent_data;
                    ctx_ptr->pending_send_deque.pop_back();
                    return false;
                }
                ctx_ptr->is_sent_pending = true;
                return true;
            } else if ( errno != EINTR ) {
                //XXX err_msg_ need lock for multithread
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = "send error [" + std::string(strerror(errno)) + "]";
                return false;
            }
        }
    }//while
    return true;
}

#ifdef __APPLE__
///////////////////////////////////////////////////////////////////////////////
bool ASock::ControlKq(Context* ctx_ptr , uint32_t events, uint32_t fflags)
{
    struct  kevent kq_event;
    memset(&kq_event, 0, sizeof(struct kevent));
    EV_SET(&kq_event, ctx_ptr->socket, events,fflags , 0, 0, ctx_ptr); 
    //udata = ctx_ptr

    int result = kevent(kq_fd_, &kq_event, 1, NULL, 0, NULL);
    if (result == -1) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "kevent error [" + std::string(strerror(errno)) + "]";
        return false; 
    }
    return true;
    //man:Re-adding an existing event will modify the parameters of the
    //    original event, and not result in a duplicate entry.
}
#elif __linux__
///////////////////////////////////////////////////////////////////////////////
bool ASock::ControlEpoll(Context* ctx_ptr , uint32_t events, int op)
{
    struct  epoll_event ev_client{};
    ev_client.data.fd    = ctx_ptr->socket;
    ev_client.events     = events ;
    ev_client.data.ptr   = ctx_ptr;

    if(epoll_ctl(ep_fd_, op, ctx_ptr->socket, &ev_client)<0) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "kevent error [" + std::string(strerror(errno)) + "]";
        return false; 
    }
    return true;
}
#endif


///////////////////////////////////////////////////////////////////////////////
// SERVER
///////////////////////////////////////////////////////////////////////////////
bool ASock::InitTcpServer(const char* bind_ip, 
                          int         bind_port, 
                          size_t      max_data_len /*=DEFAULT_PACKET_SIZE*/,
                          size_t      max_client /*=DEFAULT_MAX_CLIENT*/)
{
    sock_usage_ = SOCK_USAGE_TCP_SERVER  ;
    server_ip_ = bind_ip ; 
    server_port_ = bind_port ; 
    max_client_limit_ = max_client ; 
    if(max_client_limit_<0) {
        return false;
    }
    if(!SetBufferCapacity(max_data_len)) {
        return false;
    }
    return RunServer();
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::InitUdpServer(const char* bind_ip, 
                          size_t      bind_port, 
                          size_t      max_data_len /*=DEFAULT_PACKET_SIZE*/,
                          size_t      max_client /*=DEFAULT_MAX_CLIENT*/)
{
    sock_usage_ = SOCK_USAGE_UDP_SERVER  ;
    server_ip_ = bind_ip ; 
    server_port_ = bind_port ; 
    max_client_limit_ = max_client ; 
    if(max_client_limit_<0) {
        return false;
    }
    if(!SetBufferCapacity(max_data_len)) {
        return false;
    }
    return RunServer();
}

///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitIpcServer (const char* sock_path, 
                            size_t      max_data_len /*=DEFAULT_PACKET_SIZE*/,
                            size_t      max_client /*=DEFAULT_MAX_CLIENT*/)
{
    sock_usage_ = SOCK_USAGE_IPC_SERVER  ;
    server_ipc_socket_path_ = sock_path;
    max_client_limit_ = max_client ; 
    if(max_client_limit_<0) {
        return false;
    }
    if(!SetBufferCapacity(max_data_len)) {
        return false;
    }
    return RunServer();
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RunServer()
{
#ifdef __APPLE__
    if ( kq_events_ptr_ ) {
#elif __linux__
    if (ep_events_) {
#endif
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "error [server is already running]";
        return false;
    }
    if ( sock_usage_ == SOCK_USAGE_IPC_SERVER ) {
        listen_socket_ = socket(AF_UNIX,SOCK_STREAM,0) ;
    } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER ) {
        listen_socket_ = socket(AF_INET,SOCK_STREAM,0) ;
    } else if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
        listen_socket_ = socket(AF_INET,SOCK_DGRAM,0) ;
    }
    if( listen_socket_ < 0 ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "init error [" + std::string(strerror(errno)) + "]";
        return false;
    }   
    if(!SetSocketNonBlocking (listen_socket_)) {
        return  false;
    }
    int opt_on=1;
    int result = -1;
    if (setsockopt(listen_socket_,SOL_SOCKET,SO_REUSEADDR,&opt_on,sizeof(opt_on))==-1) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "setsockopt SO_REUSEADDR error ["  + std::string(strerror(errno)) + "]";
        return false;
    }
    if (setsockopt(listen_socket_,SOL_SOCKET,SO_KEEPALIVE, &opt_on, sizeof(opt_on))==-1) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "setsockopt SO_KEEPALIVE error ["  + std::string(strerror(errno)) + "]";
        return false;
    }
    if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
        if(!SetSockoptSndRcvBufUdp(listen_socket_)) {
            return false;
        }
    }
    //-------------------------------------------------
    if ( sock_usage_ == SOCK_USAGE_IPC_SERVER ) {
        SOCKADDR_UN ipc_server_addr ;
        memset((void *)&ipc_server_addr,0x00,sizeof(ipc_server_addr)) ;
        ipc_server_addr.sun_family = AF_UNIX;
        snprintf(ipc_server_addr.sun_path, sizeof(ipc_server_addr.sun_path),
                "%s",server_ipc_socket_path_.c_str()); 
        result = bind(listen_socket_,(SOCKADDR*)&ipc_server_addr, sizeof(ipc_server_addr)) ;
    } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER || sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
        SOCKADDR_IN    server_addr  ;
        memset((void *)&server_addr,0x00,sizeof(server_addr)) ;
        server_addr.sin_family      = AF_INET ;
        server_addr.sin_addr.s_addr = inet_addr(server_ip_.c_str()) ;
        server_addr.sin_port = htons(server_port_);
        result = bind(listen_socket_,(SOCKADDR*)&server_addr,sizeof(server_addr)) ;
    }
    //-------------------------------------------------
    if ( result < 0 ) {
        err_msg_ = "bind error ["  + std::string(strerror(errno)) + "]";
        return false ;
    }
    if ( sock_usage_ == SOCK_USAGE_IPC_SERVER || sock_usage_ == SOCK_USAGE_TCP_SERVER ) {
        result = listen(listen_socket_,SOMAXCONN) ;
        if ( result < 0 ) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "listen error [" + std::string(strerror(errno)) + "]";
            return false ;
        }
    }
    struct sigaction act;
    act.sa_handler = SIG_IGN;
    sigemptyset( &act.sa_mask );
    act.sa_flags = 0;
    sigaction( SIGPIPE, &act, NULL );
#if defined __APPLE__ || defined __linux__ 
    listen_context_ptr_ = new (std::nothrow) Context();
    if(!listen_context_ptr_) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "Context alloc failed !";
        return false;
    }
    listen_context_ptr_->socket = listen_socket_;
#endif
#ifdef __APPLE__
    kq_fd_ = kqueue();
    if (kq_fd_ == -1) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "kqueue error ["  + std::string(strerror(errno)) + "]";
        return false;
    }
    if(!ControlKq(listen_context_ptr_, EVFILT_READ, EV_ADD )) {
        return false;
    }
#elif __linux__
    ep_fd_ = epoll_create1(0);
    if (ep_fd_ == -1) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "epoll create error ["  + std::string(strerror(errno)) + "]";
        return false;
    }
    if(!ControlEpoll ( listen_context_ptr_, EPOLLIN | EPOLLERR , EPOLL_CTL_ADD )) {
        return false;
    }
#endif
    //start server thread
    is_need_server_run_ = true;
    is_server_running_  = true;
#ifdef __APPLE__
    kq_events_ptr_ = new struct kevent[max_client_limit_];
    memset(kq_events_ptr_, 0x00, sizeof(struct kevent) * max_client_limit_);
#elif __linux__
    ep_events_ = new struct epoll_event[max_client_limit_];
    memset(ep_events_, 0x00, sizeof(struct epoll_event) * max_client_limit_);
#endif
    if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
        //UDP is special~~~ XXX 
        std::thread server_thread(&ASock::ServerThreadUdpRoutine, this);
        server_thread.detach();
    } else {
        std::thread server_thread(&ASock::ServerThreadRoutine, this);
        server_thread.detach();
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::StopServer()
{
    is_need_server_run_ = false;
}

///////////////////////////////////////////////////////////////////////////////
Context* ASock::PopClientContextFromCache()
{
    Context* ctx_ptr = nullptr;
    {//lock scope
        std::lock_guard<std::mutex> lock(cache_lock_);
        if (!queue_client_cache_.empty()) {
            ctx_ptr = queue_client_cache_.front();
            queue_client_cache_.pop();
            return ctx_ptr;
        }
    }
    ctx_ptr = new (std::nothrow) Context();
    if(!ctx_ptr) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "Context alloc failed !";
        return nullptr;
    }
    if ( cumbuffer::OP_RSLT_OK != ctx_ptr->recv_buffer.Init(max_data_len_) ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_  = std::string("cumBuffer Init error : ") + 
            std::string(ctx_ptr->recv_buffer.GetErrMsg());
        return nullptr;
    }
    return ctx_ptr ;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::PushClientContextToCache(Context* ctx_ptr)
{
    //reset
    ctx_ptr->recv_buffer.ReSet();
    ctx_ptr->socket = -1;
    ctx_ptr->is_packet_len_calculated = false;
    ctx_ptr->is_sent_pending = false;
    ctx_ptr->complete_packet_len = 0;
    while(!ctx_ptr->pending_send_deque.empty() ) {
        PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
        delete [] pending_sent.pending_sent_data;
        ctx_ptr->pending_send_deque.pop_front();
    }
    std::lock_guard<std::mutex> lock(cache_lock_);
    queue_client_cache_.push(ctx_ptr);
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ClearClientCache()
{
    std::lock_guard<std::mutex> lock(cache_lock_);
    while(!queue_client_cache_.empty() ) {
        Context* ctx_ptr = queue_client_cache_.front();
        while(!ctx_ptr->pending_send_deque.empty() ) {
            PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
            delete [] pending_sent.pending_sent_data;
            ctx_ptr->pending_send_deque.pop_front();
        }
        delete ctx_ptr;
        queue_client_cache_.pop();
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::AcceptNewClient()
{
    while(true) {
        int client_fd = -1;
        if ( sock_usage_ == SOCK_USAGE_IPC_SERVER ) {
            SOCKADDR_UN client_addr ; 
            SOCKLEN_T client_addr_size = sizeof(client_addr);
            client_fd = accept(listen_socket_,(SOCKADDR*)&client_addr,&client_addr_size ) ;
        } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER ) {
            SOCKADDR_IN client_addr  ;
            SOCKLEN_T client_addr_size =sizeof(client_addr);
            client_fd = accept(listen_socket_,(SOCKADDR*)&client_addr,&client_addr_size ) ;
        }
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                //all accept done...
                break;
            } else if (errno == ECONNABORTED) {
                break;
            } else {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = "accept error [" + std::string(strerror(errno)) + "]";
                is_server_running_ = false;
                return false;
            }
        }
        ++client_cnt_;
        SetSocketNonBlocking(client_fd);
        Context* client_context_ptr = PopClientContextFromCache();
        if(client_context_ptr==nullptr) {
            is_server_running_ = false;
            return false;
        }
        client_context_ptr->socket = client_fd;

        if(cb_on_client_connected_!=nullptr) {
            cb_on_client_connected_(client_context_ptr);
        } else {
            OnClientConnected(client_context_ptr);
        }
#ifdef __APPLE__
        if(!ControlKq(client_context_ptr, EVFILT_READ, EV_ADD )){
#elif __linux__
        if(!ControlEpoll( client_context_ptr, EPOLLIN |EPOLLRDHUP, EPOLL_CTL_ADD)){
#endif
            is_server_running_ = false;
            return false;
        }
    }//while : accept
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock:: ServerThreadUdpRoutine()
{
#ifdef __APPLE__
    struct timespec ts;
    ts.tv_sec  =1;
    ts.tv_nsec =0;
#endif
    if (cumbuffer::OP_RSLT_OK != listen_context_ptr_->recv_buffer.Init(max_data_len_) ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_  = "cumBuffer Init error : " + 
                    std::string(listen_context_ptr_->recv_buffer.GetErrMsg());
        is_server_running_ = false;
        return ;
    }
    while(is_need_server_run_) {
#ifdef __APPLE__
        int event_cnt = kevent(kq_fd_, NULL, 0, kq_events_ptr_, max_client_limit_, &ts); 
        if (event_cnt < 0) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "kevent error ["  + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#elif __linux__
        int event_cnt = epoll_wait(ep_fd_, ep_events_, max_client_limit_, 1000 );
        if (event_cnt < 0) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#endif
        for (int i = 0; i < event_cnt; i++) {
#ifdef __APPLE__
            if (EVFILT_READ == kq_events_ptr_[i].filter){
#elif __linux__
            if (ep_events_[i].events & EPOLLIN) {
#endif
                //# recv #----------
                if(! RecvfromData(listen_context_ptr_)) {
                    break;
                }
            }
#ifdef __APPLE__
            else if (EVFILT_WRITE == kq_events_ptr_[i].filter ){
#elif __linux__
            else if (ep_events_[i].events & EPOLLOUT) {
#endif
                //# send #----------
                if(!SendPendingData(listen_context_ptr_)) {
                    return; //error!
                }
            } 
        } 
    }//while
    is_server_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
void ASock:: ServerThreadRoutine()
{
#ifdef __APPLE__
    struct timespec ts;
    ts.tv_sec  =1;
    ts.tv_nsec =0;
#endif
    while(is_need_server_run_) {
#ifdef __APPLE__
        int event_cnt = kevent(kq_fd_, NULL, 0, kq_events_ptr_, max_client_limit_, &ts); 
        if (event_cnt < 0) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "kevent error ["  + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#elif __linux__
        int event_cnt = epoll_wait(ep_fd_, ep_events_, max_client_limit_, 1000 );
        if (event_cnt < 0) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#endif
        for (int i = 0; i < event_cnt; i++) {
#ifdef __APPLE__
            if (kq_events_ptr_[i].ident   == listen_socket_) {
#elif __linux__
            if (((Context*)ep_events_[i].data.ptr)->socket == listen_socket_){
#endif
                //# accept #----------
                if(!AcceptNewClient()) {
                    std::cerr <<"accept error:" << err_msg_ << "\n";
                    return;
                }
            } else {
#ifdef __APPLE__
                Context* ctx_ptr = (Context*)kq_events_ptr_[i].udata;
                if (kq_events_ptr_[i].flags & EV_EOF){
#elif __linux__
                Context* ctx_ptr = (Context*)ep_events_[i].data.ptr ;
                if (ep_events_[i].events & EPOLLRDHUP || ep_events_[i].events & EPOLLERR) {
#endif
                    //# close #----------
                    TerminateClient(ctx_ptr); 
                }
#ifdef __APPLE__
                else if (EVFILT_READ == kq_events_ptr_[i].filter){
#elif __linux__
                else if (ep_events_[i].events & EPOLLIN) {
#endif
                    //# recv #----------
                    if(! RecvData(ctx_ptr) ) {
                        TerminateClient(ctx_ptr); 
                    }
                }
#ifdef __APPLE__
                else if (EVFILT_WRITE == kq_events_ptr_[i].filter ){
#elif __linux__
                else if (ep_events_[i].events & EPOLLOUT) {
#endif
                    //# send #----------
                    if(!SendPendingData(ctx_ptr)) {
                        return; //error!
                    }
                } 
            } 
        } 
    } //while
    is_server_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SendPendingData(Context* ctx_ptr)
{
    std::lock_guard<std::mutex> guard(ctx_ptr->ctx_lock);
    while(!ctx_ptr->pending_send_deque.empty()) {
        PENDING_SENT pending_sent = ctx_ptr->pending_send_deque.front();
        int sent_len = 0;
        if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            //XXX UDP : all or nothing . no partial sent!
            sent_len = sendto(ctx_ptr->socket,  pending_sent.pending_sent_data, pending_sent.pending_sent_len, 
                              0, (struct sockaddr*)& pending_sent.udp_remote_addr,   
                              sizeof(pending_sent.udp_remote_addr)) ;
        } else if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
            //XXX UDP : all or nothing . no partial sent!
            sent_len = sendto(ctx_ptr->socket,  pending_sent.pending_sent_data, pending_sent.pending_sent_len, 
                              0, 0, //XXX client : already set! (via connect)  
                              sizeof(pending_sent.udp_remote_addr)) ;
        } else {
            sent_len = send(ctx_ptr->socket, pending_sent.pending_sent_data, pending_sent.pending_sent_len, 0) ;
        }
        if( sent_len > 0 ) {
            if(sent_len == pending_sent.pending_sent_len) {
                delete [] pending_sent.pending_sent_data;
                ctx_ptr->pending_send_deque.pop_front();
                if(ctx_ptr->pending_send_deque.empty()) {
                    //sent all data
                    ctx_ptr->is_sent_pending = false; 
#ifdef __APPLE__
                    if(!ControlKq(ctx_ptr, EVFILT_WRITE, EV_DELETE ) || !ControlKq(ctx_ptr, EVFILT_READ, EV_ADD)){
#elif __linux__
                    if(!ControlEpoll (ctx_ptr, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_MOD)){
#endif
                        //error!!!
                        if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT || sock_usage_ == SOCK_USAGE_IPC_CLIENT ) {
                            close( ctx_ptr->socket);
                            InvokeServerDisconnectedHandler();
                            is_client_thread_running_ = false;
                        } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER || sock_usage_ == SOCK_USAGE_IPC_SERVER  ) {
                            is_server_running_ = false;
                        }
                        return false;
                    }
                    break;
                }
            } else {
                //TCP : partial sent ---> 남은 부분을 다시 제일 처음으로
                PENDING_SENT partial_pending_sent;
                int alloc_len = pending_sent.pending_sent_len - sent_len;
                partial_pending_sent.pending_sent_data = new char [alloc_len]; 
                partial_pending_sent.pending_sent_len  = alloc_len;
                memcpy( partial_pending_sent.pending_sent_data, 
                        pending_sent.pending_sent_data+sent_len, alloc_len);
                //remove first.
                delete [] pending_sent.pending_sent_data;
                ctx_ptr->pending_send_deque.pop_front();
                //push_front
                ctx_ptr->pending_send_deque.push_front(partial_pending_sent);
                break; //next time
            }
        } else if( sent_len < 0 ) {
            if ( errno == EWOULDBLOCK || errno == EAGAIN ) {
                break; //next time
            } else if ( errno != EINTR ) {
                {//lock scope
                    std::lock_guard<std::mutex> lock(err_msg_lock_);
                    err_msg_ = "send error ["  + std::string(strerror(errno)) + "]";
                }
                if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT || sock_usage_ == SOCK_USAGE_IPC_CLIENT ) {
                    //client error!!!
                    close( ctx_ptr->socket);
                    InvokeServerDisconnectedHandler();
                    is_client_thread_running_ = false;
                    return false; 
                } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER || sock_usage_ == SOCK_USAGE_IPC_SERVER  ) {
                    TerminateClient(ctx_ptr); 
                }
                break;
            } 
        } 
    } //while
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void  ASock::TerminateClient( Context* client_ctx, bool is_graceful)
{
	client_cnt_--;
	DBG_LOG(" socket : "<< client_ctx->socket <<", connection closing ,graceful=" << (is_graceful ? "TRUE" : "FALSE"));
	if (!is_graceful) {
		// force the subsequent closesocket to be abortative.
		struct linger linger_struct ;
		linger_struct.l_onoff = 1;
		linger_struct.l_linger = 0;
		setsockopt(client_ctx->socket, SOL_SOCKET, SO_LINGER, (char*)&linger_struct, sizeof(linger_struct));
	}
#ifdef __APPLE__
    ControlKq(client_ctx, EVFILT_READ, EV_DELETE );
#elif __linux__
    ControlEpoll(client_ctx, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_DEL ); //just in case
#endif
	close(client_ctx->socket);
	if (cb_on_client_disconnected_ != nullptr) {
		cb_on_client_disconnected_(client_ctx);
	} else {
		OnClientDisconnected(client_ctx);
	}
	PushClientContextToCache(client_ctx);
}

///////////////////////////////////////////////////////////////////////////////
// CLIENT
///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitTcpClient(const char* server_ip, 
                           unsigned short  server_port, 
                           int         connect_timeout_secs, 
                           size_t      max_data_len /*DEFAULT_PACKET_SIZE*/ )
{
    sock_usage_ = SOCK_USAGE_TCP_CLIENT  ;
    connect_timeout_secs_ = connect_timeout_secs;
    if(!SetBufferCapacity(max_data_len) ) {
        return false;
    }
    context_.socket = socket(AF_INET,SOCK_STREAM,0) ;
    memset((void *)&tcp_server_addr_,0x00,sizeof(tcp_server_addr_)) ;
    tcp_server_addr_.sin_family      = AF_INET ;
    tcp_server_addr_.sin_addr.s_addr = inet_addr( server_ip ) ;
    tcp_server_addr_.sin_port = htons( server_port );
    return ConnectToServer();  
}

///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitUdpClient(const char* server_ip, 
                           unsigned short  server_port, 
                           size_t      max_data_len /*DEFAULT_PACKET_SIZE*/ )
{
    sock_usage_ = SOCK_USAGE_UDP_CLIENT  ;
    if(!SetBufferCapacity(max_data_len) ) {
        return false;
    }
    context_.socket = socket(AF_INET,SOCK_DGRAM,0) ;
    memset((void *)&udp_server_addr_,0x00,sizeof(udp_server_addr_)) ;
    udp_server_addr_.sin_family      = AF_INET ;
    udp_server_addr_.sin_addr.s_addr = inet_addr( server_ip ) ;
    udp_server_addr_.sin_port = htons( server_port );
    return ConnectToServer();  
}
///////////////////////////////////////////////////////////////////////////////
bool ASock::InitIpcClient(const char* sock_path,  
                          int connect_timeout_secs, size_t max_data_len)
{
    sock_usage_ = SOCK_USAGE_IPC_CLIENT  ;
    connect_timeout_secs_ = connect_timeout_secs;
    if(!SetBufferCapacity(max_data_len) ) {
        return false;
    }
    server_ipc_socket_path_ = sock_path ;
    context_.socket = socket(AF_UNIX,SOCK_STREAM,0) ;
    memset((void *)&ipc_conn_addr_,0x00,sizeof(ipc_conn_addr_)) ;
    ipc_conn_addr_.sun_family = AF_UNIX;
    snprintf(ipc_conn_addr_.sun_path, sizeof(ipc_conn_addr_.sun_path), "%s",sock_path); 
    return ConnectToServer();  
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::ConnectToServer()
{
    if(is_connected_ ){
        return true;
    }
    if( context_.socket < 0 ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "error : server socket is invalid" ;
        return false;
    }   
    if(!SetSocketNonBlocking (context_.socket)) {
        close(context_.socket);
        return  false;
    }
    if(!is_buffer_init_ ) {
        if(cumbuffer::OP_RSLT_OK != context_.recv_buffer.Init(max_data_len_) ) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ =  std::string("cumBuffer Init error :") + std::string(context_.recv_buffer.GetErrMsg());
            close(context_.socket);
            return false;
        }
        is_buffer_init_ = true;
    } else {
        //in case of reconnect
        context_.recv_buffer.ReSet(); 
    }
    struct timeval timeoutVal;
    timeoutVal.tv_sec  = connect_timeout_secs_ ;  
    timeoutVal.tv_usec = 0;
    int result = -1;
    if ( sock_usage_ == SOCK_USAGE_IPC_CLIENT ) {
        result = connect(context_.socket,(SOCKADDR*)&ipc_conn_addr_, (SOCKLEN_T)sizeof(SOCKADDR_UN)) ; 
    } else if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT ) {
        result = connect(context_.socket,(SOCKADDR*)&tcp_server_addr_,(SOCKLEN_T)sizeof(SOCKADDR_IN)) ;
    } else if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
        if(!SetSockoptSndRcvBufUdp(context_.socket)) {
            close(context_.socket);
            return false;
        }
        result = connect(context_.socket,(SOCKADDR*)&udp_server_addr_,(SOCKLEN_T)sizeof(SOCKADDR_IN)) ;
    } else {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "invalid socket usage" ;
        close(context_.socket);
        return false;
    }
    if ( result < 0) {
        if (errno != EINPROGRESS) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "connect error [" + std::string(strerror(errno))+ "]";
            close(context_.socket);
            return false;
        }
    }
    if (result == 0) {
        is_connected_ = true;
        return RunClientThread();
    }
    fd_set   rset, wset;
    FD_ZERO(&rset);
    FD_SET(context_.socket, &rset);
    wset = rset;
    result = select(context_.socket+1, &rset, &wset, NULL, &timeoutVal ) ;
    if (result == 0 ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "connect timeout";
        close(context_.socket);
        return false;
    } else if (result< 0) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
        close(context_.socket);
        return false;
    }
    if (FD_ISSET(context_.socket, &rset) || FD_ISSET(context_.socket, &wset)) {
        int  socketerror = 0;
        SOCKLEN_T  len = sizeof(socketerror);
        if (getsockopt(context_.socket, SOL_SOCKET, SO_ERROR, &socketerror, &len) < 0) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
            close(context_.socket);
            return false;
        }
        if (socketerror) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
            close(context_.socket);
            return false;
        }
    } else {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "connect error : fd not set ";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< err_msg_ <<"\n"; 
        close(context_.socket);
        return false;
    }
    is_connected_ = true;
    return RunClientThread();
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RunClientThread()
{
    if(!is_client_thread_running_ ) {
#ifdef __APPLE__
        kq_events_ptr_ = new struct kevent;
        memset(kq_events_ptr_, 0x00, sizeof(struct kevent) );
        kq_fd_ = kqueue();
        if (kq_fd_ == -1) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "kqueue error ["  + std::string(strerror(errno)) + "]";
            close(context_.socket);
            return false;
        }
#elif __linux__
        ep_events_ = new struct epoll_event;
        memset(ep_events_, 0x00, sizeof(struct epoll_event) );
        ep_fd_ = epoll_create1(0);
        if ( ep_fd_== -1) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "epoll create error ["  + std::string(strerror(errno)) + "]";
            close(context_.socket);
            return false;
        }
#endif

#ifdef __APPLE__
        if(!ControlKq(&context_, EVFILT_READ, EV_ADD )) {
            close(context_.socket);
            return false;
        }
#elif __linux__
        if(!ControlEpoll( &context_, EPOLLIN | EPOLLERR , EPOLL_CTL_ADD )) {
            close(context_.socket);
            return false;
        }
#endif
        std::thread client_thread(&ASock::ClientThreadRoutine, this);
        client_thread.detach();
        is_client_thread_running_ = true;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ClientThreadRoutine()
{
    while(is_connected_) {
#ifdef __APPLE__
        struct timespec ts;
        ts.tv_sec  =1;
        ts.tv_nsec =0;
        int event_cnt = kevent(kq_fd_, NULL, 0, kq_events_ptr_, 1, &ts); 
#elif __linux__
        int event_cnt = epoll_wait(ep_fd_, ep_events_, 1, 1000 );
#endif
        if (event_cnt < 0) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
#ifdef __APPLE__
            err_msg_ = "kevent error ["  + std::string(strerror(errno)) + "]";
#elif __linux__
            err_msg_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
#endif
            is_client_thread_running_ = false;
            return;
        }
        //############## close ###########################
#ifdef __APPLE__
        if (kq_events_ptr_->flags & EV_EOF){
#elif __linux__
        if (ep_events_->events & EPOLLRDHUP || ep_events_->events & EPOLLERR) {
#endif
            close( context_.socket);
            InvokeServerDisconnectedHandler();
            break;
        }
        //############## recv ############################
#ifdef __APPLE__
        else if (EVFILT_READ == kq_events_ptr_->filter ){
#elif __linux__
        else if (ep_events_->events & EPOLLIN) {
#endif
            if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
                if(! RecvfromData(&context_) ) {
                    close( context_.socket);
                    InvokeServerDisconnectedHandler();
                    break;
                }
            } else {
                if(! RecvData(&context_) ) {
                    close( context_.socket);
                    InvokeServerDisconnectedHandler();
                    break;
                }
            }
        }
        //############## send ############################
#ifdef __APPLE__
        else if ( EVFILT_WRITE == kq_events_ptr_->filter ){
#elif __linux__
        else if (ep_events_->events & EPOLLOUT) {
#endif
            if(!SendPendingData(&context_)) {
                return; //error!
            }
        }//send
    } //while
    is_client_thread_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::InvokeServerDisconnectedHandler()
{
    if(cb_on_disconnected_from_server_!=nullptr) {
        cb_on_disconnected_from_server_();
    } else {
        OnDisconnectedFromServer();
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock:: SendToServer(const char* data, size_t len)
{
    if ( !is_connected_ ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "not connected";
        return false;
    }
    return SendData(&context_, data, len);
}

///////////////////////////////////////////////////////////////////////////////
void ASock:: Disconnect()
{
    if(context_.socket > 0 ) {
        close(context_.socket);
    }
    context_.socket = -1;
    is_connected_ = false;
}

///////////////////////////////////////////////////////////////////////////////
//for client use
void ASock:: WaitForClientLoopExit()
{
    //wait thread exit
    if (client_thread_.joinable()) {
        client_thread_.join();
    }
}


