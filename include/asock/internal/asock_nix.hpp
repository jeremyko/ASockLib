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

#ifndef ASOCKNIX_HPP
#define ASOCKNIX_HPP

#include "asock_comm.hpp"
#include "asock_buffer.hpp"

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <string>
#include <thread>
#include <queue>
#include <deque>
#include <mutex>
#include <functional>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <unordered_map>

#ifdef __APPLE__
#include <sys/select.h>
#include <sys/types.h>
#include <sys/event.h>
#endif

#if __linux__
#include <sys/epoll.h>
#endif


typedef struct  sockaddr_un SOCKADDR_UN ;
typedef         socklen_t   SOCKLEN_T ;

namespace asock {

typedef struct _PENDING_SENT_ {
    char*       pending_sent_data ; 
    size_t      pending_sent_len  ;
    SOCKADDR_IN udp_remote_addr   ; //for udp pending sent 
} PENDING_SENT ;

typedef int SOCKET_T;
typedef struct _Context_ {
    SOCKET_T        socket  {-1};
    CumBuffer* GetBuffer() {
        return & recv_buffer;
    }
    CumBuffer       recv_buffer;
    std::mutex      ctx_lock ; 
    bool            is_packet_len_calculated {false};
    size_t          complete_packet_len {0} ;
    std::deque<PENDING_SENT> pending_send_deque ; 
    bool            is_sent_pending {false}; 
    bool            is_connected {false}; 
    SOCKADDR_IN     udp_remote_addr ; //for udp
} Context ;


////////////////////////////////////////////////////////////////////////////////
class ASockBase {
public :

    ASockBase(){
        is_connected_ = false;
    }

    ~ASockBase() {
        if (sock_usage_ == SOCK_USAGE_TCP_CLIENT ||
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
            ClearServer();
        }
    }

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
        if(sock_usage_ == SOCK_USAGE_IPC_SERVER  ) {
            if(server_ipc_socket_path_.length()>0){
                DBG_LOG("unlink :" << server_ipc_socket_path_);
                unlink(server_ipc_socket_path_.c_str());
                server_ipc_socket_path_="";
            }
        }
        if ( listen_context_ptr_ ){
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
        char* data_buffer = new char[total_len];// FIX:
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
    size_t  OnCalculateDataLen(Context* ctx_ptr) {
        DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
            " / capacity : " << ctx_ptr->recv_buffer.GetCapacity() <<
            " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace() <<
            " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace() <<
            " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() );
        if( ctx_ptr->recv_buffer.GetCumulatedLen() < (int)HEADER_SIZE ) {
            DBG_LOG("more to come");
            return asock::MORE_TO_COME ; //more to come 
        }
        ST_HEADER header ;
        ctx_ptr->recv_buffer.PeekData(HEADER_SIZE, (char*)&header);  
        size_t supposed_total_len = std::atoi(header.msg_len) + HEADER_SIZE;
        if(supposed_total_len > ctx_ptr->recv_buffer.GetCapacity()) {
            DBG_RED_LOG( "(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
                 " / packet length : " << supposed_total_len <<
                 " / buffer is insufficient => increase buffer");
            // If the size of the data to be received is larger than the buffer, 
            // increase the buffer capacity.
            size_t new_buffer_len= supposed_total_len * 2;
            if(!ctx_ptr->recv_buffer.IncreaseBufferAndCopyExisting(new_buffer_len)) {
                ELOG(ctx_ptr->recv_buffer.GetErrMsg());
                exit(EXIT_FAILURE);
            }
            SetBufferCapacity(new_buffer_len);

            DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
                " / capacity : " << ctx_ptr->recv_buffer.GetCapacity() <<
                " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace() <<
                " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace() <<
                " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() );
        }; 
        DBG_LOG( "(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
                " / packet length : " << supposed_total_len );
        return supposed_total_len ;
    }

    virtual bool OnRecvedCompleteData(Context* , const char* const , size_t ) {
        std::cerr << "ERROR! OnRecvedCompleteData not implemented! \n";
        return false;
    } 
public:
    virtual void SetUsage()=0;
protected :
    ENUM_SOCK_USAGE sock_usage_ {asock::SOCK_USAGE_UNKNOWN};
    size_t  max_data_len_ {0};
    std::mutex   lock_ ; 
#ifdef __APPLE__
    struct  kevent* kq_events_ptr_ {nullptr};
    int     kq_fd_ {-1};
#elif __linux__
    struct  epoll_event* ep_events_{nullptr};
    int     ep_fd_ {-1};
#endif

    std::string  err_msg_ ;
    std::function<bool(Context*,const char* const,size_t)> cb_on_recved_complete_packet_{nullptr};

    //-------------------------------------------------------------------------
    bool SetSockoptSndRcvBufUdp(SOCKET_T socket){
        size_t opt_cur ; 
        int opt_val=max_data_len_ ; 
        int opt_len = sizeof(opt_cur) ;
        if (getsockopt(socket,SOL_SOCKET,SO_SNDBUF,&opt_cur, (SOCKLEN_T *) &opt_len)==-1) {
            err_msg_ = "gsetsockopt SO_SNDBUF error ["  + 
                        std::string(strerror(errno)) + "]";
            return false;
        }
        if(max_data_len_ > opt_cur ) {
            if (setsockopt(socket,SOL_SOCKET,SO_SNDBUF,(char*)&opt_val, sizeof(opt_val))==-1) {
                err_msg_ = "setsockopt SO_SNDBUF error ["  + 
                            std::string(strerror(errno)) + "]";
                return false;
            }
        }
        if (getsockopt(socket,SOL_SOCKET,SO_RCVBUF,&opt_cur, (SOCKLEN_T *)&opt_len)==-1) {
            err_msg_ = "setsockopt SO_RCVBUF error ["  + std::string(strerror(errno)) + "]";
            return false;
        }
        if(max_data_len_ > opt_cur ) {
            if (setsockopt(socket,SOL_SOCKET,SO_RCVBUF,(char*)&opt_val, sizeof(opt_val))==-1) {
                err_msg_ = "setsockopt SO_RCVBUF error ["  + std::string(strerror(errno)) + "]";
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
                // UDP : all or nothing . no partial sent!
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
                            //error!!!
                            if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT || 
                                 sock_usage_ == SOCK_USAGE_IPC_CLIENT ) {
                                close( ctx_ptr->socket);
                                InvokeServerDisconnectedHandler();
                                is_client_thread_running_ = false;
                            } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER || 
                                        sock_usage_ == SOCK_USAGE_IPC_SERVER  ) {
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
                    err_msg_ = "send error ["  + std::string(strerror(errno)) + "]";
                    if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT || 
                         sock_usage_ == SOCK_USAGE_IPC_CLIENT ) {
                        //client error!!!
                        close( ctx_ptr->socket);
                        InvokeServerDisconnectedHandler();
                        is_client_thread_running_ = false;
                        return false; 
                    } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER || 
                                sock_usage_ == SOCK_USAGE_IPC_SERVER  ) {
                        TerminateClient(ctx_ptr); 
                    }
                    break;
                } 
            } 
        } //while
        return true;
    }

    //-------------------------------------------------------------------------
    bool RecvData(Context* ctx_ptr){
        DBG_LOG("-----------------------------------------------------------" <<
                " max_data_len_: " << max_data_len_ );
        DBG_LOG("          (usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
                " / capacity : " << ctx_ptr->recv_buffer.GetCapacity()  <<
                " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace()  <<
                " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace() <<
                " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() );

        size_t free_linear_len = ctx_ptr->recv_buffer.GetLinearFreeSpace();
        assert(free_linear_len != 0);
        int recved_len = recv( ctx_ptr->socket, 
                              ctx_ptr->recv_buffer.GetLinearAppendPtr(), free_linear_len, 0); 
        if( recved_len > 0) {
            DBG_LOG("          (usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  
                    << " / want to recv : " << free_linear_len << " / actual recved :" << recved_len );
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
                    DBG_GREEN_LOG("          (usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
                     " / need to recv more, buffer cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() ) ;
                    return true; //need to recv more
                } else {
                    //got complete packet 
                    char* complete_packet_data = new (std::nothrow) char [ctx_ptr->complete_packet_len] ; // TODO no..not good
                    if(complete_packet_data == NULL) {
                        err_msg_ = "memory alloc failed!";
                        return false;
                    }
                    if(cumbuffer::OP_RSLT_OK!= ctx_ptr->recv_buffer.GetData(ctx_ptr->complete_packet_len, 
                                                                       complete_packet_data )) {
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
                    DBG_LOG("          (usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
                            " / capacity : " << ctx_ptr->recv_buffer.GetCapacity()  <<
                            " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace()  <<
                            " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace() <<
                            " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() );
                    ctx_ptr->is_packet_len_calculated = false;
                    delete[] complete_packet_data; 
                }
            } //while
        } else if( recved_len == 0 ) {
            err_msg_ = "recv 0, client disconnected , fd:" + std::to_string(ctx_ptr->socket);
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
            DBG_LOG("          (usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  
                    << " / actual recved :" << recved_len );
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
            DBG_LOG("          (usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket  <<
                    " / capacity : " << ctx_ptr->recv_buffer.GetCapacity()  <<
                    " / total free : " << ctx_ptr->recv_buffer.GetTotalFreeSpace()  <<
                    " / linear free : " << ctx_ptr->recv_buffer.GetLinearFreeSpace() <<
                    " / cumulated : " << ctx_ptr->recv_buffer.GetCumulatedLen() );
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
            err_msg_ = "socket:" + std::to_string(ctx_ptr->socket) + " / error [" + std::string(strerror(errno)) + "]";
            return false; 
        }
        return true;
    }
#endif


    //--------------------------------------------------------- 
    //NOTE: ClientUsage
    //--------------------------------------------------------- 
public :

    //-------------------------------------------------------------------------
    bool SendToServer (const char* data, size_t len) {
        if ( !is_connected_ ) {
            err_msg_ = "not connected";
            return false;
        }
        return SendData(&client_ctx_, data, len);
    } 

    //-------------------------------------------------------------------------
    // bool SendFile (const char* file_path) { TODO: send file .
    // }
    //-------------------------------------------------------------------------
    SOCKET_T  GetSocket () { return  client_ctx_.socket ; }
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
    } ;

    //-------------------------------------------------------------------------
    //for client use

protected :
    int         connect_timeout_secs_    ;
    bool        is_buffer_init_ {false};
    std::atomic<bool>    is_connected_   {false};
    Context     client_ctx_;
    SOCKADDR_IN tcp_server_addr_ ;
    SOCKADDR_IN udp_server_addr_ ;
    //std::thread client_thread_;
    SOCKADDR_UN ipc_conn_addr_   ;
    std::atomic<bool> is_client_thread_running_ {false};
    bool is_server_disconnted_cb_invoked_ {false};

    //-------------------------------------------------------------------------
    bool RunServer() {
#ifdef __APPLE__
        if ( kq_events_ptr_ ) {
#elif __linux__
        if (ep_events_) {
#endif
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
            err_msg_ = "init error [" + std::string(strerror(errno)) + "]";
            return false;
        }   
        if(!SetSocketNonBlocking (listen_socket_)) {
            return  false;
        }
        int opt_on=1;
        int result = -1;
        if (setsockopt(listen_socket_,SOL_SOCKET,SO_REUSEADDR,&opt_on,sizeof(opt_on))==-1) {
            err_msg_ = "setsockopt SO_REUSEADDR error ["  + 
                        std::string(strerror(errno)) + "]";
            return false;
        }
        if (setsockopt(listen_socket_,SOL_SOCKET,SO_KEEPALIVE, &opt_on, sizeof(opt_on))==-1) {
            err_msg_ = "setsockopt SO_KEEPALIVE error ["  + 
                        std::string(strerror(errno)) + "]";
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
        } else if (sock_usage_ == SOCK_USAGE_TCP_SERVER || 
                   sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            SOCKADDR_IN    server_addr  ;
            memset((void *)&server_addr,0x00,sizeof(server_addr)) ;
            server_addr.sin_family      = AF_INET ;
            server_addr.sin_addr.s_addr = inet_addr(server_ip_.c_str()) ;
            server_addr.sin_port = htons(server_port_);
            result = bind(listen_socket_,(SOCKADDR*)&server_addr,sizeof(server_addr)) ;
        }
        //-------------------------------------------------
        if ( result < 0 ) {
            if ( sock_usage_ == SOCK_USAGE_IPC_SERVER ) {
                ELOG(server_ipc_socket_path_.c_str());
            }
            err_msg_ = "bind error ["  + std::string(strerror(errno)) + "]";
            return false ;
        }
        if (sock_usage_ == SOCK_USAGE_IPC_SERVER || 
            sock_usage_ == SOCK_USAGE_TCP_SERVER ) {
            result = listen(listen_socket_,SOMAXCONN) ;
            if ( result < 0 ) {
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
            err_msg_ = "epoll create error ["  + std::string(strerror(errno)) + "]";
            return false;
        }
        // RunServer
        if(!ControlEpoll (listen_context_ptr_, EPOLLIN , EPOLL_CTL_ADD)){
            return false;
        }
#endif
        //start server thread
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
            std::thread server_thread(&ASockBase::ServerThreadUdpRoutine, this);
            server_thread.detach();
        } else {
            std::thread server_thread(&ASockBase::ServerThreadRoutine, this);
            server_thread.detach();
        }
        return true;
    }

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
    };  

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
            std::thread client_thread(&ASockBase::ClientThreadRoutine, this);
            client_thread.detach();
            is_client_thread_running_ = true;
        }
        return true;
    };

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
    };

    //-------------------------------------------------------------------------
    void InvokeServerDisconnectedHandler() {
        is_server_disconnted_cb_invoked_=true;
        if(cb_on_disconnected_from_server_!=nullptr) {
            cb_on_disconnected_from_server_();
        } else {
            OnDisconnectedFromServer();
        }
    };

public :
    //-------------------------------------------------------------------------
    void TerminateClient(Context* client_ctx, bool is_graceful=true) {
        client_cnt_--;
        DBG_LOG(" socket : "<< client_ctx->socket <<", connection closing ,graceful=" 
                << (is_graceful ? "TRUE" : "FALSE"));
        if (!is_graceful) {
            // force the subsequent closesocket to be abortative.
            struct linger linger_struct ;
            linger_struct.l_onoff = 1;
            linger_struct.l_linger = 0;
            setsockopt(client_ctx->socket, SOL_SOCKET, SO_LINGER, (char*)&linger_struct, 
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

    //for composition : Assign yours to these callbacks 
    //-------------------------------------------------------------------------
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
private :
    std::function<void()> cb_on_disconnected_from_server_ {nullptr} ;

    //for inheritance : Implement these virtual functions.
    virtual void  OnDisconnectedFromServer() {}; 

    //--------------------------------------------------------- 
    // NOTE: ServerUsage
    //--------------------------------------------------------- 
public :

    //-------------------------------------------------------------------------
    bool  IsServerRunning(){
        return is_server_running_;
    }

    //-------------------------------------------------------------------------
    void  StopServer() {
        close(listen_socket_);
        is_need_server_run_ = false;
        if ( sock_usage_ == SOCK_USAGE_IPC_SERVER  ) {
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
    size_t  GetMaxEventCount(){return max_event_ ; }
    //-------------------------------------------------------------------------
    int   GetCountOfClients(){ return client_cnt_ ; }

protected :
    std::string       server_ip_   ;
    std::string       server_ipc_socket_path_ ="";
    int               server_port_ {-1};
    std::atomic<int>  client_cnt_ {0}; 
    std::atomic<bool> is_need_server_run_ {true};
    std::atomic<bool> is_server_running_  {false};
    SOCKET_T          listen_socket_     ;
    size_t            max_event_  {0};
    std::atomic<int>  cur_quit_cnt_{0};
    std::queue<Context*> queue_ctx_cache_;
    std::mutex           ctx_cache_lock_ ; 
#if defined __APPLE__ || defined __linux__ 
    Context*  listen_context_ptr_ {nullptr};
#endif
    std::unordered_map<SOCKET_T, Context*> map_sock_ctx_;

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
                err_msg_ = "kevent error ["  + std::string(strerror(errno)) + "]";
                is_server_running_ = false;
                return;
            }
#elif __linux__
            int event_cnt = epoll_wait(ep_fd_, ep_events_, max_event_, 100 );
            if (event_cnt < 0) {
                err_msg_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
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
                    //# accept #----------
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
                        //# send #----------
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
        if (cumbuffer::OP_RSLT_OK != listen_context_ptr_->recv_buffer.Init(max_data_len_) ) {
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
            int event_cnt = epoll_wait(ep_fd_, ep_events_, max_event_, 100 );
            if (event_cnt < 0) {
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
                        exit(EXIT_FAILURE);
                    }
                }
#ifdef __APPLE__
                else if (EVFILT_WRITE == kq_events_ptr_[i].filter ){
#elif __linux__
                else if (ep_events_[i].events & EPOLLOUT) {
#endif
                    //# send #----------
                    if(!SendPendingData(listen_context_ptr_)) {
                        is_server_running_ = false;
                        return; //error!
                    }
                } 
            } 
        }//while
        is_server_running_ = false;
    }

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
        DBG_LOG("~~~~~~~~~~ pop new ctx");
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
        DBG_LOG("~~~~~~~~~ push cache");
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
            DBG_LOG("~~~~~~~~~~~ #0 clear cache");
            delete ctx_ptr;
            queue_ctx_cache_.pop();
        }
        // server 가 먼저 종료하는 경우, queue_ctx_cache_ 에 없을수 있다
        for (auto& p : map_sock_ctx_){
            Context* ctx_ptr = p.second;
            std::cout << ' ' << p.first << " => " << ctx_ptr << '\n';
            while(!ctx_ptr->pending_send_deque.empty() ) {
                PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
                delete [] pending_sent.pending_sent_data;
                ctx_ptr->pending_send_deque.pop_front();
            }
            DBG_LOG("~~~~~~~~~~~ #1 clear cache");
            delete ctx_ptr;
        }
    }

    //-------------------------------------------------------------------------
    bool AcceptNewClient() {
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
            client_context_ptr->is_connected = true;
            map_sock_ctx_.insert({client_fd, client_context_ptr});

#ifdef __APPLE__
            if(!ControlKq(client_context_ptr, EVFILT_READ, EV_ADD )){
#elif __linux__
            // AcceptNewClient
            if(!ControlEpoll (client_context_ptr, EPOLLIN , EPOLL_CTL_ADD)){
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


    //for composition : Assign yours to these callbacks 
public :
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
private :
    std::function<void(Context*)> cb_on_client_connected_ {nullptr} ;
    std::function<void(Context*)> cb_on_client_disconnected_ {nullptr};

    //for inheritance : Implement these virtual functions.
    virtual void OnClientConnected(Context*) {}; 
    virtual void OnClientDisconnected(Context*) {} ;  
};
} //namespace
#endif
