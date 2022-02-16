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
///////////////////////////////////////////////////////////////////////////////
//TODO 
//SOCK_USAGE_IPC_SERVER , SOCK_USAGE_IPC_CLIENT 
#include "ASock.hpp"

using namespace asock;
///////////////////////////////////////////////////////////////////////////////
ASock::ASock()
{
    is_connected_ = false;
}

ASock::~ASock()
{
    if (sock_usage_ == SockUsage::SOCK_USAGE_TCP_CLIENT ||
        sock_usage_ == SockUsage::SOCK_USAGE_UDP_CLIENT ||
        sock_usage_ == SockUsage::SOCK_USAGE_IPC_CLIENT) {
        Disconnect();
    } else if ( sock_usage_ == SockUsage::SOCK_USAGE_TCP_SERVER ||
                sock_usage_ == SockUsage::SOCK_USAGE_UDP_SERVER ||
                sock_usage_ == SockUsage::SOCK_USAGE_IPC_SERVER) {
        ClearClientCache();
        ClearPerIoDataCache();
    }
    WSACleanup();
}


///////////////////////////////////////////////////////////////////////////////
//server use
bool ASock::SendData(Context* ctx_ptr, const char* data_ptr, size_t len) 
{
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
            DBG_ELOG("sock="<<ctx_ptr->socket<<",close socket error! : " << WSAGetLastError());
        }
        ctx_ptr->socket = INVALID_SOCKET; 
        exit(1);
    }
    DWORD dw_flags = 0;
    DWORD dw_send_bytes = 0;
    char* send_buffer = new char[len];  //XXX alloc new
    memcpy(send_buffer, data_ptr, len); 
    per_send_io_data->wsabuf.buf = send_buffer ; //XXX wsabuf alloc
    per_send_io_data->wsabuf.len = (ULONG)len;
    per_send_io_data->io_type = EnumIOType::IO_SEND;
    per_send_io_data->total_send_len = len;
    per_send_io_data->sent_len = 0;
    int result = 0;

    if (sock_usage_ == SockUsage::SOCK_USAGE_UDP_SERVER) {
        std::lock_guard<std::mutex> lock(context_.ctx_lock); //멀티쓰레드에서 호출 가능하므로 동기화 필요
        result = WSASendTo(ctx_ptr->socket, &(per_send_io_data->wsabuf), 1, &dw_send_bytes, dw_flags,
                           (SOCKADDR*)&ctx_ptr->udp_remote_addr, sizeof(SOCKADDR_IN)
                           , &(per_send_io_data->overlapped), NULL);
    } else {
        // MSDN:
        // WSASend should not be called on the same stream-oriented socket concurrently from different threads, 
        // because some Winsock providers may split a large send request into multiple transmissions, 
        // and this may lead to unintended data interleaving from multiple concurrent send requests 
        // on the same stream-oriented socket.
        std::lock_guard<std::mutex> lock(context_.ctx_lock); //멀티쓰레드에서 호출 가능하므로 동기화 필요
        result = WSASend(ctx_ptr->socket, &(per_send_io_data->wsabuf), 1, &dw_send_bytes, dw_flags, 
                         &(per_send_io_data->overlapped), NULL);
    }
    if (result == 0) {
        //no error occurs and the send operation has completed immediately
        DBG_LOG("!!! WSASend returns 0 !!! ");
    } else if (result == SOCKET_ERROR) {
        if (WSA_IO_PENDING == WSAGetLastError()) {
            //DBG_LOG("!!! WSASend returns --> WSA_IO_PENDING");
        } else {
            int last_err = WSAGetLastError();
            BuildErrMsgString(last_err);
            ELOG("sock="<<ctx_ptr->socket<<",send error : " << err_msg_);
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
    DBG_LOG("sock=" << ctx_ptr->sock_id_copy << ", send_ref_cnt=" << ctx_ptr->send_ref_cnt);
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
//TCP 아닌것은 cumbuffer 로 수신한다
bool ASock::RecvfromData(size_t worker_index, Context* ctx_ptr, DWORD bytes_transferred )
{
    ctx_ptr->total_buffer.IncreaseData(bytes_transferred);
    DBG_LOG("worker="<<worker_index<<",sock="<<ctx_ptr->sock_id_copy<<",GetCumulatedLen = " 
        << ctx_ptr->total_buffer.GetCumulatedLen() 
        << ",bytes_transferred=" << bytes_transferred );
    char* complete_packet_data = new (std::nothrow) char[bytes_transferred]; 
    if (complete_packet_data == nullptr) {
        ELOG("mem alloc failed");
        exit(1);
    }
    if (cumbuffer::OP_RSLT_OK != ctx_ptr->total_buffer. GetData(bytes_transferred, complete_packet_data)) {
        //error !
        {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = ctx_ptr->total_buffer.GetErrMsg();
        }
        delete[] complete_packet_data; 
        DBG_ELOG("error! ");
        //exit(1);
        return false;
    }
    // UDP 이므로 받는 버퍼를 초기화해서, linear free space를 초기화 상태로 
    ctx_ptr->total_buffer.ReSet(); //this is udp. all data has arrived!
    DBG_LOG("worker=" << worker_index << ",sock=" << ctx_ptr->sock_id_copy  
            << ":got complete data [" << complete_packet_data << "]");
    if (cb_on_recved_complete_packet_ != nullptr) {
        //invoke user specific callback
        cb_on_recved_complete_packet_(ctx_ptr, complete_packet_data, bytes_transferred ); //udp
    } else {
        //invoke user specific implementation
        OnRecvedCompleteData(ctx_ptr, complete_packet_data, bytes_transferred); //udp
    }
    delete[] complete_packet_data; 
    return true;
}
///////////////////////////////////////////////////////////////////////////////
// server, client use this
// 이미 lock 잡은 상태임, 중첩 호출시 개별 버퍼로 수신하게 동적 할당 했음
bool ASock::RecvData(PER_IO_DATA* per_io_data, Context* ctx_ptr, DWORD bytes_transferred )
{
    //TODO
    std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock);  //   !!! 
    if (sock_usage_ == SockUsage::SOCK_USAGE_TCP_SERVER) {
        for (auto it = ctx_ptr->deq_recv_issued_id_n_state.begin();
             it != ctx_ptr->deq_recv_issued_id_n_state.end(); ++it) {
            if (it->id == per_io_data->recv_issued_id) {
                //LOG(" <-- sock :" << ctx_ptr->socket <<" (id = " << it->id <<") bytes_transferred ="<< bytes_transferred); //debug
                it->is_recved = true;
                it->recved_len = bytes_transferred;
                it->recved_data = new (std::nothrow) char[bytes_transferred];
                memcpy(it->recved_data, per_io_data->wsabuf.buf, bytes_transferred);//도착한 데이터를 보관한다. 순서없이 도착 가능하므로 일단 보관
                // TODO 일단 고정길이 버퍼 여러개를 만들어서 테스트해보자. cumbuffer 대신..
                break;
            }
        }//for
        //TODO tcp server : 처음 호출한 중첩 recv 가 도착한 경우에만 콜백을 호출한다. 
        int max_fragments_index_canbe_processed = -1; //처리할 수 있는 최대 인덱스. deque의 0 base
        size_t index = 0;
        size_t prev_id = 0;
        for (auto it = ctx_ptr->deq_recv_issued_id_n_state.begin();
             it != ctx_ptr->deq_recv_issued_id_n_state.end(); ++it) {
            if (index ==0 ) {
                if (!it->is_recved) {
                    break; //처음 호출에 수신데이터 없으면 처리 안함, wait
                }
                prev_id = it->id;
                max_fragments_index_canbe_processed = 0;
            } else {
                if (it->is_recved) {
                    if (prev_id + 1 == it->id) {
                        //연속 
                        prev_id = it->id;
                        max_fragments_index_canbe_processed++;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            index++;
        }//for
        //LOG("     (id = " << per_io_data->recv_issued_id << "), max_fragments_index_canbe_processed :"
        //    << max_fragments_index_canbe_processed << " : sock =" << ctx_ptr->socket); //debug
        //if (max_fragments_index_canbe_processed >= 0) {
        //    for (size_t i = 0; i <= max_fragments_index_canbe_processed; i++) {
        //        LOG("    len = " << ctx_ptr->deq_recv_issued_id_n_state[i].recved_len);
        //    }
        //} //debug
        if (max_fragments_index_canbe_processed >= 0) { //처음부터 연속된 데이터 모았다. 처리 가능
            for (size_t i = 0; i <= max_fragments_index_canbe_processed; i++) {
                //TODO 버퍼 요량 full 안나게 처리 필요
                if (cumbuffer::OP_RSLT_OK != ctx_ptr->total_buffer.Append(ctx_ptr->deq_recv_issued_id_n_state[i].recved_len,
                                                                          ctx_ptr->deq_recv_issued_id_n_state[i].recved_data)) {
                    ELOG("buffer append error : sock =" << ctx_ptr->socket << " :" << ctx_ptr->total_buffer.GetErrMsg());
                    ELOG("ctx_ptr->deq_recv_issued_id_n_state.size = " << ctx_ptr->deq_recv_issued_id_n_state.size());
                    for (size_t j = 0; j <= ctx_ptr->deq_recv_issued_id_n_state.size(); j++) {
                        ELOG("    len = " << ctx_ptr->deq_recv_issued_id_n_state[j].recved_len);
                    }
                    exit(1); //TODO
                }
                DBG_LOG("delete ctx_ptr->deq_recv_issued_id_n_state[i].recved_data");
                delete[] ctx_ptr->deq_recv_issued_id_n_state[i].recved_data; //XXX
            }
            //LOG("deq_recv_issued_id_n_state SIZE(before) ==>" << ctx_ptr->deq_recv_issued_id_n_state.size());
            for (size_t i = 0; i <= max_fragments_index_canbe_processed; i++) {
                //LOG("pop front..");
                ctx_ptr->deq_recv_issued_id_n_state.pop_front();
            }
            //LOG("  *** GetCumulatedLen ==>" << ctx_ptr->total_buffer.GetCumulatedLen()); //debug
        }
        //LOG("deq_recv_issued_id_n_state SIZE ==>" << ctx_ptr->deq_recv_issued_id_n_state.size());
    } else {
        //cumbuffer 로 받은 것 처리. TODO 헷갈림
        ctx_ptr->total_buffer.IncreaseData(bytes_transferred);
    }
    
    while (ctx_ptr->total_buffer.GetCumulatedLen()) {
        if (!ctx_ptr->is_packet_len_calculated){ 
            //only when calculation is necessary
            if (cb_on_calculate_data_len_ != nullptr) {
                //invoke user specific callback
                ctx_ptr->complete_recv_len = cb_on_calculate_data_len_(ctx_ptr);
            } else {
                //invoke user specific implementation
                ctx_ptr->complete_recv_len = OnCalculateDataLen(ctx_ptr);
            }
            ctx_ptr->is_packet_len_calculated = true;
        }
        if (ctx_ptr->complete_recv_len == asock::MORE_TO_COME) {
            ctx_ptr->is_packet_len_calculated = false;
            return true; //need to recv more
        } else if (ctx_ptr->complete_recv_len > 
                   ctx_ptr->total_buffer.GetCumulatedLen()) {
            return true; //need to recv more
        } else {
            //got complete packet 
            size_t alloc_len = ctx_ptr->complete_recv_len;
            char* complete_packet_data = new (std::nothrow) char [alloc_len] ; 
            if(complete_packet_data == nullptr) {
                ELOG("mem alloc failed");
                exit(1);
            }
            if (cumbuffer::OP_RSLT_OK !=
                ctx_ptr->total_buffer.GetData(
                    ctx_ptr->complete_recv_len, complete_packet_data)) {
                //error !
                {
                    std::lock_guard<std::mutex> lock(err_msg_lock_);
                    err_msg_ = ctx_ptr->total_buffer.GetErrMsg();
                    ELOG(err_msg_);
                }
                ctx_ptr->is_packet_len_calculated = false;
                delete[] complete_packet_data; 
                //exit(1);
                exit(1);
            }
            if (cb_on_recved_complete_packet_ != nullptr) {
                //invoke user specific callback
                cb_on_recved_complete_packet_(ctx_ptr, complete_packet_data, ctx_ptr->complete_recv_len);
            } else {
                //invoke user specific implementation
                OnRecvedCompleteData(ctx_ptr, complete_packet_data, ctx_ptr->complete_recv_len);
            }
            delete[] complete_packet_data; 
            ctx_ptr->is_packet_len_calculated = false;
        }
    } //while
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// 서버만 사용하는 함수
// 호출시 이미 lock 을 잡은 상태임. std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock); 
bool ASock::IssueRecv( Context* ctx_ptr)
{
    //--------------------------------------------------------
    // wsarecv 를 호출해놓고 데이터 수신을 기다린다. 
    // client 가 보내거나 연결을 종료할때만 GetQueuedCompletionStatus 로 통지된다.
    // -------------------------------------------------------
    // MSDN :
    // WSARecv should not be called on the same socket simultaneously from different threads, 
    // because it can result in an unpredictable buffer order.
    //--------------------------------------------------------

    ////////////////////////////////////
    PER_IO_DATA*  per_recv_io = PopPerIoDataFromCache(); //XXX
    DWORD dw_flags = 0;
    DWORD dw_recv_bytes = 0;
    SecureZeroMemory((PVOID)&per_recv_io->overlapped, sizeof(WSAOVERLAPPED));
    per_recv_io->io_type = EnumIOType::IO_RECV;

    int result = -1;
    ////////////////////////////////////
    if (sock_usage_ == SockUsage::SOCK_USAGE_TCP_SERVER ) {
       //TODO 중첩 recv 처리되고 있으므로 (IssueRecv이 중복으로 호출됬댜) 별개 버퍼 할당
        per_recv_io->wsabuf.buf = new (std::nothrow) char[max_data_len_]; //TODO
        if (per_recv_io->wsabuf.buf == nullptr) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "per_io_data alloc failed !";
            ELOG(err_msg_);
            return false;
        }
        per_recv_io->wsabuf.len = (ULONG)max_data_len_;

        //---------------------------------------------------- lock
        std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock);  //   !!! 
        //---------------------------------------------------- lock
        per_recv_io->recv_issued_id = ctx_ptr->cur_recv_issued_id ;
        RECV_ISSUED_ID_N_STATE recv_issue_id_n_state;
        recv_issue_id_n_state.id = ctx_ptr->cur_recv_issued_id; //이값은 순차적 증가값이다.
        recv_issue_id_n_state.is_recved = false;
        ctx_ptr->deq_recv_issued_id_n_state.push_back(recv_issue_id_n_state);
        ctx_ptr->recv_ref_cnt++; // 비동기 : WSARecv 호출전에 증가 필요. 
        //LOG("recv_ref_cnt =" << ctx_ptr->recv_ref_cnt); //debuf
        //LOG("--> sock :"<< ctx_ptr->socket <<" (id = "<< recv_issue_id_n_state.id << "), per_recv_io->wsabuf.len =" << per_recv_io->wsabuf.len); //debug
        result = WSARecv(ctx_ptr->socket, &(per_recv_io->wsabuf),
                         1, &dw_recv_bytes, &dw_flags,
                         &(per_recv_io->overlapped), NULL);
        if (0 == result) {
            //TODO 
            //LOG("wsarecv completes immediately : dw_recv_bytes=" << dw_recv_bytes 
            //    << " / recv_issued_id =" << ctx_ptr->cur_recv_issued_id); //debug
            // https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsarecv
            //-> If an overlapped operation completes immediately, WSARecv returns a value of zero 
            //   and the lpNumberOfBytesRecvd parameter is updated with the number of bytes 
            //   received and the flag bits indicated by the lpFlags parameter are also updated
        } else if (SOCKET_ERROR == result) {
            if (WSAGetLastError() != WSA_IO_PENDING) {
                BuildErrMsgString(WSAGetLastError());
                ctx_ptr->recv_ref_cnt--; 
                PushPerIoDataToCache(per_recv_io);
                ELOG("sock=" << ctx_ptr->socket << ", " << err_msg_ << ", recv_ref_cnt =" << ctx_ptr->recv_ref_cnt);
                shutdown(ctx_ptr->socket, SD_BOTH);
                if (0 != closesocket(ctx_ptr->socket)) {
                    DBG_ELOG("sock=" << ctx_ptr->socket << ",close socket error! : " << last_err);
                }
                ctx_ptr->socket = INVALID_SOCKET;
                return false;
            }
            //LOG("wsarecv WSA_IO_PENDING / recv_issued_id =" << ctx_ptr->cur_recv_issued_id); //debug
        }
        //LOG("recv_ref_cnt =" << ctx_ptr->recv_ref_cnt); //debuf
        ctx_ptr->cur_recv_issued_id++; 
    } else {
        //TCP 서버 아닌 경우, 중첩 수신 없고, cumbuffer 로 받게 처리
        //cumbuffer 에 Append 가능할 만큼만 수신하게 해줘야함!!
        size_t want_recv_len = max_data_len_;
        //-------------------------------------
        if (max_data_len_ > ctx_ptr->total_buffer.GetLinearFreeSpace()) {
            want_recv_len = ctx_ptr->total_buffer.GetLinearFreeSpace();
        }
        if (want_recv_len == 0) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "no linear free space left ";
            ELOG(err_msg_);
            shutdown(ctx_ptr->socket, SD_BOTH);
            if (0 != closesocket(ctx_ptr->socket)) {
                DBG_ELOG("sock=" << ctx_ptr->socket << ",close socket error! : " << last_err);
            }
            ctx_ptr->socket = INVALID_SOCKET;
            return false;
        }
        //cumbuffer 에 가능한 만큼만 ...
        per_recv_io->wsabuf.buf = ctx_ptr->total_buffer.GetLinearAppendPtr();
        per_recv_io->wsabuf.len = (ULONG)want_recv_len;
        SOCKLEN_T addrlen = sizeof(context_.udp_remote_addr);
        result = WSARecvFrom(ctx_ptr->socket, &(per_recv_io->wsabuf),
                             1, (LPDWORD)&dw_recv_bytes, (LPDWORD)&dw_flags,
                             (struct sockaddr*)& ctx_ptr->udp_remote_addr, 
                             &addrlen, &(per_recv_io->overlapped), NULL);
        if (SOCKET_ERROR == result) {
            if (WSAGetLastError() != WSA_IO_PENDING) {
                BuildErrMsgString(WSAGetLastError());
                shutdown(ctx_ptr->socket, SD_BOTH);
                if (0 != closesocket(ctx_ptr->socket)) {
                    DBG_ELOG("sock=" << ctx_ptr->socket << ",close socket error! : " << last_err);
                }
                ctx_ptr->socket = INVALID_SOCKET;
                ELOG("sock=" << ctx_ptr->sock_id_copy << ", " << err_msg_ << ", recv_ref_cnt =" << ctx_ptr->recv_ref_cnt);
                return false;
            }
            //LOG("wsarecv WSA_IO_PENDING / recv_issued_id =" << ctx_ptr->cur_recv_issued_id); //debug
        }
    }
    ////////////////////////////////////
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// SERVER
///////////////////////////////////////////////////////////////////////////////
 
///////////////////////////////////////////////////////////////////////////////
bool ASock::InitUdpServer(const char* bind_ip, 
                          size_t      bind_port, 
                          size_t      max_data_len /*=DEFAULT_PACKET_SIZE*/,
                          size_t      max_client /*=DEFAULT_MAX_CLIENT*/)
{
    sock_usage_ = SockUsage::SOCK_USAGE_UDP_SERVER  ;
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

///////////////////////////////////////////////////////////////////////////////
bool ASock::InitTcpServer(  const char* bind_ip, int bind_port, 
                            size_t  max_data_len , size_t  max_client )
{
    sock_usage_ = SockUsage::SOCK_USAGE_TCP_SERVER;
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
        err_msg_ = "error [server is already running]";
        ELOG("error! " << err_msg_); 
        return false;
    }
    if(!InitWinsock()) {
        return false;
    }
    handle_completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if(max_data_len_ <0) {
        err_msg_ = "init error [packet length is negative]";
        DBG_ELOG(err_msg_);
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

///////////////////////////////////////////////////////////////////////////////
void ASock::StartWorkerThreads()
{
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    max_worker_cnt_ = system_info.dwNumberOfProcessors * 2;
    DBG_LOG("(server) worker cnt = " << max_worker_cnt_);
    for (size_t i = 0; i < max_worker_cnt_ ; i++) {
        std::thread worker_thread(&ASock::WorkerThreadRoutine, this, i); 
        worker_thread.detach();
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::StartServer()
{
    DBG_LOG("PER_IO_DATA size ==> " << sizeof(PER_IO_DATA));
    if ( sock_usage_ == SockUsage::SOCK_USAGE_IPC_SERVER ) {
        // TODO
        //listen_socket_ = WSASocketW(AF_UNIX, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    } else if ( sock_usage_ == SockUsage::SOCK_USAGE_TCP_SERVER ) {
        listen_socket_ = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    } else if ( sock_usage_ == SockUsage::SOCK_USAGE_UDP_SERVER ) {
        listen_socket_ = WSASocketW(AF_INET, SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    }
    if (listen_socket_ == INVALID_SOCKET) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    if(!SetSocketNonBlocking (listen_socket_)) {
        return  false;
    }
    int opt_zero = 0;
    if ( sock_usage_ == SockUsage::SOCK_USAGE_UDP_SERVER ) {
        if(!SetSockoptSndRcvBufUdp(listen_socket_)) {
            return false;
        }
    }
    int opt_on = 1;
    if (setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt_on, sizeof(opt_on)) == SOCKET_ERROR) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    if (sock_usage_ != SockUsage::SOCK_USAGE_UDP_SERVER) {
        if (setsockopt(listen_socket_, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt_on, sizeof(opt_on)) == SOCKET_ERROR) {
            BuildErrMsgString(WSAGetLastError());
            ELOG(err_msg_);
            return false;
        }
    }

    int result = -1;
    if (sock_usage_ == SockUsage::SOCK_USAGE_IPC_SERVER) {
        //TODO
    }
    else if (sock_usage_ == SockUsage::SOCK_USAGE_TCP_SERVER || sock_usage_ == SockUsage::SOCK_USAGE_UDP_SERVER) {
        SOCKADDR_IN    server_addr  ;
        memset((void *)&server_addr,0x00,sizeof(server_addr)) ;
        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, server_ip_.c_str(), &(server_addr.sin_addr));
        server_addr.sin_port = htons(server_port_);
        result = bind(listen_socket_, (SOCKADDR*)&server_addr, sizeof(server_addr));
    }
    if (result < 0) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    if (sock_usage_ == SockUsage::SOCK_USAGE_IPC_SERVER || sock_usage_ == SockUsage::SOCK_USAGE_TCP_SERVER) {
        result = listen(listen_socket_, SOMAXCONN);
        if (result < 0) {
            BuildErrMsgString(WSAGetLastError());
            ELOG(err_msg_);
            return false;
        }
    }
    if (!BuildClientContextCache()) {
        return false;
    }
    if (!BuildPerIoDataCache()) {
        return false;
    }
    // start server thread
    if ( sock_usage_ == SockUsage::SOCK_USAGE_UDP_SERVER ) {
        //UDP is special, no thread.
        SOCKLEN_T socklen = sizeof(SOCKADDR_IN);
        Context* ctx_ptr = PopClientContextFromCache();
        if (ctx_ptr == nullptr) {
            ELOG(err_msg_);
            exit(1);
        }
        ctx_ptr->is_connected = true;
        ctx_ptr->socket = listen_socket_;
        handle_completion_port_ = CreateIoCompletionPort((HANDLE)listen_socket_, handle_completion_port_, (ULONG_PTR)ctx_ptr, 0);
        if (NULL == handle_completion_port_) {
            BuildErrMsgString(WSAGetLastError());
            ELOG(err_msg_);
            ELOG("alloc failed! delete ctx_ptr");
            delete ctx_ptr;
            exit(1);
        }
        // Start receiving.
        if (!IssueRecv( ctx_ptr)) { // UDP server 
            int last_err = WSAGetLastError();
            DBG_ELOG("sock=" << ctx_ptr->socket <<  ", error! : " << last_err);
            shutdown(ctx_ptr->socket, SD_BOTH);
            if (0 != closesocket(ctx_ptr->socket)) {
                DBG_ELOG("sock=" << ctx_ptr->socket << ",close socket error! : " << last_err);
            }
            LOG("delete ctx_ptr , sock=" << ctx_ptr->socket);
            ctx_ptr->socket = INVALID_SOCKET; 
            if (last_err == WSAECONNRESET) {
                ELOG("invoke PuchClientContextToCache.. sock=" << ctx_ptr->sock_id_copy);
                PushClientContextToCache(ctx_ptr);
            }
            exit(1);
        }
    }
    else {
        std::thread server_thread(&ASock::AcceptThreadRoutine, this);
        server_thread.detach();
    }
    is_server_running_ = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock:: AcceptThreadRoutine()
{
    SOCKLEN_T socklen=sizeof(SOCKADDR_IN);
    SOCKADDR_IN client_addr;
    while(is_need_server_run_) {
        //TODO : WSAAcceptEx 인 경우 SO_UPDATE_ACCEPT_CONTEXT 반드시 설정
        SOCKET_T client_sock = WSAAccept(listen_socket_, (SOCKADDR*)&client_addr, &socklen, 0, 0);
        if (client_sock == INVALID_SOCKET) {
            int last_err = WSAGetLastError();
            if (WSAEWOULDBLOCK == last_err) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            BuildErrMsgString(last_err);
            if (!is_need_server_run_) {
                ELOG(err_msg_);
                exit(1);
            } else {
                break;
            }
        }
        client_cnt_++;
        SetSocketNonBlocking(client_sock);
        //LOG("pop ctx : accept client");
        Context* ctx_ptr = PopClientContextFromCache();
        if (ctx_ptr == nullptr) {
            ELOG(err_msg_);
            exit(1);
        }
        ctx_ptr->is_connected = true;  
        ctx_ptr->socket = client_sock; 
        ctx_ptr->sock_id_copy = client_sock; //for debuggin
#ifdef DEBUG_PRINT
        DBG_LOG("sock="<< client_sock << ", cumbuffer debug=");
#endif
        //TODO multi wsarecv
        handle_completion_port_ = CreateIoCompletionPort((HANDLE)client_sock, handle_completion_port_, (ULONG_PTR)ctx_ptr, 0); 
        if (NULL == handle_completion_port_) {
            BuildErrMsgString(WSAGetLastError());
            ELOG(err_msg_ );
            delete ctx_ptr;
            exit(1); 
        }
        // iocp 초기화 완료후 사용자의 콜백을 호출해야 함. 사용자가 콜백에서 send를 시도할수 있기 때문.
        DBG_LOG("**** accept returns...new client : sock="<<client_sock << " => client total = "<< client_cnt_ );
        if (cb_on_client_connected_ != nullptr) {
            cb_on_client_connected_(ctx_ptr);
        } else {
            OnClientConnected(ctx_ptr);
        }
        //-----------------------------------------------------------------
        // Start receiving.
        // multiple recv
        //LOG("============================================"); //debug
        bool is_error = false;
        { //lock scope start
            //std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock); // TODO check : multiple wsarecv 를 호출하므로
            for (int i = 0; i < max_overlapped_recv_cnt_; i++) {
                if (!IssueRecv( ctx_ptr)) {  // TCP server 
                    is_error = true;
                    break;
                }
            }
        } //lock scope ends
        //LOG(" -------------------------------------------- all initial IssueRecv invoked....."); //debug
        if (is_error) {
            client_cnt_--;
            ELOG("sock=" << ctx_ptr->socket << ", error! : " << err_msg_);
            PushClientContextToCache(ctx_ptr);
            continue;
        }
        //-----------------------------------------------------------------
    } //while
    DBG_LOG("accept thread exiting");
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//io 작업 결과에 대해 처리한다. --> multi thread 에 의해 수행된다 !!!
void ASock:: WorkerThreadRoutine(size_t worker_index) {
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
        if (FALSE == GetQueuedCompletionStatus( handle_completion_port_, 
                                            & bytes_transferred, (PULONG_PTR) & ctx_ptr, 
                                            (LPOVERLAPPED*) &lp_overlapped, INFINITE )) {
            int err = WSAGetLastError();
            if (err == ERROR_ABANDONED_WAIT_0 || err== ERROR_INVALID_HANDLE) {
                ELOG("ERROR_ABANDONED_WAIT_0");
                return;
            }
            BuildErrMsgString(err);
            if (lp_overlapped != NULL) {
                //===================== lock
                ELOG("worker=" << worker_index << ", sock=" << ctx_ptr->sock_id_copy << ", GetQueuedCompletionStatus failed.."
                    << ", bytes = " << bytes_transferred << ", err=" << err_msg_ );
                per_io_ctx = (PER_IO_DATA*)lp_overlapped;
                if (per_io_ctx->io_type == EnumIOType::IO_SEND) {
                    ctx_ptr->send_ref_cnt--;
                } else if (per_io_ctx->io_type == EnumIOType::IO_RECV) {
                    ctx_ptr->recv_ref_cnt--;
                }
                if (bytes_transferred == 0) {
                    //graceful disconnect.
                    LOG("sock="<<ctx_ptr->sock_id_copy <<", 0 recved --> gracefully disconnected : " << ctx_ptr->recv_ref_cnt);
                    if (per_io_ctx->io_type == EnumIOType::IO_RECV) {
                        if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                            TerminateClient(ctx_ptr, worker_index);
                        }
                    } else if (per_io_ctx->io_type == EnumIOType::IO_SEND) {
                        LOG("io_send terminate client");
                        TerminateClient(ctx_ptr, worker_index);
                    }
                } else {
                    if (err == ERROR_NETNAME_DELETED) { // --> 64
                        //client hard close --> not an error
                        LOG("worker=" << worker_index << ",sock=" << ctx_ptr->sock_id_copy << " : client hard close. ERROR_NETNAME_DELETED ");
                    } else {
                        DBG_ELOG(" GetQueuedCompletionStatus failed  :" << err_msg_);
                    }
                    if (per_io_ctx->io_type == EnumIOType::IO_RECV) {
                        if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                            TerminateClient(ctx_ptr, worker_index, false); //force close
                        }
                    } else if (per_io_ctx->io_type == EnumIOType::IO_SEND) {
                        PushPerIoDataToCache(per_io_ctx); //XXX 
                        LOG("io_send terminate client");
                        TerminateClient(ctx_ptr, worker_index, false); //force close
                    }
                }
            } else {
                ELOG("GetQueuedCompletionStatus failed (lp_overlapped null) err="<< err);
                if (per_io_ctx->io_type == EnumIOType::IO_RECV) {
                    if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                        TerminateClient(ctx_ptr,worker_index,  false); //force close
                    }
                }
                else if (per_io_ctx->io_type == EnumIOType::IO_SEND) {
                    LOG("io_send terminate client");
                    TerminateClient(ctx_ptr, worker_index, false); //force close
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
            //LOG("<--- worker=" << worker_index << ",IO_RECV: sock=" << ctx_ptr->sock_id_copy
            //    << ", recv_ref_cnt =" << ctx_ptr->recv_ref_cnt << ", recved bytes =" << bytes_transferred
            //    <<",recv_issued_id = " << per_io_ctx->recv_issued_id);
            //# recv #---------- 
            if (bytes_transferred == 0) {
                //graceful disconnect.
                if (sock_usage_ == SockUsage::SOCK_USAGE_TCP_SERVER) {
                    PushPerIoDataToCache(per_io_ctx); //XXX 중첩 호출시마다 동적 할당했다!!!
                }
                if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                    TerminateClient(ctx_ptr, worker_index);
                }
                break;
            }
            bool result = false;
            if (sock_usage_ == SockUsage::SOCK_USAGE_UDP_SERVER ) {
                RecvfromData(per_io_ctx->recv_issued_id, ctx_ptr, bytes_transferred);
                // Continue to receive messages.
                if (!IssueRecv( ctx_ptr)) {
                    if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                        client_cnt_--;
                        PushClientContextToCache(ctx_ptr);
                    } else {
                        ELOG("XXX issue recv error : recv ref count =" << ctx_ptr->recv_ref_cnt);
                    }
                    break;
                }
            } else {
                //XXX TCP 는 중첩된 recv 를 수행하므로 lock 필요
                //--------------------------------------------------- lock !!!
                //std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock);  //   !!! 
                //--------------------------------------------------- lock !!!
                RecvData(per_io_ctx, ctx_ptr, bytes_transferred);
                PushPerIoDataToCache(per_io_ctx); //XXX 중첩 호출시마다 동적 할당했다!!!
                // Continue to receive messages.
                if (!IssueRecv(ctx_ptr)) {
                    if (ctx_ptr->recv_ref_cnt == 0) { // 중첩된 wsarecv 등을 고려한다
                        LOG("PushClientContextToCache : sock ="<<ctx_ptr->sock_id_copy);
                        PushClientContextToCache(ctx_ptr);
                    } else {
                        ELOG("XXX issue recv error : recv ref count =" << ctx_ptr->recv_ref_cnt);
                    }
                    break;
                }
            }
        }
        break;
        //======================================================= IO_SEND
        case EnumIOType::IO_SEND:
        {
            ctx_ptr->send_ref_cnt--;
            if (ctx_ptr->socket == INVALID_SOCKET) {
                DBG_LOG("INVALID_SOCKET : delete.sock=" << ctx_ptr->sock_id_copy << ", send_ref_cnt= " << ctx_ptr->send_ref_cnt);
                PushPerIoDataToCache(per_io_ctx);
                break;
            }
            DBG_LOG("worker=" << worker_index << ",IO_SEND: sock=" << ctx_ptr->socket << ", send_ref_cnt =" << ctx_ptr->send_ref_cnt);
            per_io_ctx->sent_len += bytes_transferred;
            DBG_LOG("IO_SEND(sock=" << ctx_ptr->socket << ") : sent this time ="
                    << bytes_transferred << ",total sent =" << per_io_ctx->sent_len );

            if (sock_usage_ == SockUsage::SOCK_USAGE_UDP_SERVER) {
                // udp , no partial send
                DBG_LOG("socket (" << ctx_ptr->socket <<
                        ") send all completed (" << per_io_ctx->sent_len << ") ==> delete per io ctx!!");
                PushPerIoDataToCache(per_io_ctx);
            } else {
                // non udp
                if (per_io_ctx->sent_len < per_io_ctx->total_send_len) {
                    //남은 부분 전송
                    DBG_LOG("socket (" << ctx_ptr->socket << ") send partially completed, total=" << per_io_ctx->total_send_len
                            << ",partial= " << per_io_ctx->sent_len << ", send again");
                    SecureZeroMemory((PVOID)&per_io_ctx->overlapped, sizeof(WSAOVERLAPPED));
                    DWORD dw_flags = 0;
                    DWORD dw_send_bytes = 0;
                    per_io_ctx->wsabuf.buf += bytes_transferred;
                    per_io_ctx->wsabuf.len -= bytes_transferred;
                    per_io_ctx->io_type = EnumIOType::IO_SEND;
                    //----------------------------------------
                    // MSDN:
                    // WSASend should not be called on the same stream-oriented socket concurrently from different threads, 
                    // because some Winsock providers may split a large send request into multiple transmissions, 
                    // and this may lead to unintended data interleaving from multiple concurrent send requests 
                    // on the same stream-oriented socket.
                    std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock); 
                    int result = WSASend(ctx_ptr->socket, &per_io_ctx->wsabuf, 1, 
                                         &dw_send_bytes, dw_flags, &(per_io_ctx->overlapped), NULL);
                    if (result == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
                        client_cnt_--;
                        ELOG("send failed. client count = " << client_cnt_);
                        PushPerIoDataToCache(per_io_ctx);
                        int last_err = WSAGetLastError();
                        BuildErrMsgString(last_err);
                        ELOG("WSASend() failed: " << err_msg_);
                        shutdown(ctx_ptr->socket, SD_BOTH);
                        if (0 != closesocket(ctx_ptr->socket)) {
                            DBG_ELOG("sock=" << ctx_ptr->socket << ",close socket error! : " << last_err);
                        }
                        ctx_ptr->socket = INVALID_SOCKET;
                        break;
                    }
                    ctx_ptr->send_ref_cnt++;
                } else {
                    DBG_LOG("socket (" << ctx_ptr->socket <<
                            ") send all completed (" << per_io_ctx->sent_len << ") ==> delete per io ctx!!");
                    PushPerIoDataToCache(per_io_ctx);
                }
            }
        }
        break;
        //======================================================= IO_RECV
        case EnumIOType::IO_QUIT :
        {
            DBG_LOG( "IO_QUIT "); 
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


///////////////////////////////////////////////////////////////////////////////
void  ASock::TerminateClient( Context* ctx_ptr,size_t worker_index, bool is_graceful)
{
    //LOG(" sock=" << ctx_ptr->sock_id_copy
    //        << ", recv_ref_cnt=" << ctx_ptr->recv_ref_cnt
    //        << ", connection closing ,graceful="
    //        << (is_graceful ? "TRUE" : "FALSE"));
    ctx_ptr->is_connected = false;
    if (cb_on_client_connected_ != nullptr) {
        cb_on_client_disconnected_(ctx_ptr);
    } else {
        OnClientDisconnected(ctx_ptr);
    }
    if(ctx_ptr->socket == INVALID_SOCKET){
        //LOG(" sock=" << ctx_ptr->sock_id_copy ); //debug
    } else {
        //LOG("-> after client count ="<<client_cnt_);
        if (!is_graceful) {
            // force the subsequent closesocket to be abortative.
            LINGER  linger_struct;
            linger_struct.l_onoff = 1;
            linger_struct.l_linger = 0;
            setsockopt(ctx_ptr->socket, SOL_SOCKET, SO_LINGER, (char*)&linger_struct, sizeof(linger_struct));
        }
        //LOG("worker = " << worker_index << ",close socket : " << ctx_ptr->socket << " / " << ctx_ptr->sock_id_copy
        //        << ", recv_ref_cnt = " << ctx_ptr->recv_ref_cnt );
        shutdown(ctx_ptr->socket, SD_BOTH); //XXX 이부분을 아래 로 옮기면 에러 -- ;;;
        if (0 != closesocket(ctx_ptr->socket)) {
            // TODO XXX 이 부분에서 에러 발생 하면 안됨!!!!  why ? 
            //TODO 이미 다른 worker 가 종료한것을 다른 worker 가 중복 처리하면서 에러 발생하는 문제....  why ? 
            //if (ctx_ptr->socket != INVALID_SOCKET) { 
            ELOG("worker = " << worker_index << ", sock=" << ctx_ptr->sock_id_copy << ",close socket error! : " << WSAGetLastError());
        } else{
            client_cnt_--;
            ctx_ptr->socket = INVALID_SOCKET;
            PushClientContextToCache(ctx_ptr); //XXX ctx 는 client 단위임.
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
    is_need_server_run_ = false;
    for (size_t i = 0; i < max_worker_cnt_; i++) {
        DBG_LOG("PostQueuedCompletionStatus");
        DWORD       bytes_transferred = 0;
        Context* ctx_ptr = PopClientContextFromCache();
        PER_IO_DATA* per_recv_io_ctx = PopPerIoDataFromCache();
        if (nullptr == per_recv_io_ctx) {
            ELOG("memory alloc failed");
            return;
        }
        SecureZeroMemory((PVOID)&per_recv_io_ctx->overlapped, sizeof(WSAOVERLAPPED));
        per_recv_io_ctx->io_type = EnumIOType::IO_QUIT;
        if (!PostQueuedCompletionStatus(handle_completion_port_, bytes_transferred,
                                        (ULONG_PTR)ctx_ptr,
                                        (LPOVERLAPPED) & (per_recv_io_ctx->overlapped))) {
            BuildErrMsgString(WSAGetLastError());
            DBG_ELOG(err_msg_);
        }
    }
    if (sock_usage_ == SockUsage::SOCK_USAGE_TCP_SERVER) {
        closesocket(listen_socket_);
    }
}

///////////////////////////////////////////////////////////////////////////////
PER_IO_DATA* ASock::PopPerIoDataFromCache() {
    PER_IO_DATA* per_io_data_ptr = nullptr;
    std::lock_guard<std::mutex> lock(per_io_data_cache_lock_);
    if (!queue_per_io_data_cache_.empty()) {
        per_io_data_ptr = queue_per_io_data_cache_.front();
        queue_per_io_data_cache_.pop();
        DBG_LOG("queue_per_io_data_cache_ not empty! -> " << "queue_per_io_data_cache_ client cache size = "
                << queue_per_io_data_cache_.size());
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

///////////////////////////////////////////////////////////////////////////////
void ASock::PushPerIoDataToCache(PER_IO_DATA* per_io_data_ptr) {
    std::lock_guard<std::mutex> lock(per_io_data_cache_lock_);
    delete [] per_io_data_ptr->wsabuf.buf;
    SecureZeroMemory((PVOID)&per_io_data_ptr->overlapped, sizeof(WSAOVERLAPPED));
    per_io_data_ptr->wsabuf.buf = NULL;
    per_io_data_ptr->wsabuf.len = 0;
    per_io_data_ptr->io_type = EnumIOType::IO_UNKNOWN;
    per_io_data_ptr->total_send_len = 0;
    per_io_data_ptr->sent_len = 0;
    //per_io_data_ptr->complete_recv_len = 0;
    //per_io_data_ptr->cum_buffer.ReSet(); 
    queue_per_io_data_cache_.push(per_io_data_ptr);
    DBG_LOG("queue_per_io_data_cache_ client cache size = " << queue_per_io_data_cache_.size());
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ClearPerIoDataCache() {
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

///////////////////////////////////////////////////////////////////////////////
bool ASock::BuildPerIoDataCache() {
    DBG_LOG("queue_per_io_data_cache_ alloc ");
    PER_IO_DATA* per_io_data_ptr = nullptr;
    for (int i = 0; i < max_client_limit_ ; i++) { 
        per_io_data_ptr = new (std::nothrow) PER_IO_DATA;
        if (nullptr == per_io_data_ptr) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "per_io_data alloc failed !";
            DBG_ELOG(err_msg_);
            return false;
        }
        // send 에서만 사용되므로 현재 cumbuffer 불필요.
        //if (cumbuffer::OP_RSLT_OK != per_io_data_ptr->cum_buffer.Init(max_data_len_)) {
        //    std::lock_guard<std::mutex> lock(err_msg_lock_);
        //    err_msg_ = std::string("cumBuffer(recv) Init error : ") +
        //        std::string(per_io_data_ptr->cum_buffer.GetErrMsg());
        //    DBG_ELOG(err_msg_);
        //    delete per_io_data_ptr;
        //    return false;
        //}
        per_io_data_ptr->wsabuf.buf = NULL;
        per_io_data_ptr->wsabuf.len = 0 ;
        per_io_data_ptr->io_type = EnumIOType::IO_UNKNOWN;
        per_io_data_ptr->total_send_len = 0;
        //per_io_data_ptr->complete_recv_len = 0;
        per_io_data_ptr->sent_len = 0;
        //per_io_data_ptr->is_packet_len_calculated = false;
        SecureZeroMemory((PVOID)&per_io_data_ptr->overlapped, sizeof(WSAOVERLAPPED));
        queue_per_io_data_cache_.push(per_io_data_ptr);
    }
    DBG_LOG("current queue_per_io_data_cache_ size =" << queue_per_io_data_cache_.size());
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::BuildClientContextCache() {
    LOG("queue_client_cache alloc ");
    Context* ctx_ptr = nullptr;
    for (int i = 0; i < max_client_limit_; i++) {
        ctx_ptr = new (std::nothrow) Context();
        if (nullptr == ctx_ptr) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = "Context alloc failed !";
            return false;
        }
        //ctx_ptr->per_recv_io_ctx = new (std::nothrow) PER_IO_DATA; 
        //if (nullptr == ctx_ptr->per_recv_io_ctx) {
        //    std::lock_guard<std::mutex> lock(err_msg_lock_);
        //    err_msg_ = "per_io_ctx alloc failed !";
        //    DBG_ELOG(err_msg_);
        //    delete ctx_ptr;
        //    return false;
        //}
        //TODO tcp 서버는 중첩수신 갯수에 따라 누적된 크기가 훨씬 클수 있으므로,크기를 어떻게 ?
        if (cumbuffer::OP_RSLT_OK != ctx_ptr->total_buffer.Init(max_data_len_)) { 
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = std::string("cumBuffer(recv) Init error : ") +
                std::string(ctx_ptr->total_buffer.GetErrMsg());
            DBG_ELOG(err_msg_);
            //delete ctx_ptr->per_recv_io_ctx;
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
///////////////////////////////////////////////////////////////////////////////
Context* ASock::PopClientContextFromCache() 
{
    Context* ctx_ptr = nullptr;
    { //lock scope
        std::lock_guard<std::mutex> lock(cache_lock_);
        if (!queue_ctx_cache_.empty()) {
            ctx_ptr = queue_ctx_cache_.front();
            queue_ctx_cache_.pop(); 
            //LOG("POP : queue_ctx_cache :" <<" size = " << queue_ctx_cache_.size());
            ReSetCtxPtr(ctx_ptr);
            return ctx_ptr;
        }
    }
    //alloc new !!!
    std::lock_guard<std::mutex> lock(cache_lock_);
    if (!BuildClientContextCache()) {
        return nullptr;
    }
    Context* rtn_ctx_ptr = queue_ctx_cache_.front();
    queue_ctx_cache_.pop();
    ReSetCtxPtr(rtn_ctx_ptr);
    return rtn_ctx_ptr ;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::PushClientContextToCache(Context* ctx_ptr)
{
    std::lock_guard<std::mutex> lock(cache_lock_);
    //reset all
    DBG_LOG("sock=" << ctx_ptr->sock_id_copy);
    ReSetCtxPtr(ctx_ptr);
    queue_ctx_cache_.push(ctx_ptr);
    //LOG("push : " << ctx_ptr->sock_id_copy << " / queue ctx cache size = " << queue_ctx_cache_.size());
}
///////////////////////////////////////////////////////////////////////////////
void ASock::ReSetCtxPtr(Context* ctx_ptr)
{
    std::lock_guard<std::mutex> lock(ctx_ptr->ctx_lock);  //   !!! 
    DBG_LOG("sock=" << ctx_ptr->socket << ",sock=" << ctx_ptr->sock_id_copy);
    ctx_ptr->total_buffer.ReSet();
    ctx_ptr->socket = INVALID_SOCKET;
    ctx_ptr->sock_id_copy = -1;
    ctx_ptr->send_ref_cnt = 0;
    ctx_ptr->recv_ref_cnt = 0;
    ctx_ptr->complete_recv_len = 0;
    //SecureZeroMemory((PVOID)&ctx_ptr->per_recv_io_ctx->overlapped, sizeof(WSAOVERLAPPED));
    //ctx_ptr->per_recv_io_ctx->wsabuf.buf = NULL; //TODO
    //ctx_ptr->per_recv_io_ctx->wsabuf.len = 0;
    //ctx_ptr->per_recv_io_ctx->io_type = EnumIOType::IO_UNKNOWN;
    //ctx_ptr->per_recv_io_ctx->total_send_len = 0;
    //ctx_ptr->per_recv_io_ctx->sent_len = 0;
    ctx_ptr->is_packet_len_calculated = false;
    ctx_ptr->is_connected = false;
    while(!ctx_ptr->pending_send_deque.empty() ) {
        PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
        delete [] pending_sent.pending_sent_data;
        ctx_ptr->pending_send_deque.pop_front();
    }
    ctx_ptr->pending_send_deque.shrink_to_fit();
	//multiple wsarecv ----- START
    ctx_ptr->cur_recv_issued_id = 0;
    for (auto it = ctx_ptr->deq_recv_issued_id_n_state.begin();
         it != ctx_ptr->deq_recv_issued_id_n_state.end(); ++it) {
        if (it->recved_data != nullptr) {
            LOG("delete recved_data");
            delete[] it->recved_data;
        }
    }
    ctx_ptr->deq_recv_issued_id_n_state.clear();
    ctx_ptr->deq_recv_issued_id_n_state.shrink_to_fit();
	//multiple wsarecv ----- END
}
///////////////////////////////////////////////////////////////////////////////
void ASock::ClearClientCache()
{
    DBG_LOG("======= clear all cache ========");
    std::lock_guard<std::mutex> lock(cache_lock_);
    while (!queue_ctx_cache_.empty()) {
        Context* ctx_ptr = queue_ctx_cache_.front();
        while(!ctx_ptr->pending_send_deque.empty() ) {
            PENDING_SENT pending_sent= ctx_ptr->pending_send_deque.front();
            delete [] pending_sent.pending_sent_data;
            ctx_ptr->pending_send_deque.pop_front();
        }
        delete ctx_ptr;
        queue_ctx_cache_.pop();
    }//while
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RunClientThread()
{
    if (!is_client_thread_running_) {

        client_thread_ = std::thread (&ASock::ClientThreadRoutine, this);
        //client_thread.detach();
        is_client_thread_running_ = true;        
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
                NULL, WSAGetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
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
    int opt_cur = 0;
    int opt_val = int(max_data_len_);
    int opt_len = sizeof(opt_cur);
    if (getsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*) &opt_cur, (SOCKLEN_T*)&opt_len) == -1) {
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
    if (getsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*) & opt_cur, (SOCKLEN_T*)&opt_len) == -1) {
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
    return true;
}
///////////////////////////////////////////////////////////////////////////////
// CLIENT
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitUdpClient(const char* server_ip, 
                           unsigned short  server_port, 
                           size_t      max_data_len /*DEFAULT_PACKET_SIZE*/ )
{
    sock_usage_ = SockUsage::SOCK_USAGE_UDP_CLIENT  ;
    server_ip_   = server_ip ;
    server_port_ = server_port;
    is_connected_ = false;
    if(!InitWinsock()) {
        return false;
    }
    if(!SetBufferCapacity(max_data_len) ) {
        return false;
    }
    //if (context_.per_recv_io_ctx != NULL) {
    //    delete context_.per_recv_io_ctx;
    //}
    //context_.per_recv_io_ctx = new (std::nothrow) PER_IO_DATA;
    //if (context_.per_recv_io_ctx == nullptr) {
    //    DBG_ELOG("mem alloc failed");
    //    return false;
    //}
    context_.socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (context_.socket == INVALID_SOCKET) {
        ELOG("socket creation failed");
        return false;
    }
    memset((void *)&udp_server_addr_, 0x00, sizeof(udp_server_addr_));
    udp_server_addr_.sin_family = AF_INET;
    inet_pton(AF_INET, server_ip_.c_str(), &(udp_server_addr_.sin_addr));
    udp_server_addr_.sin_port = htons(server_port_);
    return ConnectToServer();
}

///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitTcpClient( const char* server_ip, unsigned short server_port,
                            int connect_timeout_secs, size_t  max_data_len)
{
    sock_usage_  = SockUsage::SOCK_USAGE_TCP_CLIENT;
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
    //if (context_.per_recv_io_ctx != NULL) {
    //    delete context_.per_recv_io_ctx;
    //}
    //context_.per_recv_io_ctx = new (std::nothrow) PER_IO_DATA;
    //if (context_.per_recv_io_ctx == nullptr) {
    //    DBG_ELOG("mem alloc failed");
    //    return false;
    //}
    context_.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (context_.socket == INVALID_SOCKET) {
        ELOG("socket creation failed");
        return false;
    }
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
        if (cumbuffer::OP_RSLT_OK != context_.total_buffer.Init(max_data_len_)) {
            std::lock_guard<std::mutex> lock(err_msg_lock_);
            err_msg_ = std::string("cumBuffer Init error :") + context_.total_buffer.GetErrMsg();
            closesocket(context_.socket);
            context_.socket = INVALID_SOCKET;
            DBG_ELOG(err_msg_);
            return false;
        }
        is_buffer_init_ = true;
    } else {
        //in case of reconnect
        context_.total_buffer.ReSet();
    }
    struct timeval timeout_val;
    timeout_val.tv_sec  = connect_timeout_secs_;
    timeout_val.tv_usec = 0;

    int result = -1;
    if ( sock_usage_ == SockUsage::SOCK_USAGE_IPC_CLIENT ) {
        //TODO
    } else if ( sock_usage_ == SockUsage::SOCK_USAGE_TCP_CLIENT ) {
        result = connect(context_.socket, (SOCKADDR*)&tcp_server_addr_, (SOCKLEN_T)sizeof(SOCKADDR_IN));
    } else if ( sock_usage_ == SockUsage::SOCK_USAGE_UDP_CLIENT ) {
        if(!SetSockoptSndRcvBufUdp(context_.socket)) {
            closesocket(context_.socket);
            return false;
        }
        result = connect(context_.socket,(SOCKADDR*)&udp_server_addr_,(SOCKLEN_T)sizeof(SOCKADDR_IN)) ;
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
    WSAPOLLFD fdArray = { 0 };
    fdArray.fd = context_.socket;
    fdArray.events = POLLWRNORM;
    result = WSAPoll(&fdArray, 1, wait_timeout_ms);
    DBG_LOG("connect WSAPoll returns....: result ="<<result);
    if (SOCKET_ERROR == result) {
        BuildErrMsgString(WSAGetLastError());
        ELOG( err_msg_ ) ;
        return false;
    }
    if (result) {
        if (fdArray.revents & POLLWRNORM) {
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
            if (fdArray.revents & POLLRDNORM) {  
                //============================
                size_t want_recv_len = max_data_len_;
                //-------------------------------------
                if (max_data_len_ > context_.total_buffer.GetLinearFreeSpace()) { 
                    want_recv_len = context_.total_buffer.GetLinearFreeSpace();
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
                    DBG_ELOG("INVALID_SOCKET");
                    is_client_thread_running_ = false;
                    return;
                }
                int ret = -1;
                if (sock_usage_ == SockUsage::SOCK_USAGE_UDP_CLIENT) {
                    SOCKLEN_T addrlen = sizeof(context_.udp_remote_addr);
                    ret = recvfrom(context_.socket, //--> is listen_socket_
                                              context_.total_buffer.GetLinearAppendPtr(),
                                              int(max_data_len_),
                                              0,
                                              (struct sockaddr*)&context_.udp_remote_addr,
                                              &addrlen);
                }
                else {
                    ret = recv( context_.socket, context_.total_buffer.GetLinearAppendPtr(), (int) want_recv_len, 0);
                }
                if (SOCKET_ERROR == ret) {
                    BuildErrMsgString(WSAGetLastError());
                    DBG_ELOG( err_msg_ ) ;
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
                        context_.socket = INVALID_SOCKET; 
                        is_client_thread_running_ = false;
                        return;
                    }
                    bool isOk = false;
                    if (sock_usage_ == SockUsage::SOCK_USAGE_UDP_CLIENT) {
                        isOk = RecvfromData(0, &context_, ret);
                    }
                    else {
                        //DBG_LOG("sock=" << context_.socket << ", bytes_transferred = " << ret);
                        isOk = RecvData(nullptr, &context_, ret);
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
            ELOG(err_msg_);
            return;
        }
        if (result == 0) {
            //timeout
        }
        else {
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

///////////////////////////////////////////////////////////////////////////////
bool ASock::SendPendingData()
{
    std::lock_guard<std::mutex> guard(context_.ctx_lock);
    while(!context_.pending_send_deque.empty()) {
        DBG_LOG("pending exists");
        PENDING_SENT pending_sent = context_.pending_send_deque.front();
        int sent_len = 0;
         if ( sock_usage_ == SockUsage::SOCK_USAGE_UDP_CLIENT ) {
            // UDP : all or nothing . no partial sent!
            sent_len = sendto(context_.socket,  pending_sent.pending_sent_data, (int)pending_sent.pending_sent_len,
                              0, (SOCKADDR*)&udp_server_addr_, int(sizeof(udp_server_addr_)));
        } else {
            sent_len = send(context_.socket, pending_sent.pending_sent_data, (int)pending_sent.pending_sent_len, 0) ;
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
                ELOG(err_msg_);
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

///////////////////////////////////////////////////////////////////////////////
//client use
bool ASock:: SendToServer(const char* data_ptr, size_t len)
{
    std::lock_guard<std::mutex> lock(context_.ctx_lock); 
    char* data_position_ptr = const_cast<char*>(data_ptr) ;   
    size_t total_sent = 0;           
    if ( !is_connected_ ) {
        std::lock_guard<std::mutex> lock(err_msg_lock_);
        err_msg_ = "not connected";
        DBG_ELOG( err_msg_ ) ;
        return false;
    }
    if (context_.socket == INVALID_SOCKET) {
        err_msg_ = "not connected";
        DBG_ELOG(err_msg_);
        return false;
    }

    //if sent is pending, just push to queue. 
    if(context_.is_sent_pending){
        DBG_LOG("pending. just queue.");
        PENDING_SENT pending_sent;
        pending_sent.pending_sent_data = new (std::nothrow)char [len]; 
        if (pending_sent.pending_sent_data == nullptr) {
            ELOG("memory alloc failed");
            exit(1);
        }
        pending_sent.pending_sent_len  = len;
        memcpy(pending_sent.pending_sent_data, data_ptr, len);
        context_.pending_send_deque.push_back(pending_sent);
        return true;
    }
    while( total_sent < len ) {
        int sent_len =0;
        if ( sock_usage_ == SockUsage::SOCK_USAGE_UDP_CLIENT ) {
            // UDP : all or nothing. no partial sent!
            sent_len = sendto(context_.socket, data_ptr, int(len), 0,
                                (SOCKADDR*)&udp_server_addr_, int(sizeof(udp_server_addr_)));
        } else {
            sent_len = send(context_.socket, data_position_ptr, (int)(len-total_sent), 0);
        }

        if(sent_len > 0) {
            total_sent += sent_len ;  
            data_position_ptr += sent_len ;      
        } else if( sent_len == SOCKET_ERROR ) {
            if (WSAEWOULDBLOCK == WSAGetLastError()) {
                //send later
                PENDING_SENT pending_sent;
                pending_sent.pending_sent_data = new char [len-total_sent]; 
                pending_sent.pending_sent_len  = len-total_sent;
                memcpy(pending_sent.pending_sent_data, data_position_ptr, len-total_sent);
                context_.pending_send_deque.push_back(pending_sent);
                context_.is_sent_pending = true;
                return true;
            } else {
                BuildErrMsgString(WSAGetLastError());
                ELOG("socket ="<< context_.socket << " : " << err_msg_);
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

