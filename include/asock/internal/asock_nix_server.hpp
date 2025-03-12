/******************************************************************************
MIT License

Copyright (c) 2025 jung hyun, ko

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

#ifndef ASOCKNIXSERVER_HPP
#define ASOCKNIXSERVER_HPP

#include <thread>
#include <signal.h>
#include "asock_nix_base.hpp"

////////////////////////////////////////////////////////////////////////////////
namespace asock {

class ASockServerBase : public ASockNixBase{
public :
    virtual ~ASockServerBase() {
        ClearServer();
    }

    //-------------------------------------------------------------------------
    bool  SetCbOnClientConnected(std::function<void(Context*)> cb) {
        if (cb != nullptr) {
            cb_on_client_connected_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    bool  SetCbOnClientDisconnected(std::function<void(Context*)> cb) {
        if(cb != nullptr) {
            cb_on_client_disconnected_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    bool  IsServerRunning(){
        return is_server_running_;
    }

    //-------------------------------------------------------------------------
    void  StopServer() {
        close(listen_socket_);
        is_need_server_run_ = false;
        if(sock_usage_ == SOCK_USAGE_IPC_SERVER) {
            DBG_LOG("unlink :" << server_ipc_socket_path_);
            unlink(server_ipc_socket_path_.c_str());
            server_ipc_socket_path_="";
        }
        while(is_server_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // 사용자가 이것호출이후 바로 exit 하는 경우, thread 깔끔하게 정리안되는
        // 현상이 발생함. valgrind : possibly lost --> 약간 여유를 준다 
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ClearServer();
    }
    //-------------------------------------------------------------------------
    size_t GetMaxEventCount(){
        return max_event_ ;
    }
    //-------------------------------------------------------------------------
    int GetCountOfClients(){
        return client_cnt_;
    }

private:
    std::atomic<int>  client_cnt_ {0};
    SOCKET_T          listen_socket_ ;
    std::atomic<bool> is_need_server_run_ {true};
#if defined __APPLE__ || defined __linux__
    Context*  listen_context_ptr_ {nullptr};

    std::function<void(Context*)> cb_on_client_connected_ {nullptr};
    std::function<void(Context*)> cb_on_client_disconnected_ {nullptr};
    //for inheritance : Implement these virtual functions.
    virtual void OnClientConnected(Context*) {};
    virtual void OnClientDisconnected(Context*) {};
#endif

    //-------------------------------------------------------------------------
    void ClearServer(){
#ifdef __APPLE__
        if ( kq_events_ptr_ ){
            delete [] kq_events_ptr_;
            kq_events_ptr_=nullptr;
        }
#elif __linux__
        if (ep_events_){
            delete [] ep_events_;
            ep_events_=nullptr;
        }
#endif
        ClearClientCache();
        if(sock_usage_ == SOCK_USAGE_IPC_SERVER) {
            if(server_ipc_socket_path_.length()>0){
                DBG_LOG("unlink :" << server_ipc_socket_path_);
                unlink(server_ipc_socket_path_.c_str());
                server_ipc_socket_path_="";
            }
        }
        if(listen_context_ptr_){
#ifdef __APPLE__
            ControlKq(listen_context_ptr_, EVFILT_READ, EV_DELETE );
#elif __linux__
            ControlEpoll(listen_context_ptr_, EPOLLIN , EPOLL_CTL_DEL );
#endif
            delete listen_context_ptr_ ;
            listen_context_ptr_=nullptr;
        }
    }

    //-------------------------------------------------------------------------
    bool AcceptNewClient() {
        while(true) {
            int client_fd = -1;
            if(sock_usage_ == SOCK_USAGE_IPC_SERVER) {
                SOCKADDR_UN client_addr ; 
                SOCKLEN_T client_addr_size = sizeof(client_addr);
                client_fd = accept(listen_socket_,
                                 (SOCKADDR*)&client_addr,
                             &client_addr_size );
            } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER ) {
                SOCKADDR_IN client_addr  ;
                SOCKLEN_T client_addr_size =sizeof(client_addr);
                client_fd = accept(listen_socket_,
                                 (SOCKADDR*)&client_addr,
                             &client_addr_size );
            }
            if (client_fd == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    //all accept done...
                    break;
                } else if (errno == ECONNABORTED) {
                    break;
                } else {
                    err_msg_ = "accept error [" 
                        + std::string(strerror(errno)) + "]";
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
            client_context_ptr->is_connected = true;
            map_sock_ctx_.insert({client_fd, client_context_ptr});

#ifdef __APPLE__
            if(!ControlKq(client_context_ptr, EVFILT_READ, EV_ADD )){
#elif __linux__
            // AcceptNewClient
            if(!ControlEpoll(client_context_ptr, EPOLLIN, EPOLL_CTL_ADD)){
#endif
                is_server_running_ = false;
                return false;
            }
            // 202501 fix 
            if(cb_on_client_connected_!=nullptr) {
                cb_on_client_connected_(client_context_ptr);
            } else {
                OnClientConnected(client_context_ptr);
            }
        }//while : accept
        return true;
    }

    //-------------------------------------------------------------------------
    void DoSendPendingFailed(Context* ctx_ptr) override {
        // 서버는 계속 동작한다
        TerminateClient(ctx_ptr);
    }

    //-------------------------------------------------------------------------
    void TerminateClient(Context* client_ctx, bool is_graceful=true) {
        client_cnt_--;
        // DBG_LOG(" socket : "<< client_ctx->socket 
        //  <<", connection closing ,graceful=" << (is_graceful ? "TRUE" : "FALSE"));

        if (!is_graceful) {
            // force the subsequent closesocket to be abortative.
            struct linger linger_struct ;
            linger_struct.l_onoff = 1;
            linger_struct.l_linger = 0;
            setsockopt(client_ctx->socket, SOL_SOCKET,
                  SO_LINGER, (char*)&linger_struct,
                   sizeof(linger_struct));
        }
#ifdef __APPLE__
        ControlKq(client_ctx, EVFILT_READ, EV_DELETE );
#elif __linux__
        // TerminateClient
        ControlEpoll(client_ctx,   EPOLLIN , EPOLL_CTL_DEL ); //just in case
#endif
        close(client_ctx->socket);
        if (cb_on_client_disconnected_ != nullptr) {
            cb_on_client_disconnected_(client_ctx);
        } else {
            OnClientDisconnected(client_ctx);
        }
        PushClientContextToCache(client_ctx);
    }

    //-------------------------------------------------------------------------
    void ServerThreadRoutine() {
#ifdef __APPLE__
        struct timespec ts;
        ts.tv_sec  =1;
        ts.tv_nsec =0;
#endif
        while(is_need_server_run_) {
#ifdef __APPLE__
            int event_cnt = kevent(kq_fd_, NULL, 0, 
                                   kq_events_ptr_, max_event_,
                                   &ts);
            if (event_cnt < 0) {
                err_msg_ = "kevent error [" + std::string(strerror(errno))+"]";
                is_server_running_ = false;
                return;
            }
#elif __linux__
            int event_cnt = epoll_wait(ep_fd_,
                                     ep_events_,
                                  max_event_,
                                    100 );
            if (event_cnt < 0) {
                err_msg_ = "epoll wait error [" 
                     + std::string(strerror(errno)) + "]";
                is_server_running_ = false;
                return;
            }
#endif
            //DBG_LOG("--->> event count : " << event_cnt);
            for (int i = 0; i < event_cnt; i++) {
#ifdef __APPLE__
                if ((int)kq_events_ptr_[i].ident == listen_socket_) {
#elif __linux__
                if (((Context*)ep_events_[i].data.ptr)->socket == listen_socket_){
#endif
                    if(!AcceptNewClient()) {
                        ELOG(err_msg_);
                        is_server_running_ = false;
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
                        DBG_LOG("close ..");
                        TerminateClient(ctx_ptr); 
                    }
#ifdef __APPLE__
                    else if (EVFILT_READ == kq_events_ptr_[i].filter){
#elif __linux__
                    else if (ep_events_[i].events & EPOLLIN) {
#endif
                        //# recv #---------- ServerThreadRoutine
                        if(! RecvData(ctx_ptr) ) {
                            DBG_LOG("close ..");
                            TerminateClient(ctx_ptr); 
                        }
                    }
#ifdef __APPLE__
                    else if (EVFILT_WRITE == kq_events_ptr_[i].filter ){
#elif __linux__
                    else if (ep_events_[i].events & EPOLLOUT) {
#endif
                        if(!SendPendingData(ctx_ptr)) {
                            is_server_running_ = false;
                            return; //error!
                        }
                    }
                }
            }
        } //while
        DBG_LOG("server thread exit");
        is_server_running_ = false;
    }

    //-------------------------------------------------------------------------
    void ServerThreadUdpRoutine() {
#ifdef __APPLE__
        struct timespec ts;
        ts.tv_sec  =1;
        ts.tv_nsec =0;
#endif
        if (cumbuffer::OP_RSLT_OK != 
            listen_context_ptr_->recv_buffer.Init(max_data_len_) ) {
            err_msg_  = "cumBuffer Init error : " +
                    std::string(listen_context_ptr_->recv_buffer.GetErrMsg());
            is_server_running_ = false;
            return ;
        }
        while(is_need_server_run_) {
#ifdef __APPLE__
            int event_cnt = kevent(kq_fd_, NULL, 0, kq_events_ptr_, max_event_, &ts);
            if (event_cnt < 0) {
                err_msg_ = "kevent error ["  + std::string(strerror(errno)) + "]";
                is_server_running_ = false;
                return;
            }
#elif __linux__
            int event_cnt = epoll_wait(ep_fd_,
                                     ep_events_,
                                  max_event_,
                                    100 );
            if (event_cnt < 0) {
                err_msg_ = "epoll wait error [" 
                     + std::string(strerror(errno)) + "]";
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
                    if(! RecvfromData(listen_context_ptr_)) {
                        exit(EXIT_FAILURE);
                    }
                }
#ifdef __APPLE__
                else if (EVFILT_WRITE == kq_events_ptr_[i].filter ){
#elif __linux__
                else if (ep_events_[i].events & EPOLLOUT) {
#endif
                    if(!SendPendingData(listen_context_ptr_)) {
                        is_server_running_ = false;
                        return; //error!
                    }
                }
            }
        }//while
        is_server_running_ = false;
    }

protected :
    size_t      max_event_ {0};
    std::string server_ip_ ;
    std::string server_ipc_socket_path_ ="";
    int         server_port_ {-1};

    //-------------------------------------------------------------------------
    bool RunServer() {
#ifdef __APPLE__
        if( kq_events_ptr_) {
#elif __linux__
        if(ep_events_) {
#endif
            err_msg_ = "error [server is already running]";
            return false;
        }
        if( sock_usage_ == SOCK_USAGE_IPC_SERVER ) {
            listen_socket_ = socket(AF_UNIX,SOCK_STREAM,0) ;
        } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER ) {
            listen_socket_ = socket(AF_INET,SOCK_STREAM,0) ;
        } else if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            listen_socket_ = socket(AF_INET,SOCK_DGRAM,0) ;
        }
        if( listen_socket_ < 0 ) {
            err_msg_ = "init error [" + std::string(strerror(errno)) + "]";
            return false;
        }   
        if(!SetSocketNonBlocking (listen_socket_)) {
            return  false;
        }
        int opt_on=1;
        int result = -1;
        if (setsockopt(listen_socket_,SOL_SOCKET,
                  SO_REUSEADDR,&opt_on,sizeof(opt_on))==-1) {
            err_msg_ = "setsockopt SO_REUSEADDR error ["  +
                        std::string(strerror(errno)) + "]";
            return false;
        }
        if (setsockopt(listen_socket_,SOL_SOCKET,
                  SO_KEEPALIVE, &opt_on, sizeof(opt_on))==-1) {
            err_msg_ = "setsockopt SO_KEEPALIVE error ["  +
                        std::string(strerror(errno)) + "]";
            return false;
        }
        if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            if(!SetSockoptSndRcvBufUdp(listen_socket_)) {
                return false;
            }
        }

        if ( sock_usage_ == SOCK_USAGE_IPC_SERVER ) {
            SOCKADDR_UN ipc_server_addr ;
            memset((void *)&ipc_server_addr,0x00,sizeof(ipc_server_addr)) ;
            ipc_server_addr.sun_family = AF_UNIX;
            snprintf(ipc_server_addr.sun_path, sizeof(ipc_server_addr.sun_path),
                    "%s",server_ipc_socket_path_.c_str());

            result = bind(listen_socket_,
                        (SOCKADDR*)&ipc_server_addr,
                         sizeof(ipc_server_addr)) ;

        } else if (sock_usage_ == SOCK_USAGE_TCP_SERVER || 
                   sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            SOCKADDR_IN    server_addr  ;
            memset((void *)&server_addr,0x00,sizeof(server_addr)) ;
            server_addr.sin_family      = AF_INET ;
            server_addr.sin_addr.s_addr = inet_addr(server_ip_.c_str()) ;
            server_addr.sin_port = htons(server_port_);

            result = bind(listen_socket_,
                        (SOCKADDR*)&server_addr,
                         sizeof(server_addr)) ;
        }

        if ( result < 0 ) {
            if ( sock_usage_ == SOCK_USAGE_IPC_SERVER ) {
                ELOG(server_ipc_socket_path_.c_str());
            }
            err_msg_ = "bind error ["+ std::string(strerror(errno))+"]";
            return false ;
        }
        if (sock_usage_ == SOCK_USAGE_IPC_SERVER || 
            sock_usage_ == SOCK_USAGE_TCP_SERVER ) {
            result = listen(listen_socket_,SOMAXCONN) ;
            if ( result < 0 ) {
                err_msg_ = "listen error ["+std::string(strerror(errno))+"]";
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
            err_msg_ = "Context alloc failed !";
            return false;
        }
        listen_context_ptr_->socket = listen_socket_;
#endif
#ifdef __APPLE__
        kq_fd_ = kqueue();
        if (kq_fd_ == -1) {
            err_msg_ = "kqueue error ["  + std::string(strerror(errno)) + "]";
            return false;
        }
        if(!ControlKq(listen_context_ptr_, EVFILT_READ, EV_ADD )) {
            return false;
        }
#elif __linux__
        ep_fd_ = epoll_create1(0);
        if (ep_fd_ == -1) {
            err_msg_ = "epoll create error ["+ std::string(strerror(errno))+"]";
            return false;
        }
        // RunServer
        if(!ControlEpoll (listen_context_ptr_, EPOLLIN , EPOLL_CTL_ADD)){
            return false;
        }
#endif
        is_need_server_run_ = true;
        is_server_running_  = true;
#ifdef __APPLE__
        kq_events_ptr_ = new struct kevent[max_event_];
        memset(kq_events_ptr_, 0x00, sizeof(struct kevent) * max_event_);
#elif __linux__
        ep_events_ = new struct epoll_event[max_event_];
        memset(ep_events_, 0x00, sizeof(struct epoll_event) * max_event_);
#endif
        if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            std::thread server_thread(&ASockServerBase::ServerThreadUdpRoutine, this);
            server_thread.detach();
        } else {
            std::thread server_thread(&ASockServerBase::ServerThreadRoutine, this);
            server_thread.detach();
        }
        return true;
    }
};
} //namespace
#endif
