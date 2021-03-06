/******************************************************************************
MIT License

Copyright (c) 2019 jung hyun, ko

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
//there is no concurrent send/recv per a socket.
///////////////////////////////////////////////////////////////////////////////
//TODO per_io_ctx ==> 동적 할당된 메모리
//TODO SOCK_USAGE_UDP_SERVER 
//TODO SOCK_USAGE_UDP_CLIENT 
//TODO SOCK_USAGE_IPC_SERVER 
//TODO SOCK_USAGE_IPC_CLIENT 
//TODO bool use_zero_byte_receive_ 
//TODO one header file only ? if possible..
//202011 encoding to utf-8
#include "ASock.hpp"

using namespace asock;
///////////////////////////////////////////////////////////////////////////////
ASock::ASock()
{
    is_connected_ = false;
}

ASock::~ASock()
{
    if (sock_usage_ == SOCK_USAGE_TCP_CLIENT ||
        sock_usage_ == SOCK_USAGE_UDP_CLIENT ||
        sock_usage_ == SOCK_USAGE_IPC_CLIENT) {
        Disconnect();
    } else if ( sock_usage_ == SOCK_USAGE_TCP_SERVER ||
                sock_usage_ == SOCK_USAGE_UDP_SERVER ||
                sock_usage_ == SOCK_USAGE_IPC_SERVER) {
        ClearClientCache();
    }
    WSACleanup();
}


///////////////////////////////////////////////////////////////////////////////
//server use
bool ASock::SendData(Context* ctx_ptr, const char* data_ptr, size_t len) 
{
    //XXX  recv에서 연결 종료 판단되었을 수 있음!!!
    std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock);  //XXX need
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
    PER_IO_DATA* per_send_ctx = new (std::nothrow) asock::PER_IO_DATA;
    if (per_send_ctx == nullptr) {
        DBG_ELOG("mem alloc failed");
        shutdown(ctx_ptr->socket, SD_BOTH);
        if (0 != closesocket(ctx_ptr->socket)) {
            DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " 
                    << WSAGetLastError());
        }
        ctx_ptr->socket = INVALID_SOCKET; //XXX
        exit(1);
    }
    SecureZeroMemory((PVOID)&per_send_ctx->overlapped, sizeof(WSAOVERLAPPED));
    DWORD dw_flags = 0;
    DWORD dw_send_bytes = 0;
    memcpy(per_send_ctx->send_buffer, data_ptr, len); //XXX
    per_send_ctx->wsabuf.buf = per_send_ctx->send_buffer;
    per_send_ctx->wsabuf.len = (ULONG)len;
    per_send_ctx->io_type = EnumIOType::IO_SEND;
    per_send_ctx->total_send_len = len;
    per_send_ctx->sent_len = 0;
    int result = 0;

    result = WSASend(ctx_ptr->socket, &(per_send_ctx->wsabuf),
                    1, &dw_send_bytes, dw_flags,
                    &(per_send_ctx->overlapped), NULL);
    if (result == 0) {
        //no error occurs and the send operation has completed immediately
        DBG_LOG("!!! WSASend returns 0 !!! ");
    } else if (result == SOCKET_ERROR) {
        if (WSA_IO_PENDING == WSAGetLastError()) {
            //DBG_LOG("!!! WSASend returns --> WSA_IO_PENDING");
        } else {
            int last_err = WSAGetLastError();
            BuildErrMsgString(last_err);
            DBG_ELOG("WSASend error : " << err_msg_ <<"recv posted="
                << ctx_ptr->recv_issued_cnt );
            delete per_send_ctx; //TEST XXX 
            shutdown(ctx_ptr->socket, SD_BOTH);
            if (0 != closesocket(ctx_ptr->socket)) {
                DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " 
                        << last_err);
            }
            ctx_ptr->socket = INVALID_SOCKET; 
            return false;
        }
    }
    ctx_ptr->posted_send_cnt++;
    ctx_ptr->ref_cnt++;
    //LOG("sock=" << ctx_ptr->sock_id_copy << ", ref_cnt= " << ctx_ptr->ref_cnt);
    DBG_LOG("sock=" << ctx_ptr->sock_id_copy << ", ref_cnt=" << ctx_ptr->ref_cnt);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
//for client use
void ASock:: Disconnect()
{
    DBG_LOG("Disconnect");
    is_connected_ = false;
    if(context_.socket != INVALID_SOCKET ) {
        shutdown(context_.socket, SD_BOTH);
        closesocket(context_.socket);
    }
    context_.socket = INVALID_SOCKET;
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
///////////////////////////////////////////////////////////////////////////////
//XXX server, client use this
bool ASock::RecvData(size_t worker_index, Context* ctx_ptr, DWORD bytes_transferred )
{
    if (ctx_ptr->socket == INVALID_SOCKET) {
        DBG_ELOG("sock=" << ctx_ptr->sock_id_copy << ",invalid socket, failed.");
        return false;
    }
    //std::cout << "sock="<<ctx_ptr->socket <<", bytes_transferred = " << bytes_transferred <<"\n";
    ctx_ptr->GetBuffer()->IncreaseData(bytes_transferred);
    DBG_LOG("worker="<<worker_index<<",sock="<<ctx_ptr->sock_id_copy<<",GetCumulatedLen = " 
        << ctx_ptr->GetBuffer()->GetCumulatedLen()
        << ",ref_cnt=" << ctx_ptr->ref_cnt
        << ",bytes_transferred=" << bytes_transferred );
#ifdef DEBUG_PRINT
    ctx_ptr->GetBuffer()->DebugPos(ctx_ptr->sock_id_copy);
#endif
    //DBG_LOG("GetCumulatedLen = " << ctx_ptr->per_recv_io_ctx->cum_buffer.GetCumulatedLen());
    while (ctx_ptr->per_recv_io_ctx->cum_buffer.GetCumulatedLen()) {
        //XXX .. for client !!! =========== START TODO --> make better !
        if (ctx_ptr->socket == INVALID_SOCKET) {
            DBG_ELOG("sock=" << ctx_ptr->sock_id_copy << ",invalid socket, failed.");
            return false;
        }
        //XXX .. for client !!! =========== END
        DBG_LOG("worker="<<worker_index<<",sock=" << ctx_ptr->sock_id_copy 
            << ",GetCumulatedLen = " 
            << ctx_ptr->per_recv_io_ctx->cum_buffer.GetCumulatedLen()
            << ",ref_cnt=" << ctx_ptr->ref_cnt
            << ",bytes_transferred=" << bytes_transferred);
        //invoke user specific implementation
        if (!ctx_ptr->per_recv_io_ctx->is_packet_len_calculated){ 
            //only when calculation is necessary
            if (cb_on_calculate_data_len_ != nullptr) {
                //invoke user specific callback
                ctx_ptr->per_recv_io_ctx->complete_recv_len = 
                    cb_on_calculate_data_len_(ctx_ptr);
            } else {
                //invoke user specific implementation
                ctx_ptr->per_recv_io_ctx->complete_recv_len = 
                    OnCalculateDataLen(ctx_ptr);
            }
            DBG_LOG("worker="<<worker_index<<",sock="<<ctx_ptr->sock_id_copy<<", whole length =" 
                << ctx_ptr->per_recv_io_ctx->complete_recv_len);
            if (ctx_ptr->per_recv_io_ctx->complete_recv_len == 6) {
                //DEBUG only!!
                DBG_ELOG("whole length is 6 ?????? ");
                typedef struct _ST_MY_CHAT_HEADER_ {
                    char msg_len[6];
                } ST_MY_HEADER;
                ST_MY_HEADER header;
                const size_t  CHAT_HEADER_SIZE = sizeof(ST_MY_HEADER);
                ctx_ptr->GetBuffer()->PeekData(CHAT_HEADER_SIZE, (char*)&header);
                DBG_LOG("header ="<<header.msg_len);
            }
            ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = true;
        }
        if (ctx_ptr->per_recv_io_ctx->complete_recv_len == asock::MORE_TO_COME) {
            ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
            DBG_LOG("worker="<<worker_index<<",sock="<<ctx_ptr->sock_id_copy<<": more to come" );
            DBG_LOG("sock=" << ctx_ptr->sock_id_copy << ", cumbuffer debug=");
#ifdef DEBUG_PRINT
            ctx_ptr->GetBuffer()->DebugPos(ctx_ptr->sock_id_copy);
#endif
            return true; //need to recv more
        } else if (ctx_ptr->per_recv_io_ctx->complete_recv_len > 
                   ctx_ptr->per_recv_io_ctx->cum_buffer.GetCumulatedLen()) {
            DBG_LOG("worker="<<worker_index<<",sock="<<ctx_ptr->sock_id_copy
                <<",GetCumulatedLen = " 
                << ctx_ptr->per_recv_io_ctx->cum_buffer.GetCumulatedLen()
                <<": more to come" );
            DBG_LOG("sock=" << ctx_ptr->sock_id_copy << ", cumbuffer debug=");
#ifdef DEBUG_PRINT
            ctx_ptr->GetBuffer()->DebugPos(ctx_ptr->sock_id_copy);
#endif
            return true; //need to recv more
        } else {
            //got complete packet 
            size_t alloc_len = ctx_ptr->per_recv_io_ctx->complete_recv_len;
            DBG_LOG("alloc len=" << alloc_len);
            char* complete_packet_data = new (std::nothrow) char [alloc_len] ; //XXX 
            if(complete_packet_data == nullptr) {
                DBG_ELOG("mem alloc failed");
                exit(1);
            }
            if (cumbuffer::OP_RSLT_OK !=
                ctx_ptr->per_recv_io_ctx->cum_buffer.GetData (
                    ctx_ptr->per_recv_io_ctx->complete_recv_len, complete_packet_data)) {
                //error !
                {
                    std::lock_guard<std::mutex> lock(err_msg_lock_);
                    err_msg_ = ctx_ptr->per_recv_io_ctx->cum_buffer.GetErrMsg();
                }
                ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
                delete[] complete_packet_data; //XXX
                DBG_ELOG("error! ");
                //exit(1);
                return false;
            }
            DBG_LOG("worker="<<worker_index<<",sock="<<ctx_ptr->sock_id_copy
                << ",ref_cnt=" << ctx_ptr->ref_cnt
                << ":got complete data [" << complete_packet_data <<"]"  ); 
            if (cb_on_recved_complete_packet_ != nullptr) {
                //invoke user specific callback
                cb_on_recved_complete_packet_(ctx_ptr, complete_packet_data, 
                                    ctx_ptr->per_recv_io_ctx->complete_recv_len);
            } else {
                //invoke user specific implementation
                OnRecvedCompleteData(ctx_ptr, complete_packet_data, 
                                    ctx_ptr->per_recv_io_ctx->complete_recv_len);
            }
            delete[] complete_packet_data; //XXX
            ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
        }
    } //while
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::IssueRecv(size_t worker_index, Context* ctx_ptr)
{
    if (ctx_ptr->socket == INVALID_SOCKET) {
        DBG_ELOG("sock=" << ctx_ptr->sock_id_copy << ",invalid socket, failed.");
        return false;
    }
    if (ctx_ptr->recv_issued_cnt > 0) {
        //XXX 아직 recv issued 된것에 대해 처리가 안된 상태라면 skip!!
        //이 처리 없으면 cumbuffer 의 남은 용량에 맞게 want recv len 조정
        //동기화가 안됨! client 가 계속 전송하는 경우에, recv issue 반복되고, 
        //iocp 는 멀티쓰레드 pool 로 처리되므로..
        DBG_LOG("worker=" << worker_index << ",sock=" << ctx_ptr->sock_id_copy
            << ", recv issued cnt=" << ctx_ptr->recv_issued_cnt
            << " --> SKIP !");
        return true;
    }
    //XXX cumbuffer 에 Append 가능할 만큼만 수신하게 해줘야함!!
    //TODO 0 byte 수신을 고려?? 
    size_t want_recv_len = max_data_len_;
    //-------------------------------------
    if (max_data_len_ > ctx_ptr->GetBuffer()->GetLinearFreeSpace()) { 
        want_recv_len = ctx_ptr->GetBuffer()->GetLinearFreeSpace();
    }
    if (want_recv_len == 0) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "no linear free space left ";
        DBG_ELOG(err_msg_);
        return false;
    }
    /*
    DBG_LOG("worker="<<worker_index<<",sock=" << ctx_ptr->sock_id_copy 
        << ", recv issued cnt=" << ctx_ptr->recv_issued_cnt
        << ", *** want_recv_len =" << want_recv_len);
        */
    DWORD dw_flags = 0;
    DWORD dw_recv_bytes = 0;
    SecureZeroMemory((PVOID)& ctx_ptr->per_recv_io_ctx->overlapped, sizeof(WSAOVERLAPPED));
    //XXX cumbuffer 에 가능한 만큼만 ...
    ctx_ptr->per_recv_io_ctx->wsabuf.buf = ctx_ptr->GetBuffer()->GetLinearAppendPtr();
    ctx_ptr->per_recv_io_ctx->wsabuf.len = (ULONG)want_recv_len ;
    ctx_ptr->per_recv_io_ctx->io_type = EnumIOType::IO_RECV;
    int result = WSARecv(   ctx_ptr->socket, 
                            &(ctx_ptr->per_recv_io_ctx->wsabuf), 
                            1, 
                            & dw_recv_bytes, 
                            & dw_flags, 
                            &(ctx_ptr->per_recv_io_ctx->overlapped), 
                            NULL);
    if (SOCKET_ERROR == result) {
        if (WSAGetLastError() != WSA_IO_PENDING) {
            BuildErrMsgString(WSAGetLastError());
            ELOG("sock=" << ctx_ptr->sock_id_copy << ", " <<err_msg_);
            return false;
        }
    }
    ctx_ptr->ref_cnt++;
    ctx_ptr->recv_issued_cnt++;
    //LOG("sock=" << ctx_ptr->sock_id_copy << ", ref_cnt= " << ctx_ptr->ref_cnt);
    DBG_LOG("worker="<<worker_index<<",sock=" << ctx_ptr->sock_id_copy 
        << ", want recv len=" <<want_recv_len
        << ", post recv , ref_cnt =" << ctx_ptr->ref_cnt);
#ifdef DEBUG_PRINT
    ctx_ptr->GetBuffer()->DebugPos(ctx_ptr->sock_id_copy);
#endif
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// SERVER
///////////////////////////////////////////////////////////////////////////////
bool ASock::InitTcpServer(  const char* bind_ip, int bind_port, 
                            size_t  max_data_len , size_t  max_client )
{
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

///////////////////////////////////////////////////////////////////////////////
bool ASock::RunServer()
{
    DBG_LOG( "Run Server..."); 
    if(is_server_running_ ) { 
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "error [server is already running]";
        ELOG("error! " << err_msg_); 
        return false;
    }
    if(!InitWinsock()) {
        return false;
    }
    handle_completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if(max_data_len_ <0) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "init error [packet length is negative]";
        DBG_ELOG(err_msg_);
        return  false;
    }
    //--------------------------
    StartWorkerThreads();
    //--------------------------
    if (!StartAcceptThread()) {
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::StartWorkerThreads()
{
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    int max_worker_cnt = system_info.dwNumberOfProcessors * 2;
    DBG_LOG("(server) worker cnt = " << max_worker_cnt);
    for (size_t i = 0; i < max_worker_cnt; i++) {
        std::thread worker_thread(&ASock::WorkerThreadRoutine, this, i); 
        worker_thread.detach();
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::StartAcceptThread()
{
    DBG_LOG("PER_IO_DATA size ==> " << sizeof(PER_IO_DATA));
    listen_socket_ = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (listen_socket_ == INVALID_SOCKET) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    if(!SetSocketNonBlocking (listen_socket_)) {
        return  false;
    }
    int opt_zero = 0;
    if(setsockopt(listen_socket_, SOL_SOCKET, SO_SNDBUF, (char*)&opt_zero, 
        sizeof(opt_zero)) == SOCKET_ERROR) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    int opt_on = 1;
    if (setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt_on,
        sizeof(opt_on)) == SOCKET_ERROR) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    if (setsockopt(listen_socket_, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt_on,
        sizeof(opt_on)) == SOCKET_ERROR) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    memset((void*)&tcp_server_addr_, 0x00, sizeof(tcp_server_addr_));
    tcp_server_addr_.sin_family = AF_INET;
    inet_pton(AF_INET, server_ip_.c_str(), &(tcp_server_addr_.sin_addr));
    tcp_server_addr_.sin_port = htons(server_port_);
    int result = bind(listen_socket_, (SOCKADDR*)&tcp_server_addr_, 
                      sizeof(tcp_server_addr_));
    if (result < 0) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    result = listen(listen_socket_, SOMAXCONN);
    if (result < 0) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    //XXX start server thread
    std::thread server_thread(&ASock::AcceptThreadRoutine, this);
    server_thread.detach();
    //is_need_server_run_ = true;
    is_server_running_ = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock:: AcceptThreadRoutine()
{
    SOCKLEN_T socklen=sizeof(SOCKADDR_IN);
    SOCKADDR_IN client_addr;
    while(is_need_server_run_) {
        SOCKET_T client_sock = WSAAccept(listen_socket_, 
                                  (SOCKADDR*)&client_addr, &socklen, 0, 0);
        if (client_sock == INVALID_SOCKET) {
            int last_err = WSAGetLastError();
            if (WSAEWOULDBLOCK == last_err) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            BuildErrMsgString(last_err);
            ELOG( err_msg_ ) ;
            exit(1);
        }
        DBG_LOG("**** accept returns...new client : sock="<<client_sock );
        client_cnt_++;
        DBG_LOG("* client total = "<< client_cnt_ );
        SetSocketNonBlocking(client_sock);
        Context* ctx_ptr = PopClientContextFromCache();
        if (ctx_ptr == nullptr) {
            ELOG(err_msg_);
            exit(1);
        }
        ctx_ptr->is_connected = true;
        ctx_ptr->socket = client_sock; 
        ctx_ptr->sock_id_copy = client_sock; //for debugging
        DBG_LOG("sock="<< client_sock << ", cumbuffer debug=");
#ifdef DEBUG_PRINT
        ctx_ptr->GetBuffer()->DebugPos(ctx_ptr->sock_id_copy);
#endif
        if (cb_on_client_connected_ != nullptr) {
            cb_on_client_connected_(ctx_ptr);
        } else {
            OnClientConnected(ctx_ptr);
        }
        handle_completion_port_ = CreateIoCompletionPort((HANDLE)client_sock, 
                        handle_completion_port_, (ULONG_PTR)ctx_ptr, 0); 
        if (NULL == handle_completion_port_) {
            BuildErrMsgString(WSAGetLastError());
            ELOG(err_msg_ );
            ELOG("alloc failed! delete ctx_ptr");
            delete ctx_ptr;
            exit(1); 
        }
        //XXX ctx_ptr --> per io data !!!
        {
            std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock); 
            if (!IssueRecv(99999, ctx_ptr)) { //XXX 99999 accept 표시
                int last_err = WSAGetLastError();
                DBG_ELOG("sock="<<ctx_ptr->socket<<",ref_cnt="<<ctx_ptr->ref_cnt
                    <<", error! : " << WSAGetLastError());
                shutdown(ctx_ptr->socket, SD_BOTH);
                if (0 != closesocket(ctx_ptr->socket)) {
                    DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " 
                            << WSAGetLastError());
                }
                LOG("delete ctx_ptr , sock="<<ctx_ptr->socket);
                ctx_ptr->socket = INVALID_SOCKET; //XXX
                HandleError(ctx_ptr, last_err);
                continue; 
            }
        }
    } //while
    DBG_LOG("exiting");
    is_need_server_run_ = false;
    is_server_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//io 작업 결과에 대해 처리한다. --> multi thread 에 의해 수행된다 !!!
void ASock:: WorkerThreadRoutine(size_t worker_index) {
    DWORD       bytes_transferred = 0;
    Context*    ctx_ptr = NULL ;
    LPWSAOVERLAPPED lp_overlapped = NULL;
    PER_IO_DATA*    per_io_ctx = NULL;
    while ( true ) {
        bytes_transferred = 0;
        ctx_ptr = NULL ;
        lp_overlapped = NULL;
        per_io_ctx = NULL;
        if (FALSE == GetQueuedCompletionStatus( handle_completion_port_, 
                            & bytes_transferred, 
                            (PULONG_PTR) & ctx_ptr, 
                            (LPOVERLAPPED*) &lp_overlapped,
                            INFINITE )) {
            int err = WSAGetLastError();
            if (err == ERROR_ABANDONED_WAIT_0 || err== ERROR_INVALID_HANDLE) {
                //set by StopServer()
                //user quit
                DBG_LOG("ERROR_ABANDONED_WAIT_0");
                continue;
            }
            BuildErrMsgString(err);
            std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock); //XXX need
            ctx_ptr->ref_cnt--;
            if (lp_overlapped != NULL) {
                //===================== lock
                DBG_ELOG("worker=" << worker_index
                    << ", sock=" << ctx_ptr->sock_id_copy
                    << ", GetQueuedCompletionStatus failed.."
                    << ", bytes = " << bytes_transferred << ", err=" << err
                    << ", ref_cnt =" << ctx_ptr->ref_cnt);
                per_io_ctx = (PER_IO_DATA*)lp_overlapped;
                if (per_io_ctx->io_type == EnumIOType::IO_SEND) {
                    //XXX per_io_ctx ==> 동적 할당된 메모리임!!!  XXX
                    delete per_io_ctx;
                    per_io_ctx = NULL;
                }
                if (bytes_transferred == 0) {
                    //graceful disconnect.
                    DBG_LOG("sock="<<ctx_ptr->sock_id_copy
                        <<", 0 recved --> gracefully disconnected : " 
                        << ctx_ptr->sock_id_copy);
                    TerminateClient(ctx_ptr);
                    continue;
                } else {
                    if (err == ERROR_NETNAME_DELETED) { // --> 64
                        //client hard close --> not an error
                        DBG_LOG("worker=" << worker_index << ",sock=" 
                            << ctx_ptr->sock_id_copy
                            << " : client hard close. ERROR_NETNAME_DELETED ");
                        TerminateClient(ctx_ptr, false); //force close
                        continue;
                    } else {
                        //error 
                        DBG_ELOG(" GetQueuedCompletionStatus failed  :" << err_msg_);
                        TerminateClient(ctx_ptr, false); //force close
                        continue;
                    }
                }
            } else {
                DBG_ELOG("GetQueuedCompletionStatus failed..err="<< err);
                TerminateClient(ctx_ptr, false); //force close
                continue;
            }
        }
        //-----------------------------------------
        per_io_ctx = (PER_IO_DATA*)lp_overlapped; 
        //-----------------------------------------
        switch (per_io_ctx->io_type) {
        //case EnumIOType::IO_ACCEPT:
        //    break;
        case EnumIOType::IO_RECV:
        {
            ctx_ptr->recv_issued_cnt--;
            ctx_ptr->ref_cnt--;
            DBG_LOG("worker=" << worker_index << ",IO_RECV: sock=" 
                << ctx_ptr->sock_id_copy
                << ", ref_cnt =" << ctx_ptr->ref_cnt
                << ", recv issued cnt =" << ctx_ptr->recv_issued_cnt
                << ", recved bytes =" << bytes_transferred);
            //# recv #---------- 
            if (bytes_transferred == 0) {
                //graceful disconnect.
                DBG_LOG("0 recved --> gracefully disconnected : " 
                        << ctx_ptr->sock_id_copy);
                std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock); 
                TerminateClient(ctx_ptr);
                break;
            }
            if (!RecvData(worker_index, ctx_ptr, bytes_transferred)) {
                int last_err = WSAGetLastError();
                DBG_ELOG(err_msg_);
                std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock); 
                shutdown(ctx_ptr->socket, SD_BOTH);
                if (0 != closesocket(ctx_ptr->socket)) {
                    DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " 
                            << last_err);
                }
                ctx_ptr->socket = INVALID_SOCKET; 
                HandleError(ctx_ptr, last_err);
                break;
            }
            std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock); //XXX
            if (!IssueRecv(worker_index, ctx_ptr)) {
                int last_err = WSAGetLastError();
                shutdown(ctx_ptr->socket, SD_BOTH);
                if (0 != closesocket(ctx_ptr->socket)) {
                    DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " 
                            << last_err);
                }
                ctx_ptr->socket = INVALID_SOCKET; 
                HandleError(ctx_ptr, last_err);
                break;
            }
        }
        break;
        case EnumIOType::IO_SEND:
        {
            //===================== lock
            std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock);  //need 
            if (ctx_ptr->socket == INVALID_SOCKET) {
                delete per_io_ctx;
                DBG_LOG("delete.sock=" << ctx_ptr->sock_id_copy << ", ref_cnt= " 
                        << ctx_ptr->ref_cnt);
                break;
            }
            ctx_ptr->ref_cnt--;
            ctx_ptr->posted_send_cnt--;
            DBG_LOG("worker=" << worker_index << ",IO_SEND: sock=" << ctx_ptr->socket 
                << ", ref_cnt =" << ctx_ptr->ref_cnt);
            //XXX per_io_ctx ==> dynamically allocated memory 
            per_io_ctx->sent_len += bytes_transferred;
            DBG_LOG("IO_SEND(sock=" << ctx_ptr->socket << ") : sent this time ="
                << bytes_transferred << ",total sent =" << per_io_ctx->sent_len
                << ",ctx_ptr->posted_send_cnt =" << ctx_ptr->posted_send_cnt);
            if (per_io_ctx->sent_len < per_io_ctx->total_send_len) {
                if (ctx_ptr->posted_send_cnt != 0) {
                    //Abandon the rest of the pending transfer. 
                    //This is because the order of sending is important.
                    {//lock scope
                        std::lock_guard<std::mutex> lock(err_msg_lock_);
                        err_msg_ = "partial send. and pending another send exists, "
                            "abandon all. terminate client:";
                        err_msg_ += std::to_string(ctx_ptr->socket);
                    }
                    delete per_io_ctx;
                    DBG_ELOG(err_msg_);
                    shutdown(ctx_ptr->socket, SD_BOTH);
                    if (0 != closesocket(ctx_ptr->socket)) {
                        DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " 
                                << WSAGetLastError());
                    }
                    ctx_ptr->socket = INVALID_SOCKET; //XXX
                    continue;
                }
                //-----------------------------------
                //now --> ctx_ptr->posted_send_cnt == 0 
                //남은 부분 전송
                DBG_LOG("socket (" << ctx_ptr->socket
                    << ") send partially completed, total="
                    << per_io_ctx->total_send_len
                    << ",partial= " << per_io_ctx->sent_len << ", send again");
                WSABUF buff_send;
                DWORD dw_flags = 0;
                DWORD dw_send_bytes = 0;
                buff_send.buf = per_io_ctx->send_buffer + per_io_ctx->sent_len;
                buff_send.len = (ULONG)(per_io_ctx->total_send_len -
                    per_io_ctx->sent_len);
                per_io_ctx->io_type = EnumIOType::IO_SEND;
                //----------------------------------------
                int result = WSASend(ctx_ptr->socket, &buff_send, 1,
                    &dw_send_bytes, dw_flags, &(per_io_ctx->overlapped),
                    NULL);
                if (result == SOCKET_ERROR &&
                    (ERROR_IO_PENDING != WSAGetLastError())) {
                    delete per_io_ctx;
                    int last_err = WSAGetLastError();
                    BuildErrMsgString(last_err);
                    ELOG("WSASend() failed: " << err_msg_);
                    shutdown(ctx_ptr->socket, SD_BOTH);
                    if (0 != closesocket(ctx_ptr->socket)) {
                        DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " 
                                << last_err);
                    }
                    ctx_ptr->socket = INVALID_SOCKET; 
                    break;
                }
                ctx_ptr->ref_cnt++;
                LOG("sock=" << ctx_ptr->sock_id_copy << ", ref_cnt= " << ctx_ptr->ref_cnt);
                DBG_LOG("sock=" << ctx_ptr->socket << ") ref_cnt =" << ctx_ptr->ref_cnt);
            } else {
                DBG_LOG("socket (" << ctx_ptr->socket <<
                    ") send all completed (" << per_io_ctx->sent_len
                    << ") ==> delete per io ctx!!");
                delete per_io_ctx;
                per_io_ctx = NULL;
            }
        }
        break;
        }//switch
    } //while
    DBG_LOG( "exiting "); 
    is_server_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::HandleError(Context* ctx_ptr, int err) {
    if (err == WSAECONNRESET) {
        LOG("invoke PuchClientContextToCache.. sock="<<ctx_ptr->sock_id_copy);
        DBG_ELOG("invoke PuchClientContextToCache.. sock="<<ctx_ptr->sock_id_copy);
        PushClientContextToCache(ctx_ptr);
    }
}

///////////////////////////////////////////////////////////////////////////////
void  ASock::TerminateClient( Context* ctx_ptr, bool is_graceful)
{
    DBG_LOG(" sock=" << ctx_ptr->sock_id_copy <<", ref_cnt="
        <<ctx_ptr->ref_cnt << ", connection closing ,graceful=" 
        << (is_graceful ? "TRUE" : "FALSE"));
    ctx_ptr->is_connected = false;
    if (cb_on_client_connected_ != nullptr) {
        cb_on_client_disconnected_(ctx_ptr);
    } else {
        OnClientDisconnected(ctx_ptr);
    }
    if(ctx_ptr->socket == INVALID_SOCKET){
        DBG_LOG(" sock=" << ctx_ptr->sock_id_copy << ", already closed");
        if (ctx_ptr->ref_cnt <= 0 ) { //XXX
            PushClientContextToCache(ctx_ptr);
        }
    } else {
        if (!is_graceful) {
            // force the subsequent closesocket to be abortative.
            LINGER  linger_struct;
            linger_struct.l_onoff = 1;
            linger_struct.l_linger = 0;
            setsockopt(ctx_ptr->socket, SOL_SOCKET, SO_LINGER,
                (char*)&linger_struct, sizeof(linger_struct));
        }
        shutdown(ctx_ptr->socket, SD_BOTH);
        if (0 != closesocket(ctx_ptr->socket)) {
            DBG_ELOG("sock=" << ctx_ptr->socket << ",close socket error! : " 
                << WSAGetLastError());
        }
        ctx_ptr->socket = INVALID_SOCKET; 
        client_cnt_--;
        DBG_LOG("* client total = " << client_cnt_);
        DBG_LOG("ref_cnt = " << ctx_ptr->ref_cnt );
        if (ctx_ptr->ref_cnt <= 0 ) { //XXX
            PushClientContextToCache(ctx_ptr);
        } 
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::InitWinsock() {
    WSADATA wsa_data;
    int nResult = WSAStartup(MAKEWORD(2, 2), & wsa_data);
    if (nResult != 0) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        WSACleanup();
        return false;
    }
    if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "Could not find a usable version of Winsock.dll";
        DBG_ELOG(err_msg_);
        WSACleanup();
        return false;
    }
    return true;
}
///////////////////////////////////////////////////////////////////////////////
void ASock::StopServer()
{
    is_need_server_run_ = false; //stop accept thread
    //iocp 종료  
    CloseHandle(handle_completion_port_);
}

///////////////////////////////////////////////////////////////////////////////
Context* ASock::PopClientContextFromCache() 
{
    Context* ctx_ptr = nullptr;
    { //lock scope
        std::lock_guard<std::mutex> lock(cache_lock_);
        if (!queue_client_cache_.empty()) {
            ctx_ptr = queue_client_cache_.front();
            queue_client_cache_.pop(); 
            DBG_LOG("queue_client_cache not empty! -> "
                <<"queue client cache size = " << queue_client_cache_.size());
            ReSetCtxPtr(ctx_ptr);
            return ctx_ptr;
        }
    }
    //alloc new !!!
    DBG_LOG("queue_client_cache empty! alloc new !");
    ctx_ptr = new (std::nothrow) Context();
    if (nullptr==ctx_ptr) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "Context alloc failed !";
        return nullptr;
    }
    ctx_ptr->per_recv_io_ctx = new (std::nothrow) PER_IO_DATA;
    if (nullptr == ctx_ptr->per_recv_io_ctx) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "per_io_ctx alloc failed !";
        DBG_ELOG(err_msg_);
        delete ctx_ptr;
        return nullptr;
    }
    if (cumbuffer::OP_RSLT_OK != 
                ctx_ptr->per_recv_io_ctx->cum_buffer.Init(max_data_len_) ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = std::string("cumBuffer(recv) Init error : ") +
            std::string(ctx_ptr->per_recv_io_ctx->cum_buffer.GetErrMsg());
        DBG_ELOG(err_msg_);
        delete ctx_ptr->per_recv_io_ctx;
        delete ctx_ptr;
        return nullptr;
    }
    ReSetCtxPtr(ctx_ptr);
    return ctx_ptr;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::PushClientContextToCache(Context* ctx_ptr)
{
    std::lock_guard<std::mutex> lock(cache_lock_);
    {
        //reset all
        DBG_LOG("sock=" << ctx_ptr->socket << ",sock=" << ctx_ptr->sock_id_copy);
        ReSetCtxPtr(ctx_ptr);
    }
    queue_client_cache_.push(ctx_ptr);
    //std::cout << "queue client cache size = " << queue_client_cache_.size() <<"\n";
    DBG_LOG("queue client cache size = " << queue_client_cache_.size());
}
///////////////////////////////////////////////////////////////////////////////
void ASock::ReSetCtxPtr(Context* ctx_ptr)
{
    DBG_LOG("sock=" << ctx_ptr->socket << ",sock=" << ctx_ptr->sock_id_copy);
    SecureZeroMemory((PVOID)&ctx_ptr->per_recv_io_ctx->overlapped, sizeof(WSAOVERLAPPED));
    ctx_ptr->per_recv_io_ctx->cum_buffer.ReSet();
    ctx_ptr->socket = INVALID_SOCKET;
    ctx_ptr->sock_id_copy = -1;
    ctx_ptr->posted_send_cnt = 0;
    ctx_ptr->recv_issued_cnt = 0;
    ctx_ptr->ref_cnt = 0;
    ctx_ptr->per_recv_io_ctx->wsabuf.buf = ctx_ptr->per_recv_io_ctx->send_buffer;
    ctx_ptr->per_recv_io_ctx->wsabuf.len = sizeof(ctx_ptr->per_recv_io_ctx->send_buffer);
    ctx_ptr->per_recv_io_ctx->io_type = EnumIOType::IO_UNKNOWN;
    memset(ctx_ptr->per_recv_io_ctx->send_buffer, 0x00, 
            sizeof(ctx_ptr->per_recv_io_ctx->send_buffer));
    ctx_ptr->per_recv_io_ctx->total_send_len = 0;
    ctx_ptr->per_recv_io_ctx->complete_recv_len = 0;
    ctx_ptr->per_recv_io_ctx->sent_len = 0;
    ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
    ctx_ptr->is_connected = false;
}
///////////////////////////////////////////////////////////////////////////////
void ASock::ClearClientCache()
{
    LOG("======= clear all cache ========");
    std::lock_guard<std::mutex> lock(cache_lock_);
    while (!queue_client_cache_.empty()) {
        Context* ctx_ptr = queue_client_cache_.front();
        delete ctx_ptr;
        queue_client_cache_.pop();
    }
    DBG_LOG("debug ...... ");
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RunClientThread()
{
    if (!is_client_thread_running_) {

        client_thread_ = std::thread (&ASock::ClientThreadRoutine, this);
        //client_thread.detach();
    }
    return true;
}


///////////////////////////////////////////////////////////////////////////////
void ASock::InvokeServerDisconnectedHandler()
{
    if (cb_on_disconnected_from_server_ != nullptr) {
        cb_on_disconnected_from_server_();
    } else {
        OnDisconnectedFromServer();
    }
}

void ASock::BuildErrMsgString(int err_no)
{
    LPVOID lpMsgBuf;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, WSAGetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    std::lock_guard<std::mutex> lock(err_msg_lock_);
    err_msg_ = std::string("(") + std::to_string(err_no) + std::string(") ");
    err_msg_ += std::string((LPCTSTR)lpMsgBuf);
    LocalFree(lpMsgBuf);
}

///////////////////////////////////////////////////////////////////////////////
bool   ASock::SetSocketNonBlocking(int nSockFd)
{
    // If iMode = 0, blocking is enabled; 
    // If iMode != 0, non-blocking mode is enabled.
    unsigned long nMode = 1;
    int nResult = ioctlsocket(nSockFd, FIONBIO, &nMode);
    if (nResult != NO_ERROR) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return  false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
//for udp server, client only
bool ASock::SetSockoptSndRcvBufUdp(SOCKET_T socket)
{
    /* TODO
    size_t opt_cur;
    int opt_val = max_data_len_;
    int opt_len = sizeof(opt_cur);
    if (getsockopt(socket, SOL_SOCKET, SO_SNDBUF, &opt_cur, (SOCKLEN_T *)&opt_len) == -1) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "gsetsockopt SO_SNDBUF error [" + std::string(strerror(errno)) + "]";
        return false;
    }
    //std::cout << "curr SO_SNDBUF = " << opt_cur << "\n";
    if (max_data_len_ > opt_cur) {
        if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&opt_val, sizeof(opt_val)) == -1) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "setsockopt SO_SNDBUF error [" + std::string(strerror(errno)) + "]";
            return false;
        }
        std::cout << "set SO_SNDBUF = " << opt_val << "\n";
    }
    //--------------
    if (getsockopt(socket, SOL_SOCKET, SO_RCVBUF, &opt_cur, (SOCKLEN_T *)&opt_len) == -1) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "setsockopt SO_RCVBUF error [" + std::string(strerror(errno)) + "]";
        return false;
    }
    //std::cout << "curr SO_RCVBUF = " << opt_cur << "\n";
    if (max_data_len_ > opt_cur) {
        if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&opt_val, sizeof(opt_val)) == -1) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "setsockopt SO_RCVBUF error [" + std::string(strerror(errno)) + "]";
            return false;
        }
        std::cout << "set SO_RCVBUF = " << opt_val << "\n";
    }
    */
    return true;
}
///////////////////////////////////////////////////////////////////////////////
// CLIENT
///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitTcpClient( const char* server_ip, unsigned short server_port,
                            int connect_timeout_secs, size_t  max_data_len)
{
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
///////////////////////////////////////////////////////////////////////////////
bool ASock::ConnectToServer()
{
    if(is_connected_ ){
        return true;
    }
    if (!SetSocketNonBlocking(context_.socket)) {
        closesocket(context_.socket);
        return  false;
    }
    if (!is_buffer_init_) {
        if (cumbuffer::OP_RSLT_OK != 
                context_.per_recv_io_ctx->cum_buffer.Init(max_data_len_)) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = std::string("cumBuffer Init error :") + 
                    context_.per_recv_io_ctx->cum_buffer.GetErrMsg();
            closesocket(context_.socket);
            context_.socket = INVALID_SOCKET;
            DBG_ELOG(err_msg_);
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
    int result = connect(context_.socket,(SOCKADDR *)&tcp_server_addr_, 
                        (SOCKLEN_T )sizeof(SOCKADDR_IN)) ;
    if ( result == SOCKET_ERROR) {
        int last_err = WSAGetLastError();
        if (last_err != WSAEWOULDBLOCK) {
            BuildErrMsgString(last_err);
            ELOG( err_msg_ ) ;
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
    WSAPOLLFD fdarray = { 0 };
    fdarray.fd = context_.socket;
    fdarray.events = POLLWRNORM;
    result = WSAPoll(&fdarray, 1, wait_timeout_ms);
    DBG_LOG("connect WSAPoll returns....: result ="<<result);
    if (SOCKET_ERROR == result) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    if (result) {
        if (fdarray.revents & POLLWRNORM) {
            DBG_LOG("Established connection");
        } else {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "connect timeout";
            DBG_ELOG(err_msg_);
            return false;
        }
    }
    is_connected_ = true;
    return RunClientThread();
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ClientThreadRoutine()
{
    is_client_thread_running_ = true;
    int wait_timeout_ms = 1 * 1000 ;
    WSAPOLLFD fdarray = { 0 };
    while (is_connected_) { 
        if (context_.socket == INVALID_SOCKET) {
            DBG_ELOG( "INVALID_SOCKET" ) ;
            is_client_thread_running_ = false;
            return;
        }
        fdarray.fd = context_.socket;
        int result = -1;
        //fdarray.events = POLLWRNORM;
        //Call WSAPoll for readability on connected socket
        fdarray.events = POLLRDNORM;
        result = WSAPoll(&fdarray, 1, wait_timeout_ms);
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
            if (!is_connected_) {
                is_client_thread_running_ = false;
                return;//no error.
            }
            if (fdarray.revents & POLLRDNORM) {
                //============================
                size_t want_recv_len = max_data_len_;
                //-------------------------------------
                if (max_data_len_ > context_.GetBuffer()->GetLinearFreeSpace()) { 
                    want_recv_len = context_.GetBuffer()->GetLinearFreeSpace();
                }
                if (want_recv_len == 0) {
                    std::lock_guard<std::mutex> lock(err_msg_lock_);
                    err_msg_ = "no linear free space left ";
                    DBG_ELOG(err_msg_);
                    is_client_thread_running_ = false;
                    return ;
                }
                //============================
                // use cumbuffer !
                if (context_.socket == INVALID_SOCKET) {
                    DBG_ELOG("INVALID_SOCKET");
                    is_client_thread_running_ = false;
                    return;
                }
                int ret = recv( context_.socket, 
                                context_.GetBuffer()->GetLinearAppendPtr(),
                                (int) want_recv_len, 0);
                if (SOCKET_ERROR == ret) {
                    BuildErrMsgString(WSAGetLastError());
                    ELOG( err_msg_ ) ;
                    is_client_thread_running_ = false;
                    return;
                } else {
                    DBG_LOG("client recved : " << ret <<" bytes");
                    if (ret == 0) {
                        DBG_LOG("client disconnected : sock=" << context_.socket);
                        shutdown(context_.socket, SD_BOTH);
                        closesocket(context_.socket);
                        OnDisconnectedFromServer();
                        context_.socket = INVALID_SOCKET; //XXX
                        is_client_thread_running_ = false;
                        return;
                    }
                    DBG_LOG("sock=" << context_.socket << ", bytes_transferred = " 
                            << ret);
                    if (!RecvData(0, &context_, ret)) { 
                        DBG_ELOG(err_msg_);
                        shutdown(context_.socket, SD_BOTH);
                        if (0 != closesocket(context_.socket)) {
                            DBG_ELOG("close socket error! : " << WSAGetLastError());
                        }
                        context_.socket = INVALID_SOCKET; 
                        OnDisconnectedFromServer();
                        is_client_thread_running_ = false;
                        return;
                    }
                }
            }
        }
    } //while(true)
    is_client_thread_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
//client use
bool ASock:: SendToServer(const char* data, size_t len)
{
    if ( !is_connected_ ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "not connected";
        DBG_ELOG( err_msg_ ) ;
        return false;
    }
    std::lock_guard<std::mutex> lock(context_.ctx_lock);
    //TODO : async 
    size_t sent_sum = 0;
    while (true) {
        if (context_.socket == INVALID_SOCKET) {
            err_msg_ = "not connected";
            DBG_ELOG(err_msg_);
            return false;
        }
        int result = send(context_.socket, data + sent_sum, (int)(len - sent_sum), 0);
        if (SOCKET_ERROR == result && WSAEWOULDBLOCK != WSAGetLastError()) {
            BuildErrMsgString(WSAGetLastError());
            ELOG( err_msg_ ) ;
            shutdown(context_.socket, SD_BOTH);
            if (0 != closesocket(context_.socket)) {
                DBG_ELOG("close socket error! : " << WSAGetLastError());
            }
            context_.socket = INVALID_SOCKET;
            return false;
        } else {
            sent_sum += result;
            if (sent_sum == len) {
                //DBG_LOG("all sent OK");
                break;
            } else {
                LOG("all sent failed --> send again : total= " << len 
                        << ",sent=" <<sent_sum);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }
    }
    return true;
}



