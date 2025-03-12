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

#ifndef ASOCKNIXBASE_HPP
#define ASOCKNIXBASE_HPP

#include <atomic>
#include <queue>
#include <cassert>
#include <cstdlib>
#include <string>
#include <functional>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <unordered_map>
#include "asock_comm.hpp"

////////////////////////////////////////////////////////////////////////////////
namespace asock {

class ASockNixBase {
public :
    virtual ~ASockNixBase() {};

    virtual void SetUsage()=0;
    //-------------------------------------------------------------------------
    bool SetSocketNonBlocking(int sock_fd) {
        int oldflags  ;
        if ((oldflags = fcntl( sock_fd,F_GETFL, 0)) < 0 ) {
            err_msg_ = "fcntl F_GETFL error [" + std::string(strerror(errno))+ "]";
            return  false;
        }
        int ret  = fcntl( sock_fd,F_SETFL,oldflags | O_NONBLOCK) ;
        if ( ret < 0 ) {
            err_msg_ = "fcntl O_NONBLOCK error [" + std::string(strerror(errno))+ "]";
            return  false;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    bool SendData(Context* ctx_ptr, const char* data_ptr, size_t len) {
        std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock);
        size_t total_sent = 0;
        size_t total_len = len + HEADER_SIZE;

        ST_HEADER header;
        snprintf(header.msg_len, sizeof(header.msg_len),"%d", (int)len );

        //if sent is pending, just push to queue. 
        if(ctx_ptr->is_sent_pending){  
            PENDING_SENT pending_sent;
            pending_sent.pending_sent_data = new char[total_len];
            pending_sent.pending_sent_len  = total_len;
            memcpy(pending_sent.pending_sent_data, (char*)&header, HEADER_SIZE);
            memcpy(pending_sent.pending_sent_data+HEADER_SIZE, data_ptr, len);
            if(sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
                //udp(server) 인 경우엔, 데이터와 client remote addr 정보를 함께 queue에 저장
                memcpy( &pending_sent.udp_remote_addr, 
                        &ctx_ptr->udp_remote_addr, 
                          sizeof(pending_sent.udp_remote_addr));
            }
            ctx_ptr->pending_send_deque.push_back(pending_sent);
#ifdef __APPLE__
            if(!ControlKq(ctx_ptr, EVFILT_WRITE, EV_ADD|EV_ENABLE )) {
#elif __linux__
            // SendData
            if(!ControlEpoll (ctx_ptr, EPOLLIN | EPOLLOUT , EPOLL_CTL_MOD)){
#endif
                delete [] pending_sent.pending_sent_data ;
                ctx_ptr->pending_send_deque.pop_back();
                return false;
            }
            return true;
        }
        // header 를 앞에 추가한다. 
        char* data_buffer = new char[total_len];
        char* data_position_ptr = data_buffer;
        memcpy(data_position_ptr,(char*)&header, HEADER_SIZE);
        memcpy(data_position_ptr+HEADER_SIZE, data_ptr, len);

        while( total_sent < total_len) {
            int sent_len =0;
            size_t remained_len = total_len-total_sent;
            if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
                //UDP : all or nothing. no partial sent, send with header.
                sent_len = sendto(ctx_ptr->socket,  data_position_ptr, remained_len , 0,
                                (struct sockaddr*)& ctx_ptr->udp_remote_addr,
                                sizeof(ctx_ptr->udp_remote_addr)) ;
            } else if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
                //UDP : all or nothing. no partial sent, send with header.
                sent_len = sendto(ctx_ptr->socket,  data_position_ptr, remained_len , 0,
                                0, //client : already set! (via connect)
                                sizeof(ctx_ptr->udp_remote_addr)) ;
            } else {
                sent_len = send(ctx_ptr->socket, data_position_ptr, remained_len, 0);
            }

            if(sent_len > 0) {
                total_sent += sent_len ;
                data_position_ptr += sent_len ;
            } else if( sent_len < 0 ) {
                if ( errno == EWOULDBLOCK || errno == EAGAIN ) {
                    //send later
                    PENDING_SENT pending_sent;
                    pending_sent.pending_sent_data = new char [remained_len]; 
                    pending_sent.pending_sent_len  = remained_len;
                    memcpy(pending_sent.pending_sent_data,data_position_ptr,remained_len);
                    if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
                        //udp(server) 인 경우엔, 데이터와 client remote addr 정보를 함께 queue에 저장
                        memcpy( &pending_sent.udp_remote_addr, 
                                &ctx_ptr->udp_remote_addr, 
                                  sizeof(pending_sent.udp_remote_addr));
                    }
                    ctx_ptr->pending_send_deque.push_back(pending_sent);
#ifdef __APPLE__
                    if(!ControlKq(ctx_ptr, EVFILT_WRITE, EV_ADD|EV_ENABLE )){
#elif __linux__
                    // SendData
                    if(!ControlEpoll (ctx_ptr, EPOLLIN | EPOLLOUT, EPOLL_CTL_MOD)){
#endif
                        delete [] pending_sent.pending_sent_data;
                        ctx_ptr->pending_send_deque.pop_back();
                        delete [] data_buffer;
                        return false;
                    }
                    ctx_ptr->is_sent_pending = true;
                    delete [] data_buffer;
                    return true;
                } else if ( errno != EINTR ) {
                    // err_msg_ need lock for multithread
                    err_msg_ = "send error [" + std::string(strerror(errno)) + "]";
                    ELOG(err_msg_);
                    delete [] data_buffer;
                    return false;
                }
            }
        }//while
        delete [] data_buffer;
        return true;
    } 

    //-------------------------------------------------------------------------
    bool SetBufferCapacity(size_t max_data_len) {
        if(max_data_len<=0) {
            err_msg_ = " length is invalid";
            return false;
        }
        max_data_len_ = max_data_len;
        return true;
    }

    //-----------------------------------------------------
    std::string GetLastErrMsg(){
        return err_msg_;
    }

    //-------------------------------------------------------------------------
    bool SetCbOnRecvedCompletePacket(std::function<bool(Context*,const char* const,size_t)> cb ) {
        //for composition usage
        if(cb != nullptr) {
            cb_on_recved_complete_packet_ = cb;
        } else {

            return false;
        }
        return true;
    }

private:
    //size_t            max_event_  {0};
    std::queue<Context*> queue_ctx_cache_;
    std::mutex           ctx_cache_lock_ ;
    std::function<bool(Context*,const char* const,size_t)> cb_on_recved_complete_packet_{nullptr};
    virtual void DoSendPendingFailed(Context* ctx_ptr) = 0 ;

    size_t  OnCalculateDataLen(Context* ctx_ptr) {
        // DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
        //  " / capacity : " << ctx_ptr->recv_buffer.GetCapacity() <<
        //  " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace() <<
        //  " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace() <<
        //  " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() );
 
        if( ctx_ptr->recv_buffer.GetCumulatedLen() < (int)HEADER_SIZE ) {
            DBG_LOG("more to come");
            return asock::MORE_TO_COME ; //more to come
        }
        ST_HEADER header ;
        ctx_ptr->recv_buffer.PeekData(HEADER_SIZE, (char*)&header);
        size_t supposed_total_len = std::atoi(header.msg_len) + HEADER_SIZE;
        if(supposed_total_len > ctx_ptr->recv_buffer.GetCapacity()) {
            // DBG_RED_LOG( "(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
            //  " / packet length : " << supposed_total_len <<
            //  " / buffer is insufficient => increase buffer");

            // If the size of the data to be received is larger than the buffer,
            // increase the buffer capacity.
            size_t new_buffer_len= supposed_total_len * 2;
            if(!ctx_ptr->recv_buffer.IncreaseBufferAndCopyExisting(new_buffer_len)) {
                ELOG(ctx_ptr->recv_buffer.GetErrMsg());
                exit(EXIT_FAILURE);
            }
            SetBufferCapacity(new_buffer_len);

            // DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
            //  " / capacity : " << ctx_ptr->recv_buffer.GetCapacity() <<
            //  " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace() <<
            //  " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace() <<
            //  " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() );
        }
        // DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  
        //   << " / packet length : " << supposed_total_len );
        return supposed_total_len ;
    }

    virtual bool OnRecvedCompleteData(Context* , const char* const , size_t ) {
        ELOG("OnRecvedCompleteData not implemented");
        return false;
    }

protected :
    std::unordered_map<SOCKET_T, Context*> map_sock_ctx_;
    ENUM_SOCK_USAGE sock_usage_ {asock::SOCK_USAGE_UNKNOWN};
    size_t  max_data_len_ {0};
    std::string  err_msg_ ;
    int         connect_timeout_secs_    ;
    bool        is_buffer_init_ {false};
    std::atomic<bool> is_server_running_  {false};

#ifdef __APPLE__
    struct  kevent* kq_events_ptr_ {nullptr};
    int     kq_fd_ {-1};
#elif __linux__
    struct  epoll_event* ep_events_{nullptr};
    int     ep_fd_ {-1};
#endif

    //-------------------------------------------------------------------------
    //udp server, client
    bool SetSockoptSndRcvBufUdp(SOCKET_T socket){
        size_t opt_cur ; 
        int opt_val=max_data_len_ ; 
        int opt_len = sizeof(opt_cur) ;
        if (getsockopt(socket,SOL_SOCKET,SO_SNDBUF,
                   &opt_cur, (SOCKLEN_T *) &opt_len)==-1) {
            err_msg_ = "gsetsockopt SO_SNDBUF error ["  +
                        std::string(strerror(errno)) + "]";
            return false;
        }
        if(max_data_len_ > opt_cur ) {
            if (setsockopt(socket,SOL_SOCKET,SO_SNDBUF,
                       (char*)&opt_val, sizeof(opt_val))==-1) {
                err_msg_ = "setsockopt SO_SNDBUF error ["  +
                            std::string(strerror(errno)) + "]";
                return false;
            }
        }
        if (getsockopt(socket,SOL_SOCKET,SO_RCVBUF,
                   &opt_cur, (SOCKLEN_T *)&opt_len)==-1) {
            err_msg_ = "setsockopt SO_RCVBUF error ["  + 
                        std::string(strerror(errno)) + "]";
            return false;
        }
        if(max_data_len_ > opt_cur ) {
            if (setsockopt(socket,SOL_SOCKET,SO_RCVBUF,
                       (char*)&opt_val, sizeof(opt_val))==-1) {
                err_msg_ = "setsockopt SO_RCVBUF error ["  + 
                            std::string(strerror(errno)) + "]";
                return false;
            }
        }
        return true;
    }

    //-------------------------------------------------------------------------
    bool SendPendingData(Context* ctx_ptr) {
        std::lock_guard<std::mutex> guard(ctx_ptr->ctx_lock);
        //LOG("lock !!\n");
        while(!ctx_ptr->pending_send_deque.empty()) {
            //LOG("pending send");
            PENDING_SENT pending_sent = ctx_ptr->pending_send_deque.front();
            int sent_len = 0;
            if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
                sent_len = sendto(ctx_ptr->socket,  pending_sent.pending_sent_data,
                                  pending_sent.pending_sent_len, 
                                  0, (struct sockaddr*)& pending_sent.udp_remote_addr,
                                  sizeof(pending_sent.udp_remote_addr)) ;
            } else if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
                // UDP : all or nothing . no partial sent!
                sent_len = sendto(ctx_ptr->socket,  pending_sent.pending_sent_data,
                                  pending_sent.pending_sent_len, 
                                  0, 0, // client : already set! (via connect)
                                  sizeof(pending_sent.udp_remote_addr)) ;
            } else {
                sent_len = send(ctx_ptr->socket, pending_sent.pending_sent_data,
                                pending_sent.pending_sent_len, 0) ;
            }
            if( sent_len > 0 ) {
                if(sent_len == (int)pending_sent.pending_sent_len) {
                    //LOG("all pending sent :" + std::to_string(pending_sent.pending_sent_len));
                    delete [] pending_sent.pending_sent_data;
                    ctx_ptr->pending_send_deque.pop_front();
                    if(ctx_ptr->pending_send_deque.empty()) {
                        //sent all data
                        ctx_ptr->is_sent_pending = false; 
#ifdef __APPLE__
                        if(!ControlKq(ctx_ptr, EVFILT_WRITE, EV_DELETE ) ||
                           !ControlKq(ctx_ptr, EVFILT_READ, EV_ADD)){
#elif __linux__
                        // SendPendingData
                        if(!ControlEpoll (ctx_ptr,EPOLLIN,EPOLL_CTL_MOD)){
#endif
                            //error : server, client 모두 종료됨
                            DoSendPendingFailed(ctx_ptr); //virtual
                            if ( sock_usage_ == SOCK_USAGE_TCP_SERVER ||
                                 sock_usage_ == SOCK_USAGE_IPC_SERVER  ) {
                                is_server_running_ = false; //서버가 종료됨
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
                    memcpy(partial_pending_sent.pending_sent_data, 
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
                    //error : client 만 종료
                    err_msg_ = "send error ["  + std::string(strerror(errno)) + "]";
                    DoSendPendingFailed(ctx_ptr); //virtual
                    return false;
                }
            }
        } //while
        return true;
    }

    //-------------------------------------------------------------------------
    bool RecvData(Context* ctx_ptr){

        // DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
        //     " / capacity : " << ctx_ptr->recv_buffer.GetCapacity()  <<
        //     " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace()  <<
        //     " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace() <<
        //     " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() );

        size_t free_linear_len = ctx_ptr->recv_buffer.GetLinearFreeSpace();
        assert(free_linear_len != 0);
        int recved_len = recv( ctx_ptr->socket,
                              ctx_ptr->recv_buffer.GetLinearAppendPtr(),
                                free_linear_len, 0);
        if( recved_len > 0) {
            // DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket
            //    << " / want to recv : " << free_linear_len
            //    << " / actual recved :" << recved_len );

            ctx_ptr->recv_buffer.IncreaseData(recved_len);
            while(ctx_ptr->recv_buffer.GetCumulatedLen()) {
                //invoke user specific implementation
                if(!ctx_ptr->is_packet_len_calculated ) {
                    //only when calculation is necessary
                    ctx_ptr->complete_packet_len = OnCalculateDataLen( ctx_ptr );
                    ctx_ptr->is_packet_len_calculated = true;
                }
                if(ctx_ptr->complete_packet_len == asock::MORE_TO_COME) {
                    ctx_ptr->is_packet_len_calculated = false;
                    return true; //need to recv more
                } else if(ctx_ptr->complete_packet_len > ctx_ptr->recv_buffer.GetCumulatedLen()) {
                    // 이미 recv 한 이후 임. 만약 OnCalculateDataLen 에서 버퍼 증가 재할당이 발생했다면,
                    // 한번더 recv 가 수행될것이다.
                    // DBG_GREEN_LOG("(usg:" << this->sock_usage_ << ")sock:"
                    //     << ctx_ptr->socket << " / need to recv more, buffer cumulated : "
                    //     << ctx_ptr->recv_buffer.GetCumulatedLen() ) ;
                    return true; //need to recv more
                } else {
                    //got complete packet 
                    char* complete_packet_data = new (std::nothrow) char [ctx_ptr->complete_packet_len] ;
                    if(complete_packet_data == NULL) {
                        err_msg_ = "memory alloc failed!";
                        return false;
                    }
                    if(cumbuffer::OP_RSLT_OK!= ctx_ptr->recv_buffer.GetData(
                        ctx_ptr->complete_packet_len, complete_packet_data )) {
                        //error !
                        err_msg_ = ctx_ptr->recv_buffer.GetErrMsg();
                        ctx_ptr->is_packet_len_calculated = false;
                        delete[] complete_packet_data;
                        return false;
                    }

                    if(cb_on_recved_complete_packet_!=nullptr) {
                        //invoke user specific callback
                        cb_on_recved_complete_packet_ (ctx_ptr,
                                  complete_packet_data + HEADER_SIZE ,
                                  ctx_ptr->complete_packet_len - HEADER_SIZE );
                    } else {
                        //invoke user specific implementation
                        OnRecvedCompleteData(ctx_ptr,
                                   complete_packet_data + HEADER_SIZE , 
                                   ctx_ptr->complete_packet_len - HEADER_SIZE );
                    }
                    // DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket
                    //    << " / capacity : " << ctx_ptr->recv_buffer.GetCapacity()
                    //    << " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace()
                    //    << " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace()
                    //    << " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() );

                    ctx_ptr->is_packet_len_calculated = false;
                    delete[] complete_packet_data;
                }
            } //while
        } else if( recved_len == 0 ) {
            err_msg_ = "recv 0, client disconnected , fd:"
                    + std::to_string(ctx_ptr->socket);
            DBG_LOG(err_msg_);
            return false ;
        }
        return true ;
    }

    //-------------------------------------------------------------------------
    bool RecvfromData(Context* ctx_ptr) {

        // udp 인 경우에는 미리 최대 수신가능한 크기를 알아서 버퍼를 할당해야함.
        // In case of UDP, you need to know the maximum receivable size in advance and allocate a buffer.

        SOCKLEN_T addrlen = sizeof(ctx_ptr->udp_remote_addr);
        int recved_len = recvfrom(ctx_ptr->socket, //--> is listen_socket_
                                ctx_ptr->recv_buffer.GetLinearAppendPtr(),
                                max_data_len_ ,
                                0,
                                (struct sockaddr *)&ctx_ptr->udp_remote_addr,
                                &addrlen ); 
        if( recved_len > 0) {
            // DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket
            //     << " / actual recved :" << recved_len );

            ctx_ptr->recv_buffer.IncreaseData(recved_len);
            //udp got complete packet
            char* complete_packet_data = new (std::nothrow) char [max_data_len_] ; // TODO no..not good
            if(complete_packet_data == NULL) {
                err_msg_ = "memory alloc failed!";
                ELOG(err_msg_);
                exit(EXIT_FAILURE);
            }
            if(cumbuffer::OP_RSLT_OK!= 
            ctx_ptr->recv_buffer.GetData(recved_len, complete_packet_data )) {
                //error !
                err_msg_ = ctx_ptr->recv_buffer.GetErrMsg();
                ELOG(err_msg_);
                ctx_ptr->is_packet_len_calculated = false;
                delete[] complete_packet_data; 
                return false;
            }
            // UDP 이므로 받는 버퍼를 초기화해서, linear free space를 초기화 상태로 
            ctx_ptr->recv_buffer.ReSet(); //this is udp. all data has arrived!

            if(cb_on_recved_complete_packet_!=nullptr) {
                //invoke user specific callback
                cb_on_recved_complete_packet_ (ctx_ptr, complete_packet_data + HEADER_SIZE ,
                                               recved_len - HEADER_SIZE);
            } else {
                //invoke user specific implementation
                OnRecvedCompleteData(ctx_ptr,complete_packet_data + HEADER_SIZE ,
                             recved_len - HEADER_SIZE);
            }
            delete[] complete_packet_data;

            // DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket
            //     << " / capacity : " << ctx_ptr->recv_buffer.GetCapacity()
            //     << " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace()
            //     << " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace()
            //     << " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen());
        }
        return true;
    } //udp

#ifdef __APPLE__
    bool ControlKq(Context* ctx_ptr,uint32_t events,uint32_t fflags) {
        struct  kevent kq_event;
        memset(&kq_event, 0, sizeof(struct kevent));
        EV_SET(&kq_event, ctx_ptr->socket, events,fflags , 0, 0, ctx_ptr); 
        //udata = ctx_ptr

        int result = kevent(kq_fd_, &kq_event, 1, NULL, 0, NULL);
        if (result == -1) {
            err_msg_ = "kevent error [" + std::string(strerror(errno)) + "]";
            return false; 
        }
        return true;
        //man:Re-adding an existing event will modify the parameters of the
        //    original event, and not result in a duplicate entry.
    }
#elif __linux__
    bool ControlEpoll(Context* ctx_ptr , uint32_t events, int op) {
        struct  epoll_event ev_client{};
        ev_client.data.fd    = ctx_ptr->socket;
        ev_client.events     = events ;
        ev_client.data.ptr   = ctx_ptr;

        if(epoll_ctl(ep_fd_, op, ctx_ptr->socket, &ev_client)<0) {
            err_msg_ = "socket:" + std::to_string(ctx_ptr->socket) 
                    + " / error [" + std::string(strerror(errno)) + "]";
            return false; 
        }
        return true;
    }
#endif

    //-------------------------------------------------------------------------
    Context* PopClientContextFromCache() {
        Context* ctx_ptr = nullptr;
        {//lock scope
            std::lock_guard<std::mutex> lock(ctx_cache_lock_);
            if (!queue_ctx_cache_.empty()) {
                ctx_ptr = queue_ctx_cache_.front();
                queue_ctx_cache_.pop();
                return ctx_ptr;
            }
        }
        ctx_ptr = new (std::nothrow) Context();
        if(!ctx_ptr) {
            err_msg_ = "Context alloc failed !";
            return nullptr;
        }
        if ( cumbuffer::OP_RSLT_OK != ctx_ptr->recv_buffer.Init(max_data_len_) ) {
            err_msg_  = std::string("cumBuffer Init error : ") + 
                std::string(ctx_ptr->recv_buffer.GetErrMsg());
            return nullptr;
        }
        return ctx_ptr ;
    }
    //-------------------------------------------------------------------------
    void PushClientContextToCache(Context* ctx_ptr) {
        map_sock_ctx_.erase(ctx_ptr->socket);

        //reset
        ctx_ptr->recv_buffer.ReSet();
        ctx_ptr->socket = -1;
        ctx_ptr->is_packet_len_calculated = false;
        ctx_ptr->is_sent_pending = false;
        ctx_ptr->is_connected = false;
        ctx_ptr->complete_packet_len = 0;
        while(!ctx_ptr->pending_send_deque.empty() ) {
            PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
            delete [] pending_sent.pending_sent_data;
            ctx_ptr->pending_send_deque.pop_front();
        }
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        queue_ctx_cache_.push(ctx_ptr);
    }

    //-------------------------------------------------------------------------
    void ClearClientCache() {
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        while(!queue_ctx_cache_.empty() ) {
            Context* ctx_ptr = queue_ctx_cache_.front();
            while(!ctx_ptr->pending_send_deque.empty() ) {
                PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
                delete [] pending_sent.pending_sent_data;
                ctx_ptr->pending_send_deque.pop_front();
            }
            delete ctx_ptr;
            queue_ctx_cache_.pop();
        }
        // server 가 먼저 종료하는 경우, queue_ctx_cache_ 에 없을수 있다
        for (auto& p : map_sock_ctx_){
            Context* ctx_ptr = p.second;
            while(!ctx_ptr->pending_send_deque.empty() ) {
                PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
                delete [] pending_sent.pending_sent_data;
                ctx_ptr->pending_send_deque.pop_front();
            }
            delete ctx_ptr;
        }
    }
};
} //namespace
#endif
