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

///////////////////////////////////////////////////////////////////////////////
//for windows, ipcp(server), wsapoll(client) is used.
///////////////////////////////////////////////////////////////////////////////
//TODO SOCK_USAGE_IPC_SERVER 
//TODO SOCK_USAGE_IPC_CLIENT 

#include "ASockComm.hpp"
#include "ASockBuffer.hpp"

#include <atomic>
#include <thread>
#include <queue>
#include <deque>
#include <mutex> 
#include <functional>
#include <unordered_map>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#pragma comment(lib, "Ws2_32.lib")

typedef int SOCKLEN_T ;
typedef SOCKET SOCKET_T;

namespace asock {
enum class EnumIOType {
    IO_ACCEPT,
    IO_SEND,
    IO_RECV,
    IO_QUIT,
    IO_UNKNOWN
};
typedef struct _PENDING_SENT_ {
    char*       pending_sent_data ; 
    size_t      pending_sent_len  ;
    SOCKADDR_IN udp_remote_addr   ; //for udp pending sent 
} PENDING_SENT ;

typedef struct _PER_IO_DATA_ {
    OVERLAPPED  overlapped ;
    WSABUF      wsabuf;
    EnumIOType  io_type;
    CumBuffer   cum_buffer;
    size_t      total_send_len {0} ;
    size_t      complete_recv_len {0} ;
    size_t      sent_len {0} ; //XXX
    bool        is_packet_len_calculated {false};
} PER_IO_DATA; //XXX 1200 bytes.... 

typedef struct _Context_ {
    SOCKET_T     socket; 
    int          sock_id_copy{ -1 };
    PER_IO_DATA* per_recv_io_ctx { NULL }; 
    std::mutex   ctx_lock ; 
    std::atomic<int>  recv_ref_cnt{ 0 }; 
    std::atomic<int>  send_ref_cnt{ 0 }; 
    CumBuffer* GetBuffer() {
        return & (per_recv_io_ctx->cum_buffer);
    }
    bool         is_connected {false}; 
    SOCKADDR_IN  udp_remote_addr ; //for udp
    std::deque<PENDING_SENT> pending_send_deque ; 
    bool         is_sent_pending {false}; 
} Context ;

////////////////////////////////////////////////////////////////////////////////
class ASock {
public :

    ASock(){
        is_connected_ = false;
    } 

    ~ASock() {
        if (sock_usage_ == SOCK_USAGE_TCP_CLIENT ||
            sock_usage_ == SOCK_USAGE_UDP_CLIENT ||
            sock_usage_ == SOCK_USAGE_IPC_CLIENT) {
            Disconnect();
        } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER ||
            sock_usage_ == SOCK_USAGE_UDP_SERVER ||
            sock_usage_ == SOCK_USAGE_IPC_SERVER) {
            ClearClientCache();
            ClearPerIoDataCache();
        }
        WSACleanup();
    }  

    //-------------------------------------------------------------------------
    bool SetSocketNonBlocking(int sock_fd) {
        // If iMode = 0, blocking is enabled; 
        // If iMode != 0, non-blocking mode is enabled.
        unsigned long nMode = 1;
        int nResult = ioctlsocket(sock_fd, FIONBIO, &nMode);
        if (nResult != NO_ERROR) {
            BuildErrMsgString(WSAGetLastError());
            ELOG( err_msg_ ) ;
            return  false;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    bool SendData(Context* ctx_ptr, const char* data_ptr, size_t len) {
        // MSDN:
        // WSASend should not be called on the same stream-oriented socket 
        // concurrently from different threads, 
        // because some Winsock providers may split a large send request into multiple transmissions, 
        // and this may lead to unintended data interleaving from multiple concurrent send requests 
        // on the same stream-oriented socket.
        std::lock_guard<std::mutex> lock(context_.ctx_lock); 
        if (ctx_ptr->socket == INVALID_SOCKET) {
            DBG_ELOG("invalid socket, failed.");
            return false;
        }
        if (!ctx_ptr->is_connected) {
            DBG_ELOG("diconnected socket, failed.");
            return false;
        }
        //ex)multi thread 가 동일한 소켓에 대해 각각 보내는 경우...
        //buffer 가 각각 필요함(전송 여부 비동기 통지, 중첩)
        PER_IO_DATA* per_send_io_data = PopPerIoDataFromCache();
        if (per_send_io_data == nullptr) {
            ELOG("mem alloc failed");
            shutdown(ctx_ptr->socket, SD_BOTH);
            if (0 != closesocket(ctx_ptr->socket)) {
                DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " 
                         << WSAGetLastError());
            }
            ctx_ptr->socket = INVALID_SOCKET; //XXX
            exit(EXIT_FAILURE);
        }

        ST_HEADER header;
        snprintf(header.msg_len, sizeof(header.msg_len), "%d", (int)len);

        DWORD dw_flags = 0;
        DWORD dw_send_bytes = 0;
        size_t total_len = len + HEADER_SIZE;
        char* send_buffer = new char[len+HEADER_SIZE]; //XXX alloc
        memcpy(send_buffer,(char*)&header, HEADER_SIZE);
        memcpy(send_buffer+ HEADER_SIZE, data_ptr, len);
        per_send_io_data->wsabuf.buf = send_buffer ;
        per_send_io_data->wsabuf.len = (ULONG)total_len;
        per_send_io_data->io_type = EnumIOType::IO_SEND;
        per_send_io_data->total_send_len = total_len;
        per_send_io_data->sent_len = 0;
        int result = 0;

        if (sock_usage_ == SOCK_USAGE_UDP_SERVER) {
            result = WSASendTo(ctx_ptr->socket, 
                               &(per_send_io_data->wsabuf), 
                               1, 
                               &dw_send_bytes, 
                               dw_flags,
                               (SOCKADDR*)&ctx_ptr->udp_remote_addr, 
                               sizeof(SOCKADDR_IN), 
                               &(per_send_io_data->overlapped), 
                               NULL);
        } else {
            result = WSASend(ctx_ptr->socket, 
                             &(per_send_io_data->wsabuf), 
                             1, 
                             &dw_send_bytes, 
                             dw_flags, 
                             &(per_send_io_data->overlapped), 
                             NULL);
        }
        if (result == 0) {
            //no error occurs and the send operation has completed immediately
            DBG_LOG("WSASend returns 0 : no error");
        } else if (result == SOCKET_ERROR) {
            if (WSA_IO_PENDING == WSAGetLastError()) {
                //DBG_LOG("!!! WSASend returns --> WSA_IO_PENDING");
            } else {
                int last_err = WSAGetLastError();
                BuildErrMsgString(last_err);
                PushPerIoDataToCache(per_send_io_data);
                shutdown(ctx_ptr->socket, SD_BOTH);
                if (0 != closesocket(ctx_ptr->socket)) {
                    DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " << last_err);
                }
                ctx_ptr->socket = INVALID_SOCKET; 
                return false;
            }
        }
        ctx_ptr->send_ref_cnt++;
        DBG_LOG("sock=" << ctx_ptr->sock_id_copy << ", send_ref_cnt=" 
                << ctx_ptr->send_ref_cnt);
        return true;
    }

    //-------------------------------------------------------------------------
    bool SetBufferCapacity(size_t max_data_len) {
        if(max_data_len<=0) {
            err_msg_ = " length is invalid";
            return false;
        }
        max_data_len_ = max_data_len ; 
        return true;
    }

    //-------------------------------------------------------------------------
    std::string GetLastErrMsg(){
        return err_msg_; 
    }

    //-------------------------------------------------------------------------
    bool SetCbOnRecvedCompletePacket(std::function<bool(Context*,char*,size_t)> cb) {
        //for composition usage 
        if(cb != nullptr) {
            cb_on_recved_complete_packet_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }

    HANDLE  handle_completion_port_;

protected :
    size_t  max_data_len_ {0};
    std::mutex   lock_ ; 
    std::mutex   err_msg_lock_ ; 
    std::string  err_msg_ ;
    ENUM_SOCK_USAGE sock_usage_ {asock::SOCK_USAGE_UNKNOWN};
    std::function<bool(Context*,char*,size_t)>cb_on_recved_complete_packet_{nullptr};

    //-------------------------------------------------------------------------
    //for udp server, client only
    bool SetSockoptSndRcvBufUdp(SOCKET_T socket) {
        int opt_cur = 0;
        int opt_val = int(max_data_len_);
        int opt_len = sizeof(opt_cur);
        if (getsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*) &opt_cur, 
                       (SOCKLEN_T*)&opt_len) == -1) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "gsetsockopt SO_SNDBUF error [" + 
                        std::string(strerror(errno)) + "]";
            return false;
        }
        //std::cout << "curr SO_SNDBUF = " << opt_cur << "\n";
        if (max_data_len_ > opt_cur) {
            if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&opt_val, 
                           sizeof(opt_val)) == -1) {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = "setsockopt SO_SNDBUF error [" + 
                            std::string(strerror(errno)) + "]";
                return false;
            }
            std::cout << "set SO_SNDBUF = " << opt_val << "\n";
        }
        //--------------
        if (getsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*) & opt_cur, 
                       (SOCKLEN_T*)&opt_len) == -1) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "setsockopt SO_RCVBUF error [" + 
                        std::string(strerror(errno)) + "]";
            return false;
        }
        //std::cout << "curr SO_RCVBUF = " << opt_cur << "\n";
        if (max_data_len_ > opt_cur) {
            if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&opt_val, 
                           sizeof(opt_val)) == -1) {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = "setsockopt SO_RCVBUF error [" + 
                            std::string(strerror(errno)) + "]";
                return false;
            }
            std::cout << "set SO_RCVBUF = " << opt_val << "\n";
        }
        return true;
    }
    //-------------------------------------------------------------------------
    bool InitWinsock() {
        WSADATA wsa_data;
        int nResult = WSAStartup(MAKEWORD(2, 2), & wsa_data);
        if (nResult != 0) {
            BuildErrMsgString(WSAGetLastError());
            WSACleanup();
            return false;
        }
        if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
            err_msg_ = "Could not find a usable version of Winsock.dll";
            WSACleanup();
            return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    void StartWorkerThreads() {
        SYSTEM_INFO system_info;
        GetSystemInfo(&system_info);
        max_worker_cnt_ = system_info.dwNumberOfProcessors * 2;
        DBG_LOG("(server) worker cnt = " << max_worker_cnt_);
        for (size_t i = 0; i < max_worker_cnt_ ; i++) {
            std::thread worker_thread(&ASock::WorkerThreadRoutine, this, i); 
            worker_thread.detach();
        }
    }
    //-------------------------------------------------------------------------
    bool StartServer() {
        DBG_LOG("PER_IO_DATA size ==> " << sizeof(PER_IO_DATA));
        if ( sock_usage_ == SOCK_USAGE_IPC_SERVER ) {
            // TODO
            //listen_socket_ = WSASocketW(AF_UNIX, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
        } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER ) {
            listen_socket_ = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
        } else if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            listen_socket_ = WSASocketW(AF_INET, SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
        }
        if (listen_socket_ == INVALID_SOCKET) {
            BuildErrMsgString(WSAGetLastError());
            return false;
        }
        if(!SetSocketNonBlocking (listen_socket_)) {
            return  false;
        }
        int opt_zero = 0;
        if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            if(!SetSockoptSndRcvBufUdp(listen_socket_)) {
                return false;
            }
        }
        int opt_on = 1;
        if (setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, 
                       (char*)&opt_on, sizeof(opt_on)) == SOCKET_ERROR) {
            BuildErrMsgString(WSAGetLastError());
            return false;
        }
        if (sock_usage_ != SOCK_USAGE_UDP_SERVER) {
            if (setsockopt(listen_socket_, SOL_SOCKET, SO_KEEPALIVE, 
                           (char*)&opt_on, sizeof(opt_on)) == SOCKET_ERROR) {
                BuildErrMsgString(WSAGetLastError());
                return false;
            }
        }

        int result = -1;
        if (sock_usage_ == SOCK_USAGE_IPC_SERVER) {
            //TODO
        } else if (sock_usage_ == SOCK_USAGE_TCP_SERVER || 
                   sock_usage_ == SOCK_USAGE_UDP_SERVER) {
            SOCKADDR_IN    server_addr  ;
            memset((void *)&server_addr,0x00,sizeof(server_addr)) ;
            server_addr.sin_family = AF_INET;
            inet_pton(AF_INET, server_ip_.c_str(), &(server_addr.sin_addr));
            server_addr.sin_port = htons(server_port_);
            result = bind(listen_socket_, (SOCKADDR*)&server_addr, sizeof(server_addr));
        }
        if (result < 0) {
            BuildErrMsgString(WSAGetLastError());
            return false;
        }
        if (sock_usage_ == SOCK_USAGE_IPC_SERVER || 
            sock_usage_ == SOCK_USAGE_TCP_SERVER) {
            result = listen(listen_socket_, SOMAXCONN);
            if (result < 0) {
                BuildErrMsgString(WSAGetLastError());
                return false;
            }
        }
        if (!BuildClientContextCache()) {
            return false;
        }
        if (!BuildPerIoDataCache()) {
            return false;
        }
        //XXX start server thread
        if ( sock_usage_ == SOCK_USAGE_UDP_SERVER ) {
            //UDP is special, no thread.
            SOCKLEN_T socklen = sizeof(SOCKADDR_IN);
            Context* ctx_ptr = PopClientContextFromCache();
            if (ctx_ptr == nullptr) {
                ELOG(err_msg_);
                exit(EXIT_FAILURE);
            }
            ctx_ptr->is_connected = true;
            ctx_ptr->socket = listen_socket_;
            handle_completion_port_ = CreateIoCompletionPort((HANDLE)listen_socket_, 
                                                             handle_completion_port_, 
                                                             (ULONG_PTR)ctx_ptr, 
                                                             0);
            if (NULL == handle_completion_port_) {
                BuildErrMsgString(WSAGetLastError());
                ELOG("alloc failed! delete ctx_ptr");
                delete ctx_ptr;
                exit(EXIT_FAILURE);
            }
            // Start receiving.
            if (!IssueRecv(99999, ctx_ptr)) { // worker_index 99999 is for debugging.
                int last_err = WSAGetLastError();
                DBG_ELOG("sock=" << ctx_ptr->socket <<  ", error! : " << last_err);
                shutdown(ctx_ptr->socket, SD_BOTH);
                if (0 != closesocket(ctx_ptr->socket)) {
                    DBG_ELOG("sock=" << ctx_ptr->socket << ",close socket error! : " 
                             << last_err);
                }
                LOG("delete ctx_ptr , sock=" << ctx_ptr->socket);
                ctx_ptr->socket = INVALID_SOCKET; //XXX
                if (last_err == WSAECONNRESET) {
                    DBG_ELOG("invoke PuchClientContextToCache.. sock=" 
                             << ctx_ptr->sock_id_copy);
                    PushClientContextToCache(ctx_ptr);
                }
                exit(EXIT_FAILURE);
            }
        } else {
            std::thread server_thread(&ASock::AcceptThreadRoutine, this);
            server_thread.detach();
        }
        is_server_running_ = true;
        return true;
    }

    //-------------------------------------------------------------------------
    void BuildErrMsgString(int err_no) {
        LPVOID lpMsgBuf;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                      NULL, WSAGetLastError(), 
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = std::string("(") + std::to_string(err_no) + std::string(") ");
        err_msg_ += std::string((LPCTSTR)lpMsgBuf);
        LocalFree(lpMsgBuf);
    }

    //-------------------------------------------------------------------------
    bool RecvData(size_t worker_id, Context* ctx_ptr, DWORD bytes_transferred) {
        DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket <<
                " / capacity : " << ctx_ptr->GetBuffer()->GetCapacity() <<
                " / total free : " << ctx_ptr->GetBuffer()->GetTotalFreeSpace() <<
                " / linear free : " << ctx_ptr->GetBuffer()->GetLinearFreeSpace() <<
                " / cumulated : " << ctx_ptr->GetBuffer()->GetCumulatedLen());

        ctx_ptr->GetBuffer()->IncreaseData(bytes_transferred);
        while (ctx_ptr->per_recv_io_ctx->cum_buffer.GetCumulatedLen()) {
            if (!ctx_ptr->per_recv_io_ctx->is_packet_len_calculated){ 
                //only when calculation is necessary
                ctx_ptr->per_recv_io_ctx->complete_recv_len = OnCalculateDataLen(ctx_ptr);
                ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = true;
            }
            if (ctx_ptr->per_recv_io_ctx->complete_recv_len == asock::MORE_TO_COME) {
                ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
                return true; //need to recv more
            } else if (ctx_ptr->per_recv_io_ctx->complete_recv_len > 
                       ctx_ptr->per_recv_io_ctx->cum_buffer.GetCumulatedLen()) {
                return true; //need to recv more
            } else {
                //got complete packet 
                DBG_LOG("got complete packet");
                size_t alloc_len = ctx_ptr->per_recv_io_ctx->complete_recv_len;
                char* complete_packet_data = new (std::nothrow) char [alloc_len] ; //XXX 
                if(complete_packet_data == nullptr) {
                    ELOG("mem alloc failed");
                    exit(EXIT_FAILURE);
                }
                if (cumbuffer::OP_RSLT_OK !=
                    ctx_ptr->per_recv_io_ctx->cum_buffer.GetData (
                        ctx_ptr->per_recv_io_ctx->complete_recv_len, 
                        complete_packet_data)) {
                    //error !
                    {
                        std::lock_guard<std::mutex> lock(err_msg_lock_);
                        err_msg_ = ctx_ptr->per_recv_io_ctx->cum_buffer.GetErrMsg();
                        ELOG(err_msg_);
                    }
                    ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
                    delete[] complete_packet_data; //XXX
                    exit(EXIT_FAILURE);
                }
                if (cb_on_recved_complete_packet_ != nullptr) {
                    //invoke user specific callback
                    cb_on_recved_complete_packet_(ctx_ptr, 
                                                  complete_packet_data+HEADER_SIZE , 
                                                  ctx_ptr->per_recv_io_ctx->complete_recv_len-HEADER_SIZE);
                } else {
                    //invoke user specific implementation
                    OnRecvedCompleteData(ctx_ptr, complete_packet_data+HEADER_SIZE, 
                                         ctx_ptr->per_recv_io_ctx->complete_recv_len-HEADER_SIZE);
                }
                delete[] complete_packet_data; //XXX
                ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
            }
        } //while
        return true;
    }

    //-------------------------------------------------------------------------
    bool RecvfromData(size_t worker_id, Context* ctx_ptr, DWORD bytes_transferred) {
        ctx_ptr->GetBuffer()->IncreaseData(bytes_transferred);
        DBG_LOG("worker="<< worker_id <<",sock="<<ctx_ptr->sock_id_copy
                <<",GetCumulatedLen = " << ctx_ptr->GetBuffer()->GetCumulatedLen() 
                << ",recv_ref_cnt=" << ctx_ptr->recv_ref_cnt << ",bytes_transferred=" 
                << bytes_transferred );
        char* complete_packet_data = new (std::nothrow) char[bytes_transferred]; //XXX TODO !!!
        if (complete_packet_data == nullptr) {
            ELOG("mem alloc failed");
            exit(EXIT_FAILURE);
        }
        if (cumbuffer::OP_RSLT_OK != ctx_ptr->per_recv_io_ctx->cum_buffer.
            GetData(bytes_transferred, complete_packet_data)) {
            //error !
            {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = ctx_ptr->per_recv_io_ctx->cum_buffer.GetErrMsg();
            }
            //ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
            delete[] complete_packet_data; //XXX
            DBG_ELOG("error! ");
            //exit(1);
            return false;
        }
        //XXX UDP 이므로 받는 버퍼를 초기화해서, linear free space를 초기화 상태로 
        ctx_ptr->GetBuffer()->ReSet(); //this is udp. all data has arrived!
        DBG_LOG("worker=" << worker_id << ",sock=" << ctx_ptr->sock_id_copy 
                << ",recv_ref_cnt=" << ctx_ptr->recv_ref_cnt 
                << ":got complete data [" << complete_packet_data << "]");
        if (cb_on_recved_complete_packet_ != nullptr) {
            //invoke user specific callback
            cb_on_recved_complete_packet_(ctx_ptr, complete_packet_data+HEADER_SIZE, 
                                          bytes_transferred - HEADER_SIZE ); //udp
        } else {
            //invoke user specific implementation
            OnRecvedCompleteData(ctx_ptr, complete_packet_data+HEADER_SIZE, 
                                 bytes_transferred-HEADER_SIZE); //udp
        }
        delete[] complete_packet_data; //XXX
        return true;
    }

    //-------------------------------------------------------------------------
    void ReSetCtxPtr(Context* ctx_ptr) {
        //DBG_LOG("sock=" << ctx_ptr->socket << ",sock=" << ctx_ptr->sock_id_copy);
        SecureZeroMemory((PVOID)&ctx_ptr->per_recv_io_ctx->overlapped, sizeof(WSAOVERLAPPED));
        ctx_ptr->per_recv_io_ctx->cum_buffer.ReSet();
        ctx_ptr->socket = INVALID_SOCKET;
        ctx_ptr->sock_id_copy = -1;
        ctx_ptr->send_ref_cnt = 0;
        ctx_ptr->recv_ref_cnt = 0;
        ctx_ptr->per_recv_io_ctx->wsabuf.buf = NULL;
        ctx_ptr->per_recv_io_ctx->wsabuf.len = 0;
        ctx_ptr->per_recv_io_ctx->io_type = EnumIOType::IO_UNKNOWN;
        ctx_ptr->per_recv_io_ctx->total_send_len = 0;
        ctx_ptr->per_recv_io_ctx->complete_recv_len = 0;
        ctx_ptr->per_recv_io_ctx->sent_len = 0;
        ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
        ctx_ptr->is_connected = false;
        while(!ctx_ptr->pending_send_deque.empty() ) {
            PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
            delete [] pending_sent.pending_sent_data;
            ctx_ptr->pending_send_deque.pop_front();
        }
    }
    //-------------------------------------------------------------------------
    bool SendPendingData() {
        std::lock_guard<std::mutex> guard(context_.ctx_lock);
        while(!context_.pending_send_deque.empty()) {
            DBG_LOG("pending exists");
            PENDING_SENT pending_sent = context_.pending_send_deque.front();
            int sent_len = 0;
             if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
                //XXX UDP : all or nothing . no partial sent!
                sent_len = sendto(context_.socket,  pending_sent.pending_sent_data, 
                                  (int)pending_sent.pending_sent_len,
                                  0, (SOCKADDR*)&udp_server_addr_, 
                                  int(sizeof(udp_server_addr_)));
            } else {
                sent_len = send(context_.socket, pending_sent.pending_sent_data, 
                                (int)pending_sent.pending_sent_len, 0) ;
            }
            if( sent_len > 0 ) {
                if(sent_len == pending_sent.pending_sent_len) {
                    delete [] pending_sent.pending_sent_data;
                    context_.pending_send_deque.pop_front();
                    if(context_.pending_send_deque.empty()) {
                        //sent all data
                        context_.is_sent_pending = false;
                        DBG_LOG("all pending is sent");
                        break;
                    }
                } else {
                    //TCP : partial sent ---> 남은 부분을 다시 제일 처음으로
                    DBG_LOG("partial pending is sent, retry next time");
                    PENDING_SENT partial_pending_sent;
                    size_t alloc_len = pending_sent.pending_sent_len - sent_len;
                    partial_pending_sent.pending_sent_data = new char [alloc_len]; 
                    partial_pending_sent.pending_sent_len  = alloc_len;
                    memcpy( partial_pending_sent.pending_sent_data, 
                            pending_sent.pending_sent_data+sent_len, alloc_len);
                    //remove first.
                    delete [] pending_sent.pending_sent_data;
                    context_.pending_send_deque.pop_front();
                    //push_front
                    context_.pending_send_deque.push_front(partial_pending_sent);
                    break; //next time
                }
            } else if( sent_len == SOCKET_ERROR ) {
                if (WSAEWOULDBLOCK == WSAGetLastError()) {
                    break; //next time
                } else {
                    {//lock scope
                        std::lock_guard<std::mutex> lock(err_msg_lock_);
                        err_msg_ = "send error ["  + std::string(strerror(errno)) + "]";
                    }
                    BuildErrMsgString(WSAGetLastError());
                    OnDisconnectedFromServer();
                    shutdown(context_.socket, SD_BOTH);
                    if (0 != closesocket(context_.socket)) {
                        DBG_ELOG("close socket error! : " << WSAGetLastError());
                    }
                    context_.socket = INVALID_SOCKET;
                    return false; 
                } 
            } 
        } //while
        return true;
    }
    //-------------------------------------------------------------------------
    // worker_index is for debugging.
    bool IssueRecv(size_t worker_index, Context* client_ctx) {
        //--------------------------------------------------------
        // wsarecv 를 호출해놓고 데이터 수신을 기다린다. 
        // client 가 보내거나 연결을 종료할때만 GetQueuedCompletionStatus 로 통지된다.
        // -------------------------------------------------------
        // 현재는 socket 별로 수신 후 1번만 IssueRecv 호출하게 되어 있어서 lock 은 불필요
        // MSDN :
        // WSARecv should not be called on the same socket simultaneously from different threads, 
        // because it can result in an unpredictable buffer order.
        //--------------------------------------------------------

        //-------------------------------------
        size_t free_linear_len = client_ctx->GetBuffer()->GetLinearFreeSpace();
        DBG_LOG("want to recv len = " << free_linear_len);
        DWORD dw_flags = 0;
        DWORD dw_recv_bytes = 0;
        SecureZeroMemory((PVOID)& client_ctx->per_recv_io_ctx->overlapped, sizeof(WSAOVERLAPPED));
        //XXX cumbuffer 에 가능한 만큼만 ...
        client_ctx->per_recv_io_ctx->wsabuf.buf = client_ctx->GetBuffer()->GetLinearAppendPtr();
        client_ctx->per_recv_io_ctx->wsabuf.len = (ULONG)free_linear_len ;
        client_ctx->per_recv_io_ctx->io_type = EnumIOType::IO_RECV;
        int result = -1;
        if (sock_usage_ == SOCK_USAGE_UDP_SERVER || sock_usage_ == SOCK_USAGE_UDP_CLIENT) {
            SOCKLEN_T addrlen = sizeof(context_.udp_remote_addr);
            result = WSARecvFrom(client_ctx->socket, 
                                 &(client_ctx->per_recv_io_ctx->wsabuf),
                                 1, 
                                 (LPDWORD)&dw_recv_bytes, 
                                 (LPDWORD)&dw_flags,
                                 (struct sockaddr*)& client_ctx->udp_remote_addr, 
                                 &addrlen, 
                                 &(client_ctx->per_recv_io_ctx->overlapped), 
                                 NULL);
        } else {
            result = WSARecv(client_ctx->socket, 
                             &(client_ctx->per_recv_io_ctx->wsabuf),
                             1, 
                             &dw_recv_bytes, 
                             &dw_flags,
                             &(client_ctx->per_recv_io_ctx->overlapped), 
                             NULL);
        }
        if (SOCKET_ERROR == result) {
            if (WSAGetLastError() != WSA_IO_PENDING) {
                int last_err = WSAGetLastError();
                BuildErrMsgString(last_err);
                shutdown(client_ctx->socket, SD_BOTH);
                if (0 != closesocket(client_ctx->socket)) {
                    DBG_ELOG("sock=" << client_ctx->socket << ",close socket error! : " 
                             << last_err);
                }
                client_ctx->socket = INVALID_SOCKET;
                DBG_ELOG("sock=" << client_ctx->sock_id_copy << ", " <<err_msg_ 
                         << ", recv_ref_cnt =" << client_ctx->recv_ref_cnt);
                return false;
            }
        }
        client_ctx->recv_ref_cnt++;
    #ifdef DEBUG_PRINT
        client_ctx->GetBuffer()->DebugPos(client_ctx->sock_id_copy);
    #endif
        return true;
    }

private:
    //-----------------------------------------------------
    size_t  OnCalculateDataLen(Context* ctx_ptr) {
        DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket <<
                " / capacity : " << ctx_ptr->GetBuffer()->GetCapacity() <<
                " / total free : " << ctx_ptr->GetBuffer()->GetTotalFreeSpace() <<
                " / linear free : " << ctx_ptr->GetBuffer()->GetLinearFreeSpace() <<
                " / cumulated : " << ctx_ptr->GetBuffer()->GetCumulatedLen());

        if (ctx_ptr->GetBuffer()->GetCumulatedLen() < (int)HEADER_SIZE) {
            DBG_LOG("more to come");
            return asock::MORE_TO_COME; //more to come 
        }
        ST_HEADER header;
        ctx_ptr->GetBuffer()->PeekData(HEADER_SIZE, (char*)&header);
        size_t supposed_total_len = std::atoi(header.msg_len) + HEADER_SIZE;
        if (supposed_total_len > ctx_ptr->GetBuffer()->GetCapacity()) {
            DBG_RED_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket <<
                        " / packet length : " << supposed_total_len <<
                        " / buffer is insufficient => increase buffer");
            // If the size of the data to be received is larger than the buffer, 
            // increase the buffer capacity.
            size_t new_buffer_len= supposed_total_len * 2;
            if(!ctx_ptr->GetBuffer()->IncreaseBufferAndCopyExisting(new_buffer_len)){
                ELOG(ctx_ptr->GetBuffer()->GetErrMsg());
                exit(EXIT_FAILURE);
            }
            SetBufferCapacity(new_buffer_len);

            DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket <<
                    " / capacity : " << ctx_ptr->GetBuffer()->GetCapacity() <<
                    " / total free : " << ctx_ptr->GetBuffer()->GetTotalFreeSpace() <<
                    " / linear free : " << ctx_ptr->GetBuffer()->GetLinearFreeSpace() <<
                    " / cumulated : " << ctx_ptr->GetBuffer()->GetCumulatedLen());
        };
        DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket <<
                " / packet length : " << supposed_total_len);
        return supposed_total_len;
    }

    virtual bool    OnRecvedCompleteData(Context* ctx_ptr, 
                                         char* data_ptr, size_t size_t) {
        std::cerr << "ERROR! OnRecvedCompleteData not implemented!\n";
        return false;
    }

    //---------------------------------------------------------    
    // CLIENT Usage
    //---------------------------------------------------------    
public :
    //-------------------------------------------------------------------------
    bool InitTcpClient(const char* server_ip,
                       unsigned short  server_port,
                       int         connect_timeout_secs = 10,
                       size_t  max_data_len = asock::DEFAULT_BUFFER_SIZE) {
        sock_usage_  = SOCK_USAGE_TCP_CLIENT;
        server_ip_   = server_ip ;
        server_port_ = server_port;
        is_connected_ = false;
        if(!InitWinsock()) {
            return false;
        }
        connect_timeout_secs_ = connect_timeout_secs;
        if (!SetBufferCapacity(max_data_len)) {
            return false;
        }
        if (context_.per_recv_io_ctx != NULL) {
            delete context_.per_recv_io_ctx;
        }
        context_.per_recv_io_ctx = new (std::nothrow) PER_IO_DATA;
        if (context_.per_recv_io_ctx == nullptr) {
            DBG_ELOG("mem alloc failed");
            return false;
        }
        context_.socket = socket(AF_INET, SOCK_STREAM, 0);
        memset((void *)&tcp_server_addr_, 0x00, sizeof(tcp_server_addr_));
        tcp_server_addr_.sin_family = AF_INET;
        inet_pton(AF_INET, server_ip_.c_str(), &(tcp_server_addr_.sin_addr));
        tcp_server_addr_.sin_port = htons(server_port_);
        return ConnectToServer();
    }

    //-------------------------------------------------------------------------
    bool InitUdpClient(const char* server_ip,
                       unsigned short  server_port,
                       size_t  max_data_len = asock::DEFAULT_BUFFER_SIZE) {
        sock_usage_ = SOCK_USAGE_UDP_CLIENT  ;
        server_ip_   = server_ip ;
        server_port_ = server_port;
        is_connected_ = false;
        if(!InitWinsock()) {
            return false;
        }
        if(!SetBufferCapacity(max_data_len) ) {
            return false;
        }
        if (context_.per_recv_io_ctx != NULL) {
            delete context_.per_recv_io_ctx;
        }
        context_.per_recv_io_ctx = new (std::nothrow) PER_IO_DATA;
        if (context_.per_recv_io_ctx == nullptr) {
            DBG_ELOG("mem alloc failed");
            return false;
        }
        context_.socket = socket(AF_INET, SOCK_DGRAM, 0);
        memset((void *)&udp_server_addr_, 0x00, sizeof(udp_server_addr_));
        udp_server_addr_.sin_family = AF_INET;
        inet_pton(AF_INET, server_ip_.c_str(), &(udp_server_addr_.sin_addr));
        udp_server_addr_.sin_port = htons(server_port_);
        return ConnectToServer();
    }

    //-------------------------------------------------------------------------
    bool SendToServer(const char* data, size_t len) {
        std::lock_guard<std::mutex> lock(context_.ctx_lock); // XXX lock
        size_t total_sent = 0;           
        size_t total_len = len + HEADER_SIZE;
        if ( !is_connected_ ) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "not connected";
            return false;
        }
        if (context_.socket == INVALID_SOCKET) {
            err_msg_ = "not connected";
            return false;
        }

        ST_HEADER header;
        snprintf(header.msg_len, sizeof(header.msg_len), "%d", (int)len);

        //if sent is pending, just push to queue. 
        if(context_.is_sent_pending){
            DBG_LOG("pending. just queue.");
            PENDING_SENT pending_sent;
            pending_sent.pending_sent_data = new char [len + HEADER_SIZE]; 
            //pending_sent.pending_sent_data = new char [total_len]; // ? C6386 warn ?
            pending_sent.pending_sent_len  = total_len;
            memcpy(pending_sent.pending_sent_data, (char*)&header, HEADER_SIZE);
            memcpy(pending_sent.pending_sent_data+HEADER_SIZE, data, len);
            context_.pending_send_deque.push_back(pending_sent);
            return true;
        }

        char* data_position_ptr = new char[len + HEADER_SIZE];// XXX TODO 
        memcpy(data_position_ptr, (char*)&header, HEADER_SIZE);
        memcpy(data_position_ptr + HEADER_SIZE, data, len);

        while( total_sent < total_len ) {
            int sent_len =0;
            size_t remained_len = total_len - total_sent;
            if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
                //XXX UDP : all or nothing. no partial sent!
                sent_len = sendto(context_.socket, data_position_ptr, int(remained_len), 0,
                                  (SOCKADDR*)&udp_server_addr_, 
                                  int(sizeof(udp_server_addr_)));
            } else {
                sent_len = send(context_.socket, data_position_ptr, 
                                (int)(remained_len), 0);
            }

            if(sent_len > 0) {
                total_sent += sent_len ;  
                data_position_ptr += sent_len ;      
            } else if( sent_len == SOCKET_ERROR ) {
                if (WSAEWOULDBLOCK == WSAGetLastError()) {
                    //send later
                    PENDING_SENT pending_sent;
                    pending_sent.pending_sent_data = new char [remained_len]; 
                    pending_sent.pending_sent_len = remained_len;
                    memcpy(pending_sent.pending_sent_data,data_position_ptr, remained_len);
                    context_.pending_send_deque.push_back(pending_sent);
                    context_.is_sent_pending = true;
                    return true;
                } else {
                    BuildErrMsgString(WSAGetLastError());
                    shutdown(context_.socket, SD_BOTH);
                    if (0 != closesocket(context_.socket)) {
                        DBG_ELOG("close socket error! : " << WSAGetLastError());
                    }
                    context_.socket = INVALID_SOCKET;
                    return false;
                }
            }
        }//while
        return true;
    }

    //-------------------------------------------------------------------------
    SOCKET_T  GetSocket () { return  context_.socket ; }
    //-------------------------------------------------------------------------
    bool IsConnected() { 
        if (is_connected_ && is_client_thread_running_) {
            return true;
        }
        return false;
    }
    //-------------------------------------------------------------------------
    void Disconnect() {
        DBG_LOG("Disconnect");
        is_connected_ = false;
        if(context_.socket != INVALID_SOCKET ) {
            shutdown(context_.socket, SD_BOTH);
            closesocket(context_.socket);
        }
        context_.socket = INVALID_SOCKET;
        //wait thread exit
        if (client_thread_.joinable()) {
            client_thread_.join();
        }
    }

private :
    int         connect_timeout_secs_    ;
    bool        is_buffer_init_ {false};
    std::atomic<bool>    is_connected_   {false};
    Context     context_;
    SOCKADDR_IN tcp_server_addr_ ;
    SOCKADDR_IN udp_server_addr_ ;
    std::thread client_thread_;
    std::atomic<bool> is_client_thread_running_ {false};

    //-------------------------------------------------------------------------
    bool RunServer() {
        DBG_LOG( "Run Server..."); 
        if(is_server_running_ ) { 
            err_msg_ = "error [server is already running]";
            return false;
        }
        if(!InitWinsock()) {
            return false;
        }
        handle_completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if(max_data_len_ <0) {
            err_msg_ = "init error [packet length is negative]";
            return  false;
        }
        //--------------------------
        StartWorkerThreads();
        //--------------------------

        is_need_server_run_ = true;
        if (!StartServer()) {
            return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    bool ConnectToServer() {
        if(is_connected_ ){
            return true;
        }
        if (!SetSocketNonBlocking(context_.socket)) {
            closesocket(context_.socket);
            return  false;
        }
        if (!is_buffer_init_) {
            if (cumbuffer::OP_RSLT_OK != context_.per_recv_io_ctx->cum_buffer.Init(max_data_len_)) {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = std::string("cumBuffer Init error :") + 
                            context_.per_recv_io_ctx->cum_buffer.GetErrMsg();
                closesocket(context_.socket);
                context_.socket = INVALID_SOCKET;
                return false;
            }
            is_buffer_init_ = true;
        } else {
            //in case of reconnect
            context_.per_recv_io_ctx->cum_buffer.ReSet();
        }
        struct timeval timeout_val;
        timeout_val.tv_sec  = connect_timeout_secs_;
        timeout_val.tv_usec = 0;

        int result = -1;
        if ( sock_usage_ == SOCK_USAGE_IPC_CLIENT ) {
            //TODO
        } else if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT ) {
            result = connect(context_.socket, 
                             (SOCKADDR*)&tcp_server_addr_, 
                             (SOCKLEN_T)sizeof(SOCKADDR_IN));
        } else if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
            if(!SetSockoptSndRcvBufUdp(context_.socket)) {
                closesocket(context_.socket);
                return false;
            }
            result = connect(context_.socket,
                             (SOCKADDR*)&udp_server_addr_,
                             (SOCKLEN_T)sizeof(SOCKADDR_IN)) ;
        } else {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "invalid socket usage" ;
            closesocket(context_.socket);
            return false;
        }

        if ( result == SOCKET_ERROR) {
            int last_err = WSAGetLastError();
            if (last_err != WSAEWOULDBLOCK) {
                BuildErrMsgString(last_err);
                return false;
            }
        }
        if (result == 0) {
            is_connected_ = true;
            return RunClientThread();
        }
        WSASetLastError(0);
        //wait for connected
        int wait_timeout_ms = connect_timeout_secs_ * 1000 ;
        WSAPOLLFD fdArray = { 0 };
        fdArray.fd = context_.socket;
        fdArray.events = POLLWRNORM;
        result = WSAPoll(&fdArray, 1, wait_timeout_ms);
        DBG_LOG("connect WSAPoll returns....: result ="<<result);
        if (SOCKET_ERROR == result) {
            BuildErrMsgString(WSAGetLastError());
            return false;
        }
        if (result) {
            if (fdArray.revents & POLLWRNORM) {
                DBG_LOG("Established connection");
            } else {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = "connect timeout";
                return false;
            }
        }
        is_connected_ = true;
        return RunClientThread();
    }

    //-------------------------------------------------------------------------
    bool RunClientThread() {
        if (!is_client_thread_running_) {

            client_thread_ = std::thread (&ASock::ClientThreadRoutine, this);
            //client_thread.detach();
            is_client_thread_running_ = true;        
        }
        return true;
    }

    //-------------------------------------------------------------------------
    void ClientThreadRoutine() {
        is_client_thread_running_ = true;
        int wait_timeout_ms =  10 ;
        while (is_connected_) { 
            WSAPOLLFD fdArray = { 0 };
            if (context_.socket == INVALID_SOCKET) {
                DBG_ELOG( "INVALID_SOCKET" ) ;
                is_client_thread_running_ = false;
                return;
            }
            fdArray.fd = context_.socket;
            fdArray.revents = 0;
            int result = -1;
            //================================== 
            //fdArray.events = POLLRDNORM | POLLWRNORM; // XXX 이런식으로 하면 안됨? 
            fdArray.events = POLLRDNORM ;
            result = WSAPoll(&fdArray, 1, wait_timeout_ms);
            if (SOCKET_ERROR == result) {
                is_client_thread_running_ = false;
                if (!is_connected_) { 
                    return;//no error.
                }
                BuildErrMsgString(WSAGetLastError());
                return ;
            }
            if (result == 0) {
                //timeout
            }else{
                if (!is_connected_) {
                    is_client_thread_running_ = false;
                    return;//no error.
                }
                if (fdArray.revents & POLLRDNORM) {  
                    //============================
                    size_t want_recv_len = max_data_len_;
                    //-------------------------------------
                    size_t free_linear_len = context_.GetBuffer()->GetLinearFreeSpace();
                    if (max_data_len_ > free_linear_len) { 
                        want_recv_len = free_linear_len;
                    }
                    if (want_recv_len == 0) {
                        std::lock_guard<std::mutex> lock(err_msg_lock_);
                        err_msg_ = "no linear free space left ";
                        DBG_ELOG(err_msg_);
                        is_client_thread_running_ = false;
                        return ;
                    }
                    //============================
                    if (context_.socket == INVALID_SOCKET) {
                        ELOG("INVALID_SOCKET");
                        is_client_thread_running_ = false;
                        return;
                    }
                    int ret = -1;
                    if (sock_usage_ == SOCK_USAGE_UDP_CLIENT) {
                        SOCKLEN_T addrlen = sizeof(context_.udp_remote_addr);
                        ret = recvfrom(context_.socket, //--> is listen_socket_
                                        context_.GetBuffer()->GetLinearAppendPtr(),
                                        int(max_data_len_),
                                        0,
                                        (struct sockaddr*)&context_.udp_remote_addr,
                                        &addrlen);
                    } else {
                        ret = recv(context_.socket, 
                                   context_.GetBuffer()->GetLinearAppendPtr(), 
                                   (int) want_recv_len, 0);
                    }
                    if (SOCKET_ERROR == ret) {
                        BuildErrMsgString(WSAGetLastError());
                        ELOG( err_msg_ ) ;
                        is_client_thread_running_ = false;
                        return;
                    } else {
                        DBG_LOG("client recved : " << ret <<" bytes");
                        if (ret == 0) {
                            DBG_LOG("disconnected : sock=" << context_.socket);
                            shutdown(context_.socket, SD_BOTH);
                            closesocket(context_.socket);
                            is_connected_ = false;
                            OnDisconnectedFromServer();
                            context_.socket = INVALID_SOCKET; //XXX
                            is_client_thread_running_ = false;
                            return;
                        }
                        bool isOk = false;
                        if (sock_usage_ == SOCK_USAGE_UDP_CLIENT) {
                            isOk = RecvfromData(0, &context_, ret);
                        } else {
                            isOk = RecvData(0, &context_, ret);
                        }
                        if (!isOk) {
                            DBG_ELOG(err_msg_);
                            shutdown(context_.socket, SD_BOTH);
                            if (0 != closesocket(context_.socket)) {
                                DBG_ELOG("close socket error! : " << WSAGetLastError());
                            }
                            is_connected_ = false;
                            context_.socket = INVALID_SOCKET;
                            OnDisconnectedFromServer();
                            is_client_thread_running_ = false;
                            return;
                        }
                    }
                } else if (fdArray.revents & POLLHUP) {
                    LOG("POLLHUP");
                    shutdown(context_.socket, SD_BOTH);
                    if (0 != closesocket(context_.socket)) {
                        DBG_ELOG("close socket error! : " << WSAGetLastError());
                    }
                    is_connected_ = false;
                    context_.socket = INVALID_SOCKET;
                    OnDisconnectedFromServer();
                    is_client_thread_running_ = false;
                    return;
                }
            }
            //================================== send pending if any
            fdArray.fd = context_.socket;
            fdArray.revents = 0;
            fdArray.events = POLLWRNORM;
            result = WSAPoll(&fdArray, 1, 0);
            if (SOCKET_ERROR == result) {
                is_client_thread_running_ = false;
                if (!is_connected_) { 
                    return;//no error.
                }
                BuildErrMsgString(WSAGetLastError());
                ELOG( err_msg_ ) ;
                return ;
            }
            if (result == 0) {
                //timeout
            } else{
                 if (fdArray.revents & POLLWRNORM) {
                    if (!SendPendingData()) {
                        is_client_thread_running_ = false;
                        return; //error!
                    }
                }
            }
            //================================== 
        } //while(true)
        is_client_thread_running_ = false;
    }

    //-------------------------------------------------------------------------
    void InvokeServerDisconnectedHandler() {
        if (cb_on_disconnected_from_server_ != nullptr) {
            cb_on_disconnected_from_server_();
        } else {
            OnDisconnectedFromServer();
        }
    }

    //-------------------------------------------------------------------------
    std::function<void()> cb_on_disconnected_from_server_ {nullptr} ;
    //for inheritance : Implement these virtual functions.
    virtual void  OnDisconnectedFromServer() {}; 

    //for composition : Assign yours to these callbacks 
public :
    //-------------------------------------------------------------------------
    void TerminateClient(Context* ctx_ptr, bool is_graceful = true) {
        client_cnt_--;
        DBG_LOG(" sock=" << ctx_ptr->sock_id_copy
                << ", send_ref_cnt=" << ctx_ptr->send_ref_cnt
                << ", recv_ref_cnt=" << ctx_ptr->recv_ref_cnt
                << ", connection closing ,graceful="
                << (is_graceful ? "TRUE" : "FALSE"));
        ctx_ptr->is_connected = false;
        if (cb_on_client_connected_ != nullptr) {
            cb_on_client_disconnected_(ctx_ptr);
        } else {
            OnClientDisconnected(ctx_ptr);
        }
        if(ctx_ptr->socket == INVALID_SOCKET){
            DBG_LOG(" sock=" << ctx_ptr->sock_id_copy << ", already closed");
            PushClientContextToCache(ctx_ptr);
        } else {
            if (!is_graceful) {
                // force the subsequent closesocket to be abortative.
                LINGER  linger_struct;
                linger_struct.l_onoff = 1;
                linger_struct.l_linger = 0;
                setsockopt(ctx_ptr->socket, 
                           SOL_SOCKET, SO_LINGER, 
                           (char*)&linger_struct, 
                           sizeof(linger_struct));
            }
            DBG_LOG("close socket : " << ctx_ptr->socket  
                    << ", send_ref_cnt = " << ctx_ptr->send_ref_cnt 
                    << ", recv_ref_cnt = " << ctx_ptr->recv_ref_cnt );
            shutdown(ctx_ptr->socket, SD_BOTH);
            if (0 != closesocket(ctx_ptr->socket)) {
                DBG_ELOG("sock=" << ctx_ptr->socket 
                         << ",close socket error! : " << WSAGetLastError());
            }
            ctx_ptr->socket = INVALID_SOCKET; 
            PushClientContextToCache(ctx_ptr);
        }
    }

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

    //-------------------------------------------------------------------------
    bool  RunTcpServer(const char* bind_ip,
                        int         bind_port,
                        size_t  max_data_len = asock::DEFAULT_BUFFER_SIZE,
                        size_t  max_client = asock::DEFAULT_MAX_CLIENT) {
        sock_usage_ = SOCK_USAGE_TCP_SERVER;
        server_ip_ = bind_ip;
        server_port_ = bind_port;
        max_client_limit_ = max_client;
        if (max_client_limit_ < 0) {
            return false;
        }
        if (!SetBufferCapacity(max_data_len)) {
            return false;
        }
        return RunServer();
    }

    //-------------------------------------------------------------------------
    bool  RunUdpServer(const char* bind_ip,
                        size_t  bind_port,
                        size_t  max_data_len = asock::DEFAULT_BUFFER_SIZE,
                        size_t  max_client = asock::DEFAULT_MAX_CLIENT) {
        sock_usage_ = SOCK_USAGE_UDP_SERVER  ;
        server_ip_ = bind_ip ; 
        server_port_ = int(bind_port) ; 
        max_client_limit_ = max_client ; 
        if(max_client_limit_<0) {
            return false;
        }
        if(!SetBufferCapacity(max_data_len)) {
            return false;
        }
        return RunServer();
    }

    //-------------------------------------------------------------------------
    bool  IsServerRunning(){return is_server_running_;};
    //-------------------------------------------------------------------------
    void  StopServer() {
        is_need_server_run_ = false;
        for (size_t i = 0; i < max_worker_cnt_; i++) {
            //DBG_LOG("PostQueuedCompletionStatus");
            DWORD       bytes_transferred = 0;
            Context* ctx_ptr = PopClientContextFromCache();
            ctx_ptr->per_recv_io_ctx = PopPerIoDataFromCache();
            if (nullptr == ctx_ptr->per_recv_io_ctx) {
                ELOG("memory alloc failed");
                return;
            }
            SecureZeroMemory((PVOID)&ctx_ptr->per_recv_io_ctx->overlapped, 
                             sizeof(WSAOVERLAPPED));
            ctx_ptr->per_recv_io_ctx->io_type = EnumIOType::IO_QUIT;
            if (!PostQueuedCompletionStatus(handle_completion_port_, 
                                            bytes_transferred,
                                            (ULONG_PTR)ctx_ptr,
                                            (LPOVERLAPPED) & (ctx_ptr->per_recv_io_ctx->overlapped))) {
                BuildErrMsgString(WSAGetLastError());
                ELOG(err_msg_);
            }
        }
        if (sock_usage_ == SOCK_USAGE_TCP_SERVER) {
            closesocket(listen_socket_);
        }
    }

    //-------------------------------------------------------------------------
    size_t  GetMaxClientLimit(){return max_client_limit_ ; }
    //-------------------------------------------------------------------------
    int   GetCountOfClients(){ return client_cnt_ ; }
    //-------------------------------------------------------------------------
    size_t  GetCountOfContextQueue(){ 
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        return queue_ctx_cache_.size(); 
    }

private :
    std::string       server_ip_   ;
    std::string       server_ipc_socket_path_ ;
    int               server_port_ {-1};
    std::atomic<int>  client_cnt_ {0}; 
    std::atomic<bool> is_need_server_run_ {true};
    std::atomic<bool> is_server_running_  {false};
    SOCKET_T          listen_socket_     ;
    size_t            max_client_limit_  {0};
    int               max_worker_cnt_{ 0 };
    std::atomic<int>  cur_quit_cnt_{0};
    std::queue<Context*> queue_ctx_cache_;
    std::mutex           ctx_cache_lock_ ; 
    //bool use_zero_byte_receive_{ false };
    std::queue<PER_IO_DATA*> queue_per_io_data_cache_;
    std::mutex           per_io_data_cache_lock_ ; 

    //-------------------------------------------------------------------------
    bool BuildClientContextCache() {
        //DBG_LOG("queue_client_cache alloc ");
        Context* ctx_ptr = nullptr;
        for (int i = 0; i < max_client_limit_; i++) {
            ctx_ptr = new (std::nothrow) Context();
            if (nullptr == ctx_ptr) {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = "Context alloc failed !";
                return false;
            }
            ctx_ptr->per_recv_io_ctx = new (std::nothrow) PER_IO_DATA;
            if (nullptr == ctx_ptr->per_recv_io_ctx) {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = "per_io_ctx alloc failed !";
                delete ctx_ptr;
                return false;
            }
            if (cumbuffer::OP_RSLT_OK != ctx_ptr->per_recv_io_ctx->cum_buffer.Init(max_data_len_)) {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = std::string("cumBuffer(recv) Init error : ") +
                    std::string(ctx_ptr->per_recv_io_ctx->cum_buffer.GetErrMsg());
                delete ctx_ptr->per_recv_io_ctx;
                delete ctx_ptr;
                return false;
            }
            ctx_ptr->pending_send_deque.clear();
            ReSetCtxPtr(ctx_ptr);
            queue_ctx_cache_.push(ctx_ptr);
        }
        DBG_LOG("current queue_ctx_cache_ size =" << queue_ctx_cache_.size());
        return true;
    }
    //-------------------------------------------------------------------------
    void PushClientContextToCache(Context* ctx_ptr) {
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        {
            //reset all
            DBG_LOG("sock=" << ctx_ptr->sock_id_copy);
            ReSetCtxPtr(ctx_ptr);
        }
        queue_ctx_cache_.push(ctx_ptr);
        DBG_LOG("queue ctx cache size = " << queue_ctx_cache_.size());
    }

    //-------------------------------------------------------------------------
    void ClearClientCache() {
        DBG_LOG("======= clear all cache ========");
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        while (!queue_ctx_cache_.empty()) {
            Context* ctx_ptr = queue_ctx_cache_.front();
            while(!ctx_ptr->pending_send_deque.empty() ) {
                PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
                delete [] pending_sent.pending_sent_data;
                ctx_ptr->pending_send_deque.pop_front();
            }
            delete ctx_ptr;
            queue_ctx_cache_.pop();
        }
    }

    //-------------------------------------------------------------------------
    bool AcceptNewClient();
    //-------------------------------------------------------------------------
    Context* PopClientContextFromCache() {
        Context* ctx_ptr = nullptr;
        { //lock scope
            std::lock_guard<std::mutex> lock(ctx_cache_lock_);
            if (!queue_ctx_cache_.empty()) {
                ctx_ptr = queue_ctx_cache_.front();
                queue_ctx_cache_.pop(); 
                //DBG_LOG("queue_ctx_cache not empty! -> " << "queue ctx cache size = " << queue_ctx_cache_.size());
                ReSetCtxPtr(ctx_ptr);
                return ctx_ptr;
            }
        }
        //alloc new !!!
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        if (!BuildClientContextCache()) {
            return nullptr;
        }
        Context* rtn_ctx_ptr = queue_ctx_cache_.front();
        queue_ctx_cache_.pop();
        ReSetCtxPtr(rtn_ctx_ptr);
        return rtn_ctx_ptr ;
    }

    //-------------------------------------------------------------------------
    bool BuildPerIoDataCache() {
        //DBG_LOG("queue_per_io_data_cache_ alloc ");
        PER_IO_DATA* per_io_data_ptr = nullptr;
        for (int i = 0; i < max_client_limit_ * 2; i++) { // 최대 예상 client 의 2배로 시작
            per_io_data_ptr = new (std::nothrow) PER_IO_DATA;
            if (nullptr == per_io_data_ptr) {
                std::lock_guard<std::mutex> lock(err_msg_lock_);
                err_msg_ = "per_io_data alloc failed !";
                ELOG(err_msg_);
                return false;
            }
            // send 에서만 사용되므로 현재 cumbuffer 불필요.
            per_io_data_ptr->wsabuf.buf = NULL;
            per_io_data_ptr->wsabuf.len = 0 ;
            per_io_data_ptr->io_type = EnumIOType::IO_UNKNOWN;
            per_io_data_ptr->total_send_len = 0;
            per_io_data_ptr->complete_recv_len = 0;
            per_io_data_ptr->sent_len = 0;
            per_io_data_ptr->is_packet_len_calculated = false;
            SecureZeroMemory((PVOID)&per_io_data_ptr->overlapped, sizeof(WSAOVERLAPPED));
            queue_per_io_data_cache_.push(per_io_data_ptr);
        }
        DBG_LOG("current queue_per_io_data_cache_ size =" << queue_per_io_data_cache_.size());
        return true;
    }
    //-------------------------------------------------------------------------
    PER_IO_DATA* PopPerIoDataFromCache() {
        PER_IO_DATA* per_io_data_ptr = nullptr;
        std::lock_guard<std::mutex> lock(per_io_data_cache_lock_);
        if (!queue_per_io_data_cache_.empty()) {
            per_io_data_ptr = queue_per_io_data_cache_.front();
            queue_per_io_data_cache_.pop();
            //DBG_LOG("queue_per_io_data_cache_ not empty! -> " << "queue_per_io_data_cache_ client cache size = " << queue_per_io_data_cache_.size());
            return per_io_data_ptr;
        }
        //alloc new !!!
        if (!BuildPerIoDataCache()) {
            return nullptr;
        }
        PER_IO_DATA* rtn_per_io_data_ptr = queue_per_io_data_cache_.front();
        queue_per_io_data_cache_.pop();
        return rtn_per_io_data_ptr;
    }

    //-------------------------------------------------------------------------
    void PushPerIoDataToCache(PER_IO_DATA* per_io_data_ptr) {
        std::lock_guard<std::mutex> lock(per_io_data_cache_lock_);
        delete [] per_io_data_ptr->wsabuf.buf;
        SecureZeroMemory((PVOID)&per_io_data_ptr->overlapped, sizeof(WSAOVERLAPPED));
        per_io_data_ptr->wsabuf.buf = NULL;
        per_io_data_ptr->wsabuf.len = 0;
        per_io_data_ptr->io_type = EnumIOType::IO_UNKNOWN;
        per_io_data_ptr->total_send_len = 0;
        per_io_data_ptr->complete_recv_len = 0;
        per_io_data_ptr->sent_len = 0;
        //per_io_data_ptr->cum_buffer.ReSet(); //현재 send 에서만 사용하므로 불필요
        queue_per_io_data_cache_.push(per_io_data_ptr);
        DBG_LOG("queue_per_io_data_cache_ client cache size = " 
                << queue_per_io_data_cache_.size());
    }

    //-------------------------------------------------------------------------
    void ClearPerIoDataCache() {
        DBG_LOG("queue_per_io_data_cache_ size =" << queue_per_io_data_cache_.size());
        std::lock_guard<std::mutex> lock(per_io_data_cache_lock_);
        while (!queue_per_io_data_cache_.empty()) {
            PER_IO_DATA* per_io_data_ptr = queue_per_io_data_cache_.front();
            if (per_io_data_ptr->wsabuf.buf != NULL) {
                delete [] per_io_data_ptr->wsabuf.buf;
            }
            delete per_io_data_ptr;
            queue_per_io_data_cache_.pop();
        }
    }
    //-------------------------------------------------------------------------
    void AcceptThreadRoutine() {
        SOCKLEN_T socklen=sizeof(SOCKADDR_IN);
        SOCKADDR_IN client_addr;
        while(is_need_server_run_) {
            SOCKET_T client_sock = WSAAccept(listen_socket_, 
                                             (SOCKADDR*)&client_addr, 
                                             &socklen, 0, 0);
            if (client_sock == INVALID_SOCKET) {
                int last_err = WSAGetLastError();
                if (WSAEWOULDBLOCK == last_err) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                BuildErrMsgString(last_err);
                if (!is_need_server_run_) {
                    ELOG(err_msg_);
                    exit(EXIT_FAILURE);
                } else {
                    break;
                }
            }
            client_cnt_++;
            SetSocketNonBlocking(client_sock);
            Context* ctx_ptr = PopClientContextFromCache();
            if (ctx_ptr == nullptr) {
                ELOG(err_msg_);
                exit(EXIT_FAILURE);
            }
            ctx_ptr->is_connected = true;
            ctx_ptr->socket = client_sock; 
            ctx_ptr->sock_id_copy = client_sock; //for debuggin
            handle_completion_port_ = CreateIoCompletionPort((HANDLE)client_sock, 
                                                             handle_completion_port_, 
                                                             (ULONG_PTR)ctx_ptr, 0); 
            if (NULL == handle_completion_port_) {
                BuildErrMsgString(WSAGetLastError());
                ELOG(err_msg_ );
                delete ctx_ptr;
                exit(EXIT_FAILURE);
            }
            // XXX iocp 초기화 완료후 사용자의 콜백을 호출해야 함. 
            // 사용자가 콜백에서 send를 시도할수 있기 때문.
            DBG_LOG("**** accept returns...new client : sock="<<client_sock 
                    << " => client total = "<< client_cnt_ );
            if (cb_on_client_connected_ != nullptr) {
                cb_on_client_connected_(ctx_ptr);
            } else {
                OnClientConnected(ctx_ptr);
            }
            // Start receiving.
            if (!IssueRecv(0, ctx_ptr)) {  // worker_index is not in use
                client_cnt_--;
                DBG_ELOG("sock=" << ctx_ptr->socket <<  ", error! : " << err_msg_);
                PushClientContextToCache(ctx_ptr);
                continue;
            }
        } //while
        DBG_LOG("accept thread exiting");
    }

    //-------------------------------------------------------------------------
    void        UdpServerThreadRoutine();
    //-------------------------------------------------------------------------
    //io 작업 결과에 대해 처리한다. --> multi thread 에 의해 수행된다 !!!
    void WorkerThreadRoutine(size_t worker_index) {
        DWORD       bytes_transferred = 0;
        Context*    ctx_ptr = NULL ;
        LPWSAOVERLAPPED lp_overlapped = NULL;
        PER_IO_DATA*    per_io_ctx = NULL;
        while (true) {
            bytes_transferred = 0;
            ctx_ptr = NULL ;
            lp_overlapped = NULL;
            per_io_ctx = NULL;
            // wsarecv 를 호출한 경우,
            // client 가 보내거나 연결을 종료할때만 GetQueuedCompletionStatus 로 통지된다.
            if (FALSE == GetQueuedCompletionStatus(handle_completion_port_, 
                                                   &bytes_transferred, 
                                                   (PULONG_PTR) & ctx_ptr, 
                                                   (LPOVERLAPPED*) &lp_overlapped, INFINITE )) {
                int err = WSAGetLastError();
                if (err == ERROR_ABANDONED_WAIT_0 || err== ERROR_INVALID_HANDLE) {
                    DBG_LOG("ERROR_ABANDONED_WAIT_0");
                    return;
                }
                BuildErrMsgString(err);
                if (lp_overlapped != NULL) {
                    //===================== lock
                    DBG_ELOG("worker=" << worker_index << ", sock=" 
                             << ctx_ptr->sock_id_copy 
                             << ", GetQueuedCompletionStatus failed.."
                             << ", bytes = " << bytes_transferred << ", err=" << err );
                    per_io_ctx = (PER_IO_DATA*)lp_overlapped;
                    if (per_io_ctx->io_type == EnumIOType::IO_SEND) {
                        ctx_ptr->send_ref_cnt--;
                        PushPerIoDataToCache(per_io_ctx); //XXX per_io_ctx => 동적 할당된 메모리
                    } else if (per_io_ctx->io_type == EnumIOType::IO_RECV) {
                        ctx_ptr->recv_ref_cnt--;
                    }
                    if (bytes_transferred == 0) {
                        //graceful disconnect.
                        DBG_LOG("sock="<<ctx_ptr->sock_id_copy 
                                << ", 0 recved --> gracefully disconnected : " 
                                << ctx_ptr->recv_ref_cnt);
                        if (per_io_ctx->io_type == EnumIOType::IO_RECV) {
                            if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                                TerminateClient(ctx_ptr);
                            }
                        } else if (per_io_ctx->io_type == EnumIOType::IO_SEND) {
                            TerminateClient(ctx_ptr);
                        }
                    } else {
                        if (err == ERROR_NETNAME_DELETED) { // --> 64
                            //client hard close --> not an error
                            DBG_LOG("worker=" << worker_index << ",sock=" 
                                    << ctx_ptr->sock_id_copy 
                                    << " : client hard close. ERROR_NETNAME_DELETED ");
                        } else {
                            //error 
                            DBG_ELOG(" GetQueuedCompletionStatus failed  :" << err_msg_);
                        }
                        if (per_io_ctx->io_type == EnumIOType::IO_RECV) {
                            if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                                TerminateClient(ctx_ptr, false); //force close
                            }
                        } else if (per_io_ctx->io_type == EnumIOType::IO_SEND) {
                            TerminateClient(ctx_ptr, false); //force close
                        }
                    }
                } else {
                    DBG_ELOG("GetQueuedCompletionStatus failed..err="<< err);
                    if (per_io_ctx->io_type == EnumIOType::IO_RECV) {
                        if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                            TerminateClient(ctx_ptr, false); //force close
                        }
                    } else if (per_io_ctx->io_type == EnumIOType::IO_SEND) {
                        TerminateClient(ctx_ptr, false); //force close
                    }
                }
                continue;
            }
            //-----------------------------------------
            per_io_ctx = (PER_IO_DATA*)lp_overlapped; 
            //-----------------------------------------
            switch (per_io_ctx->io_type) {
            //case EnumIOType::IO_ACCEPT:
            //    break;
            //======================================================= IO_RECV
            case EnumIOType::IO_RECV:
            {
                ctx_ptr->recv_ref_cnt--;
                DBG_LOG("worker=" << worker_index << ",IO_RECV: sock=" 
                        << ctx_ptr->sock_id_copy << ", recv_ref_cnt =" 
                        << ctx_ptr->recv_ref_cnt << ", recved bytes =" 
                        << bytes_transferred);
                //# recv #---------- 
                if (bytes_transferred == 0) {
                    //graceful disconnect.
                    DBG_LOG("0 recved --> gracefully disconnected : " 
                            << ctx_ptr->recv_ref_cnt);
                    if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                        TerminateClient(ctx_ptr);
                    }
                    break;
                }
                bool result = false;
                if (sock_usage_ == SOCK_USAGE_UDP_SERVER || 
                    sock_usage_ == SOCK_USAGE_UDP_CLIENT) {
                    RecvfromData(worker_index, ctx_ptr, bytes_transferred);
                } else {
                    RecvData(worker_index, ctx_ptr, bytes_transferred);
                }
                // Continue to receive messages.
                if (!IssueRecv(worker_index, ctx_ptr)) {  
                    if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                        client_cnt_--;
                        PushClientContextToCache(ctx_ptr);
                    } else {
                        ELOG("issue recv error : recv ref count =" << ctx_ptr->recv_ref_cnt);
                    }
                    break;
                }
            }
            break;
            //======================================================= IO_SEND
            case EnumIOType::IO_SEND:
            {
                ctx_ptr->send_ref_cnt--;
                if (ctx_ptr->socket == INVALID_SOCKET) {
                    DBG_LOG("INVALID_SOCKET : delete.sock=" << ctx_ptr->sock_id_copy 
                            << ", send_ref_cnt= " << ctx_ptr->send_ref_cnt);
                    PushPerIoDataToCache(per_io_ctx);
                    break;
                }
                DBG_LOG("worker=" << worker_index << ",IO_SEND: sock=" 
                        << ctx_ptr->socket << ", send_ref_cnt =" << ctx_ptr->send_ref_cnt);
                //XXX per_io_ctx ==> dynamically allocated memory 
                per_io_ctx->sent_len += bytes_transferred;
                DBG_LOG("IO_SEND(sock=" << ctx_ptr->socket << ") : sent this time =" 
                        << bytes_transferred << ",total sent =" << per_io_ctx->sent_len );

                if (sock_usage_ == SOCK_USAGE_UDP_SERVER) {
                    // udp , no partial send
                    DBG_LOG("socket (" << ctx_ptr->socket 
                            << ") send all completed (" << per_io_ctx->sent_len 
                            << ") ==> delete per io ctx!!");
                    PushPerIoDataToCache(per_io_ctx);
                } else {
                    // non udp
                    if (per_io_ctx->sent_len < per_io_ctx->total_send_len) {
                        //남은 부분 전송
                        DBG_LOG("socket (" << ctx_ptr->socket 
                                << ") send partially completed, total=" 
                                << per_io_ctx->total_send_len
                                << ",partial= " << per_io_ctx->sent_len << ", send again");
                        SecureZeroMemory((PVOID)&per_io_ctx->overlapped, sizeof(WSAOVERLAPPED));
                        DWORD dw_flags = 0;
                        DWORD dw_send_bytes = 0;
                        per_io_ctx->wsabuf.buf += bytes_transferred;
                        per_io_ctx->wsabuf.len -= bytes_transferred;
                        per_io_ctx->io_type = EnumIOType::IO_SEND;
                        //----------------------------------------
                         
                        std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock); 

                        int result = WSASend(ctx_ptr->socket, 
                                             &per_io_ctx->wsabuf, 
                                             1, 
                                             &dw_send_bytes, 
                                             dw_flags, 
                                             &(per_io_ctx->overlapped), NULL);
                        if (result == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
                            client_cnt_--;
                            PushPerIoDataToCache(per_io_ctx);
                            int last_err = WSAGetLastError();
                            BuildErrMsgString(last_err);
                            ELOG("WSASend() failed: " << err_msg_);
                            shutdown(ctx_ptr->socket, SD_BOTH);
                            if (0 != closesocket(ctx_ptr->socket)) {
                                DBG_ELOG("sock=" << ctx_ptr->socket 
                                         << ",close socket error! : " << last_err);
                            }
                            ctx_ptr->socket = INVALID_SOCKET;
                            break;
                        }
                        ctx_ptr->send_ref_cnt++;
                    } else {
                        DBG_LOG("socket (" << ctx_ptr->socket 
                                << ") send all completed (" << per_io_ctx->sent_len 
                                << ") ==> delete per io ctx!!");
                        PushPerIoDataToCache(per_io_ctx);
                    }
                }
            }
            break;
            //======================================================= IO_RECV
            case EnumIOType::IO_QUIT :
            {
                //DBG_LOG( "IO_QUIT "); 
                cur_quit_cnt_++;
                if (max_worker_cnt_ == cur_quit_cnt_) {
                    is_server_running_ = false;
                    DBG_LOG("set is_server_running_ false.");
                }
                return;
            }
            break;
            }//switch
        } //while
    }

public :
    //-------------------------------------------------------------------------
    //for composition : Assign yours to these callbacks 
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
    virtual void    OnClientConnected(Context* ctx_ptr) {}; 
    virtual void    OnClientDisconnected(Context* ctx_ptr) {} ;  
};
} //namaspace


