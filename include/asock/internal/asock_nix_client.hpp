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

#ifndef ASOCKNIXCLIENT_HPP
#define ASOCKNIXCLIENT_HPP


#include <atomic>
#include <cassert>
#include <cstdlib>
#include <string>
#include <thread>
#include <functional>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include "asock_nix_base.hpp"

////////////////////////////////////////////////////////////////////////////////
namespace asock {

class ASockClientBase : public ASockNixBase{
public :
    virtual ~ASockClientBase() {
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
    }

    //-------------------------------------------------------------------------
    //for composition : Assign yours to these callbacks 
    bool SetCbOnDisconnectedFromServer(std::function<void()> cb) {
        //for composition usage 
        if(cb != nullptr) {
            cb_on_disconnected_from_server_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    bool SendToServer (const char* data, size_t len) {
        if ( !is_connected_ ) {
            err_msg_ = "not connected";
            return false;
        }
        return SendData(&client_ctx_, data, len);
    }

    //-------------------------------------------------------------------------
    SOCKET_T  GetSocket () {
        return  client_ctx_.socket ; 
    }

    //-------------------------------------------------------------------------
    bool IsConnected() {
        if (is_connected_ && is_client_thread_running_) {
            return true;
        }
        return false;
    }

    //-------------------------------------------------------------------------
    // Disconnects from the server and waits for the client thread to terminate.
    void Disconnect() {
        if(client_ctx_.socket > 0 ) {
            close(client_ctx_.socket);
        }
        client_ctx_.socket = -1;
        is_connected_ = false;

        // TODO: fix this. 아래 로직 개선할것.
        // server 연결종료시 호출된 콜백내부에서 Disconnect 를 호출하는 경우 고려,
        // 구분처리한다.(무한loop방지)
        // --> is_client_thread_running_ :
        // 콜백이 호출된 이후, client thread loop 가 종료되어야 false로 설정되므로
        if(!is_server_disconnted_cb_invoked_){
            //wait thread exit
            while (is_client_thread_running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        // 콜백내에서 Disconnect 를 호출한 경우에만, main 종료시 대기(sleep)하는것이 필요함.
        // --> thread 완전히 종료하는것을 대기하는것임.
    }

private :
    std::function<void()> cb_on_disconnected_from_server_ {nullptr} ;
    //for inheritance : Implement these virtual functions.
    virtual void  OnDisconnectedFromServer() {}
    //-------------------------------------------------------------------------
    bool RunClientThread(){
        if(!is_client_thread_running_ ) {
#ifdef __APPLE__
            kq_events_ptr_ = new struct kevent;
            memset(kq_events_ptr_, 0x00, sizeof(struct kevent) );
            kq_fd_ = kqueue();
            if (kq_fd_ == -1) {
                err_msg_ = "kqueue error ["  + std::string(strerror(errno)) + "]";
                close(client_ctx_.socket);
                return false;
            }
#elif __linux__
            ep_events_ = new struct epoll_event;
            memset(ep_events_, 0x00, sizeof(struct epoll_event) );
            ep_fd_ = epoll_create1(0);
            if ( ep_fd_== -1) {
                err_msg_ = "epoll create error ["  + std::string(strerror(errno)) + "]";
                close(client_ctx_.socket);
                return false;
            }
#endif

#ifdef __APPLE__
            if(!ControlKq(&client_ctx_, EVFILT_READ, EV_ADD )) {
                close(client_ctx_.socket);
                return false;
            }
#elif __linux__
            // RunClientThread
            if(!ControlEpoll( &client_ctx_, EPOLLIN  , EPOLL_CTL_ADD )) {
                close(client_ctx_.socket);
                return false;
            }
#endif
            std::thread client_thread(&ASockClientBase::ClientThreadRoutine, this);
            client_thread.detach();
            is_client_thread_running_ = true;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    void ClientThreadRoutine(){
        while(is_connected_) {
#ifdef __APPLE__
            struct timespec ts;
            ts.tv_sec  =0;
            ts.tv_nsec =10000000;
            int event_cnt = kevent(kq_fd_, NULL, 0, 
                                   kq_events_ptr_, 1, &ts); 
#elif __linux__
            int event_cnt = epoll_wait(ep_fd_, ep_events_, 1, 10 );
#endif
            if (event_cnt < 0) {
#ifdef __APPLE__
                err_msg_ = "kevent error ["  + std::string(strerror(errno)) + "]";
#elif __linux__
                err_msg_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
#endif
                ELOG(err_msg_);
                is_client_thread_running_ = false;
                return;
            }
            //############## close ###########################
#ifdef __APPLE__
            if (kq_events_ptr_->flags & EV_EOF){
#elif __linux__
            if (ep_events_->events & EPOLLRDHUP || ep_events_->events & EPOLLERR) {
#endif
                close( client_ctx_.socket);
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
                    if(! RecvfromData(&client_ctx_) ) {
                        close( client_ctx_.socket);
                        InvokeServerDisconnectedHandler();
                        break;
                    }
                } else {
                    // ClientThreadRoutine
                    if(! RecvData(&client_ctx_) ) {
                        close( client_ctx_.socket);
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
                if(!SendPendingData(&client_ctx_)) {
                    return; //error!
                }
            }//send
        } //while
        is_client_thread_running_ = false;
        DBG_LOG("client thread exited");
    }

    //-------------------------------------------------------------------------
    void InvokeServerDisconnectedHandler() {
        is_server_disconnted_cb_invoked_=true;
        if(cb_on_disconnected_from_server_!=nullptr) {
            cb_on_disconnected_from_server_();
        } else {
            OnDisconnectedFromServer();
        }
    }

protected :
    std::string server_ipc_socket_path_ ="";

    void DoSendPendingFailed(Context* ctx_ptr) override {
        // client 종료
        close( ctx_ptr->socket);
        InvokeServerDisconnectedHandler();
        is_client_thread_running_ = false;
    }
    std::atomic<bool> is_connected_ {false};
    Context     client_ctx_;
    SOCKADDR_IN tcp_server_addr_ ;
    SOCKADDR_IN udp_server_addr_ ;
    SOCKADDR_UN ipc_conn_addr_   ;
    std::atomic<bool> is_client_thread_running_ {false};
    bool is_server_disconnted_cb_invoked_ {false};

    //-------------------------------------------------------------------------
    bool ConnectToServer() {
        if(is_connected_ ){
            return true;
        }
        if( client_ctx_.socket < 0 ) {
            err_msg_ = "error : server socket is invalid" ;
            return false;
        }   
        if(!SetSocketNonBlocking (client_ctx_.socket)) {
            close(client_ctx_.socket);
            return  false;
        }
        if(!is_buffer_init_ ) {
            if(cumbuffer::OP_RSLT_OK != client_ctx_.recv_buffer.Init(max_data_len_) ) {
                err_msg_ = std::string("cumBuffer Init error :") + 
                           std::string(client_ctx_.recv_buffer.GetErrMsg());
                close(client_ctx_.socket);
                return false;
            }
            is_buffer_init_ = true;
        } else {
            //in case of reconnect
            client_ctx_.recv_buffer.ReSet();
        }
        struct timeval timeoutVal;
        timeoutVal.tv_sec  = connect_timeout_secs_ ;
        timeoutVal.tv_usec = 0;
        int result = -1;
        if ( sock_usage_ == SOCK_USAGE_IPC_CLIENT ) {
            result = connect(client_ctx_.socket,(SOCKADDR*)&ipc_conn_addr_,
                             (SOCKLEN_T)sizeof(SOCKADDR_UN)) ; 
        } else if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT ) {
            result = connect(client_ctx_.socket,(SOCKADDR*)&tcp_server_addr_,
                             (SOCKLEN_T)sizeof(SOCKADDR_IN)) ;
        } else if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
            if(!SetSockoptSndRcvBufUdp(client_ctx_.socket)) {
                close(client_ctx_.socket);
                return false;
            }
            result = connect(client_ctx_.socket,(SOCKADDR*)&udp_server_addr_,
                             (SOCKLEN_T)sizeof(SOCKADDR_IN)) ;
        } else {
            err_msg_ = "invalid socket usage" ;
            close(client_ctx_.socket);
            return false;
        }
        if ( result < 0) {
            if (errno != EINPROGRESS) {
                err_msg_ = "connect error [" + std::string(strerror(errno))+ "]";
                close(client_ctx_.socket);
                return false;
            }
        }
        if (result == 0) {
            is_connected_ = true;
            return RunClientThread();
        }
        fd_set   rset, wset;
        FD_ZERO(&rset);
        FD_SET(client_ctx_.socket, &rset);
        wset = rset;
        result = select(client_ctx_.socket+1, &rset, &wset, NULL, &timeoutVal ) ;
        if (result == 0 ) {
            err_msg_ = "connect timeout";
            close(client_ctx_.socket);
            return false;
        } else if (result< 0) {
            err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
            close(client_ctx_.socket);
            return false;
        }
        if (FD_ISSET(client_ctx_.socket, &rset) || FD_ISSET(client_ctx_.socket, &wset)) {
            int  socketerror = 0;
            SOCKLEN_T  len = sizeof(socketerror);
            if (getsockopt(client_ctx_.socket, SOL_SOCKET, SO_ERROR, &socketerror, &len) < 0) {
                err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
                close(client_ctx_.socket);
                return false;
            }
            if (socketerror) {
                err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
                close(client_ctx_.socket);
                return false;
            }
        } else {
            err_msg_ = "connect error : fd not set ";
            ELOG(err_msg_);
            close(client_ctx_.socket);
            return false;
        }
        is_connected_ = true;
        return RunClientThread();
    }
};
} //namespace
#endif
