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

#ifndef ASOCKWINSERVER_HPP
#define ASOCKWINSERVER_HPP

#include <atomic>
#include <thread>
#include <queue>
#include <functional>
#include <chrono>
#include "asock_win_base.hpp"

namespace asock {
////////////////////////////////////////////////////////////////////////////////
class ASockServerBase : public ASockWinBase {
public :
	virtual ~ASockServerBase() {
		ClearClientCache();
		ClearPerIoDataCache();
        if (sock_usage_ == SOCK_USAGE_IPC_SERVER) {
            if (server_ipc_socket_path_.length() > 0) {
                DBG_LOG("unlink :" << server_ipc_socket_path_);
                DeleteFile(server_ipc_socket_path_.c_str());
                server_ipc_socket_path_ = "";
            }
        }
        WSACleanup();
	}

    virtual void SetUsage()=0;
    //-------------------------------------------------------------------------
    bool SendData(Context* ctx_ptr, const char* data_ptr, size_t len) {
        // 서버에서만 사용
        std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock);
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
    bool IsServerRunning(){
        return is_server_running_;
    }
    //-------------------------------------------------------------------------
    void StopServer() {
        DBG_LOG("stop server invoked");
        //terminate worker threads
        for (size_t i = 0; i < max_worker_cnt_; i++) {
            DWORD       bytes_transferred = 0;
            Context* ctx_ptr = PopClientContextFromCache();
            if (nullptr == ctx_ptr->per_recv_io_ctx) {
                ELOG("memory alloc failed");
                return;
            }
            SecureZeroMemory((PVOID)&ctx_ptr->per_recv_io_ctx->overlapped, 
                             sizeof(WSAOVERLAPPED));
            ctx_ptr->per_recv_io_ctx->io_type = EnumIOType::IO_QUIT;
            if (!PostQueuedCompletionStatus(handle_completion_port_, 
                    bytes_transferred, (ULONG_PTR)ctx_ptr,
                    (LPOVERLAPPED) & (ctx_ptr->per_recv_io_ctx->overlapped))) {
                BuildErrMsgString(WSAGetLastError());
            }
        }

        if (sock_usage_ != SOCK_USAGE_UDP_SERVER) {
            is_need_accept_ = false;
            //udp 는 accept thread 기동 안했기때문
            if (accept_thread_.joinable()) {
                accept_thread_.join();
            }
        }

		closesocket(listen_socket_);     // --> 호출 순서 중요!!
        listen_socket_ = INVALID_SOCKET; // --> 호출 순서 중요!!

        while(is_server_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (sock_usage_ == SOCK_USAGE_UDP_SERVER) {
			PushClientContextToCache(udp_server_ctx_ptr);
        }
        if (sock_usage_ == SOCK_USAGE_IPC_SERVER) {
            DBG_LOG("unlink :" << server_ipc_socket_path_);
            DeleteFile(server_ipc_socket_path_.c_str());
            server_ipc_socket_path_ = "";
        }
    }
    //-------------------------------------------------------------------------
    size_t GetMaxEventCount(){
        return max_event_ ; 
    }
    //-------------------------------------------------------------------------
    int GetCountOfClients(){ 
        return client_cnt_ ; 
    }
    //-------------------------------------------------------------------------
    size_t GetCountOfContextQueue(){ 
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        return queue_ctx_cache_.size(); 
    }
    //-------------------------------------------------------------------------
    //for composition : Assign yours to these callbacks 
    bool SetCbOnClientConnected(std::function<void(Context*)> cb) {
        if (cb != nullptr) {
            cb_on_client_connected_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }
    //-------------------------------------------------------------------------
    bool SetCbOnClientDisconnected(std::function<void(Context*)> cb) {
        if(cb != nullptr) {
            cb_on_client_disconnected_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }

protected :
    //-------------------------------------------------------------------------
    bool RunServer() {
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

        if (sock_usage_ != SOCK_USAGE_UDP_SERVER) {
            is_need_accept_ = true;
        }
        if (!StartServer()) {
            return false;
        }
        return true;
    }

    std::string       server_ip_ ;
    std::string       server_ipc_socket_path_ = "";
    int               server_port_ {-1};
    std::atomic<int>  client_cnt_ {0}; 
    std::atomic<bool> is_server_running_  {false};
    SOCKET_T          listen_socket_;
    size_t            max_event_  {0};
    int               max_worker_cnt_{0};
    std::atomic<int>  cur_quit_cnt_{0};
    std::queue<Context*> queue_ctx_cache_;
    std::queue<PER_IO_DATA*> queue_per_io_data_cache_;
    std::mutex per_io_data_cache_lock_ ; 
    std::mutex ctx_cache_lock_ ; 
    std::thread accept_thread_;
    std::atomic<bool> is_need_accept_{false};

private :
    //-------------------------------------------------------------------------
    bool StartServer() {
        if ( sock_usage_ == SOCK_USAGE_IPC_SERVER ) {
            listen_socket_ = WSASocketW(AF_UNIX, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
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
        if (sock_usage_ != SOCK_USAGE_IPC_SERVER) {
            //XXX windows는 domain socket 인 경우 다음을 호출하면 에러 발생됨 ! why ? 
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
        }

        int result = -1;
        if (sock_usage_ == SOCK_USAGE_IPC_SERVER) {
            DBG_LOG("unlink :" << server_ipc_socket_path_);
            DeleteFile(server_ipc_socket_path_.c_str());
            SOCKADDR_UN ipc_server_addr;
            memset((void*)&ipc_server_addr, 0x00, sizeof(ipc_server_addr));
            ipc_server_addr.sun_family = AF_UNIX;
            snprintf(ipc_server_addr.sun_path, sizeof(ipc_server_addr.sun_path),
                     "%s", server_ipc_socket_path_.c_str());

            result = bind(listen_socket_, 
                          (SOCKADDR*)&ipc_server_addr, 
                          sizeof(ipc_server_addr));

        } else if (sock_usage_ == SOCK_USAGE_TCP_SERVER || 
                   sock_usage_ == SOCK_USAGE_UDP_SERVER) {
            SOCKADDR_IN    server_addr  ;
            memset((void *)&server_addr,0x00,sizeof(server_addr)) ;
            server_addr.sin_family = AF_INET;
            inet_pton(AF_INET, server_ip_.c_str(), &(server_addr.sin_addr));
            server_addr.sin_port = htons(server_port_);

            result = bind(listen_socket_, 
                          (SOCKADDR*)&server_addr, 
                          sizeof(server_addr));
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
            udp_server_ctx_ptr =  PopClientContextFromCache();
            if (udp_server_ctx_ptr == nullptr) {
                ELOG(err_msg_);
                exit(EXIT_FAILURE);
            }
            udp_server_ctx_ptr->is_connected = true;
            udp_server_ctx_ptr->socket = listen_socket_;
            handle_completion_port_ = CreateIoCompletionPort((HANDLE)listen_socket_, 
                                         handle_completion_port_, 
                                         (ULONG_PTR)udp_server_ctx_ptr, 0);
            if (NULL == handle_completion_port_) {
                BuildErrMsgString(WSAGetLastError());
                ELOG("alloc failed! delete ctx_ptr");
                delete udp_server_ctx_ptr;
                exit(EXIT_FAILURE);
            }
            // Start receiving.
            if (!IssueRecv(99999, udp_server_ctx_ptr)) { // worker_index 99999 is for debugging.
                int last_err = WSAGetLastError();
                ELOG("sock=" << udp_server_ctx_ptr->socket <<  
                     ", error! : " << last_err);

                shutdown(udp_server_ctx_ptr->socket, SD_BOTH);
                if (0 != closesocket(udp_server_ctx_ptr->socket)) {
                    ELOG("sock=" << udp_server_ctx_ptr->socket << 
                         ",close socket error! : " << last_err);
                }
                //LOG("delete ctx_ptr , sock=" << udp_server_ctx_ptr->socket);
                udp_server_ctx_ptr->socket = INVALID_SOCKET; //XXX
                if (last_err == WSAECONNRESET) {
                    ELOG("invoke PuchClientContextToCache.. sock=" 
                             << udp_server_ctx_ptr->sock_id_copy);
                    PushClientContextToCache(udp_server_ctx_ptr);
                }
                exit(EXIT_FAILURE);
            }
        } else {
            accept_thread_ = std::thread (&ASockServerBase::AcceptThreadRoutine, this);
        }
        is_server_running_ = true;
        return true;
    }
    //-------------------------------------------------------------------------
    void TerminateClient(Context* ctx_ptr, bool is_graceful = true) {
        client_cnt_--;
        DBG_LOG(" sock=" << ctx_ptr->sock_id_copy
                << ", send_ref_cnt=" << ctx_ptr->send_ref_cnt
                << ", recv_ref_cnt=" << ctx_ptr->recv_ref_cnt
                << ", connection closing ,graceful="
                << (is_graceful ? "TRUE" : "FALSE"));
        ctx_ptr->is_connected = false;
        if (cb_on_client_disconnected_ != nullptr) {
            cb_on_client_disconnected_(ctx_ptr);
        } else {
            OnClientDisconnected(ctx_ptr);
        }
        if(ctx_ptr->socket == INVALID_SOCKET){
            DBG_LOG("already closed");
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
                ELOG("sock=" << ctx_ptr->socket << 
                     ",close socket error! : " << WSAGetLastError());
            }
            ctx_ptr->socket = INVALID_SOCKET; 
            PushClientContextToCache(ctx_ptr);
        }
    }
    //-------------------------------------------------------------------------
    void StartWorkerThreads() {
        SYSTEM_INFO system_info;
        GetSystemInfo(&system_info);
        max_worker_cnt_ = system_info.dwNumberOfProcessors ;
        DBG_LOG("(server) worker cnt = " << max_worker_cnt_);
        for (size_t i = 0; i < max_worker_cnt_ ; i++) {
            std::thread worker_thread(&ASockServerBase::WorkerThreadRoutine, this, i); 
            worker_thread.detach();
        }
    }
    //-------------------------------------------------------------------------
    // worker_index is for debugging.
    bool IssueRecv(size_t worker_index, Context* ctx) {
        size_t free_linear_len = ctx->GetBuffer()->GetLinearFreeSpace();
        DBG_LOG("want to recv len = " << free_linear_len);
        DWORD dw_flags = 0;
        DWORD dw_recv_bytes = 0;
        SecureZeroMemory((PVOID)& ctx->per_recv_io_ctx->overlapped, sizeof(WSAOVERLAPPED));
        //XXX cumbuffer 에 가능한 만큼만 ...
        ctx->per_recv_io_ctx->wsabuf.buf = ctx->GetBuffer()->GetLinearAppendPtr();
        ctx->per_recv_io_ctx->wsabuf.len = (ULONG)free_linear_len ;
        ctx->per_recv_io_ctx->io_type = EnumIOType::IO_RECV;
        int result = -1;
        if (sock_usage_ == SOCK_USAGE_UDP_SERVER || sock_usage_ == SOCK_USAGE_UDP_CLIENT) {
            SOCKLEN_T addrlen = sizeof(ctx->udp_remote_addr);
            result = WSARecvFrom(ctx->socket, 
                                 &(ctx->per_recv_io_ctx->wsabuf),
                                 1, 
                                 (LPDWORD)&dw_recv_bytes, 
                                 (LPDWORD)&dw_flags,
                                 (struct sockaddr*)& ctx->udp_remote_addr, 
                                 &addrlen, 
                                 &(ctx->per_recv_io_ctx->overlapped), 
                                 NULL);
        } else {
            result = WSARecv(ctx->socket, 
                             &(ctx->per_recv_io_ctx->wsabuf),
                             1, 
                             &dw_recv_bytes, 
                             &dw_flags,
                             &(ctx->per_recv_io_ctx->overlapped), 
                             NULL);
        }
        if (SOCKET_ERROR == result) {
            if (WSAGetLastError() != WSA_IO_PENDING) {
                int last_err = WSAGetLastError();
                BuildErrMsgString(last_err);
                shutdown(ctx->socket, SD_BOTH);
                if (0 != closesocket(ctx->socket)) {
                    ELOG("sock=" << ctx->socket << ",close socket error! : " << last_err);
                }
                ctx->socket = INVALID_SOCKET;
                return false;
            }
        }
        ctx->recv_ref_cnt++;
        return true;
    }

    //-------------------------------------------------------------------------
    bool BuildClientContextCache() {
        Context* ctx_ptr = nullptr;
        for (int i = 0; i < max_event_; i++) {
            ctx_ptr = new (std::nothrow) Context();
            if (nullptr == ctx_ptr) {
                err_msg_ = "Context alloc failed !";
                return false;
            }
            ctx_ptr->per_recv_io_ctx = new (std::nothrow) PER_IO_DATA; 
            if (nullptr == ctx_ptr->per_recv_io_ctx) {
                err_msg_ = "per_io_ctx alloc failed !";
                delete ctx_ptr;
                return false;
            }
            if (cumbuffer::OP_RSLT_OK != ctx_ptr->per_recv_io_ctx->cum_buffer.Init(max_data_len_)) {
                err_msg_ = std::string("cumBuffer(recv) Init error : ") +
                    std::string(ctx_ptr->per_recv_io_ctx->cum_buffer.GetErrMsg());
                delete ctx_ptr->per_recv_io_ctx;
                delete ctx_ptr;
                ELOG(err_msg_);
                return false;
            }
            ctx_ptr->pending_send_deque.clear();
            ReSetCtxPtr(ctx_ptr);
            queue_ctx_cache_.push(ctx_ptr);
        }
        //LOG("build ctx: queue_ctx_cache_ size =" << queue_ctx_cache_.size());
        return true;
    }
    //-------------------------------------------------------------------------
    void PushClientContextToCache(Context* ctx_ptr) {
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        ReSetCtxPtr(ctx_ptr);
        queue_ctx_cache_.push(ctx_ptr);
        //LOG("push: queue ctx cache size = " << queue_ctx_cache_.size());
    }

    //-------------------------------------------------------------------------
    void ClearClientCache() {
        DBG_LOG("======= clear all ctx cache ======== : " << queue_ctx_cache_.size());
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        while (!queue_ctx_cache_.empty()) {
            Context* ctx_ptr = queue_ctx_cache_.front();
            while(!ctx_ptr->pending_send_deque.empty() ) {
                PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
                delete [] pending_sent.pending_sent_data;
                ctx_ptr->pending_send_deque.pop_front();
            }
            //LOG("delete per rec io ctx");
			delete ctx_ptr->per_recv_io_ctx; 
            delete ctx_ptr;
            queue_ctx_cache_.pop();
        }
    }
    //-------------------------------------------------------------------------
    void ReSetCtxPtr(Context* ctx_ptr) {
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
    Context* PopClientContextFromCache() {
		//LOG("pop : queue_ctx_cache  -> " << queue_ctx_cache_.size());
        Context* ctx_ptr = nullptr;
        { //lock scope
            std::lock_guard<std::mutex> lock(ctx_cache_lock_);
			//LOG("pop : queue_ctx_cache -> " << queue_ctx_cache_.size());
            if (!queue_ctx_cache_.empty()) {
                ctx_ptr = queue_ctx_cache_.front();
                queue_ctx_cache_.pop(); 
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
		//LOG("pop ctx: queue_ctx_cache  -> " << queue_ctx_cache_.size());
        ReSetCtxPtr(rtn_ctx_ptr);
        return rtn_ctx_ptr ;
    }

    //-------------------------------------------------------------------------
    // SendData 에서만 사용함
    bool BuildPerIoDataCache() {
        DBG_LOG("queue_per_io_data_cache_ alloc ");
        PER_IO_DATA* per_io_data_ptr = nullptr;
        for (int i = 0; i < max_event_ * 2; i++) { 
            per_io_data_ptr = new (std::nothrow) PER_IO_DATA;
            if (nullptr == per_io_data_ptr) {
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
        DBG_LOG("build per io data: queue_per_io_data_cache_ size =" << queue_per_io_data_cache_.size());
        return true;
    }
    //-------------------------------------------------------------------------
    // SendData 에서만 사용함
    PER_IO_DATA* PopPerIoDataFromCache() {
        PER_IO_DATA* per_io_data_ptr = nullptr;
        std::lock_guard<std::mutex> lock(per_io_data_cache_lock_);
        if (!queue_per_io_data_cache_.empty()) {
            per_io_data_ptr = queue_per_io_data_cache_.front();
            queue_per_io_data_cache_.pop();
            //LOG("queue_per_io_data_cache_ not empty! -> " << queue_per_io_data_cache_.size());
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
    // SendData 에서만 사용함
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
        DBG_LOG("queue_per_io_data_cache_ client cache size = " << queue_per_io_data_cache_.size());
    }

    //-------------------------------------------------------------------------
    // SendData 에서만 사용함
    void ClearPerIoDataCache() {
        //LOG("~~~~ clear: queue_per_io_data_cache_ size =" << queue_per_io_data_cache_.size());
        std::lock_guard<std::mutex> lock(per_io_data_cache_lock_);
        while (!queue_per_io_data_cache_.empty()) {
            PER_IO_DATA* per_io_data_ptr = queue_per_io_data_cache_.front();
            if(per_io_data_ptr->wsabuf.buf != NULL) {
                delete [] per_io_data_ptr->wsabuf.buf; 
            }
            delete per_io_data_ptr;
            queue_per_io_data_cache_.pop();
        }
    }
    //-------------------------------------------------------------------------
    void AcceptThreadRoutine() {
        is_server_running_ = true;
        while(is_need_accept_) {
            SOCKET_T client_sock = WSAAccept(listen_socket_, NULL, NULL, NULL, 0);
            if (client_sock == INVALID_SOCKET) {
                int last_err = WSAGetLastError();
                if (WSAEWOULDBLOCK == last_err) {
					if (!is_need_accept_) {
                        break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                BuildErrMsgString(last_err);
                if (!is_need_accept_) {
                    ELOG("exit");
                    exit(EXIT_FAILURE);
                } else {
                    break;
                }
            }
            client_cnt_++;
            SetSocketNonBlocking(client_sock);
            Context* ctx_ptr = PopClientContextFromCache();
            if (ctx_ptr == nullptr) {
                ELOG("exit");
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
                ELOG("exit" );
                delete ctx_ptr;
                exit(EXIT_FAILURE);
            }
            // XXX iocp 초기화 완료후 사용자의 콜백을 호출해야 함. 
            // 사용자가 콜백에서 send를 시도할수 있기 때문.
            DBG_LOG("**** accept returns...new client : sock="<<client_sock << " => client total = "<< client_cnt_ );
            if (cb_on_client_connected_ != nullptr) {
                cb_on_client_connected_(ctx_ptr);
            } else {
                OnClientConnected(ctx_ptr);
            }
            // Start receiving.
            if (!IssueRecv(0, ctx_ptr)) {  // worker_index is not in use
                client_cnt_--;
                ELOG("sock=" << ctx_ptr->socket <<  ", error! ");
                PushClientContextToCache(ctx_ptr);
                continue;
            }
        } //while
        //LOG("accept thread exiting");
    }

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
                    ELOG("ERROR_ABANDONED_WAIT_0");
                    return;
                }
                BuildErrMsgString(err);
                if (lp_overlapped != NULL) {
                    //===================== lock
                    ELOG("worker=" << worker_index << ", sock=" 
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
                    ELOG("GetQueuedCompletionStatus failed..err="<< err);
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
            //======================================================= IO_RECV
            case EnumIOType::IO_RECV:
            {
                ctx_ptr->recv_ref_cnt--;
                //LOG("worker=" << worker_index << ",IO_RECV: sock=" 
                //        << ctx_ptr->sock_id_copy << ", recv_ref_cnt =" 
                //        << ctx_ptr->recv_ref_cnt << ", recved bytes =" 
                //        << bytes_transferred);
                //# recv #---------- 
                if (bytes_transferred == 0) {
                    //graceful disconnect.
                    DBG_LOG("0 recved --> gracefully disconnected : " << ctx_ptr->recv_ref_cnt);
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
                //LOG("worker=" << worker_index << ",IO_SEND: sock=" 
                //        << ctx_ptr->socket << ", send_ref_cnt =" << ctx_ptr->send_ref_cnt);
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
                            ELOG("WSASend() failed" );
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
            //======================================================= IO_QUIT
            case EnumIOType::IO_QUIT :
            {
                //XXX StopServer에서 다음처럼 사용했으므로, 메모리 정리 해야함
				//--> Context* ctx_ptr = PopClientContextFromCache();
				PushClientContextToCache(ctx_ptr);
                //LOG( "IO_QUIT :" << cur_quit_cnt_ << " : " << max_worker_cnt_);
                cur_quit_cnt_++;
                if (max_worker_cnt_ == cur_quit_cnt_) {
                    is_server_running_ = false;
                    //LOG("set is_server_running_ false.");
                }
                return;
            }
            break;
            }//switch
        } //while
    }

private :
    std::function<void(Context*)> cb_on_client_connected_ {nullptr} ;
    std::function<void(Context*)> cb_on_client_disconnected_ {nullptr};

    //for inheritance : Implement these virtual functions.
    virtual void    OnClientConnected(Context* ctx_ptr) {}; 
    virtual void    OnClientDisconnected(Context* ctx_ptr) {} ;  
};
} //namaspace

#endif

