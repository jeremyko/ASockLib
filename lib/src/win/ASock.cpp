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
//TODO
#include "ASock.hpp"
#include <chrono>

using namespace asock;

///////////////////////////////////////////////////////////////////////////////
ASock::~ASock()
{
    if (complete_packet_data_ != NULL)
    {
        delete[] complete_packet_data_;
        complete_packet_data_ = NULL;
    }

    if (sock_usage_ == SOCK_USAGE_TCP_CLIENT ||
        sock_usage_ == SOCK_USAGE_UDP_CLIENT ||
        sock_usage_ == SOCK_USAGE_IPC_CLIENT)
    {
#ifdef __APPLE__
        if (kq_events_ptr_)
        {
            delete kq_events_ptr_;
        }
#elif __linux__
        if (ep_events_)
        {
            delete ep_events_;
        }
#endif
        Disconnect();
    }
    else if (sock_usage_ == SOCK_USAGE_TCP_SERVER ||
        sock_usage_ == SOCK_USAGE_UDP_SERVER ||
        sock_usage_ == SOCK_USAGE_IPC_SERVER)
    {
#ifdef __APPLE__
        if (kq_events_ptr_)
        {
            delete[] kq_events_ptr_;
        }
#elif __linux__
        if (ep_events_)
        {
            delete[] ep_events_;
        }
#endif

        CLIENT_UNORDERMAP_ITER_T it_del = client_map_.begin();
        while (it_del != client_map_.end())
        {
            delete it_del->second;
            it_del = client_map_.erase(it_del);
        }

        ClearClientCache();

#ifdef __APPLE__
        ControlKq(listen_context_ptr_, EVFILT_READ, EV_DELETE);
#elif __linux__
        ControlEpoll(listen_context_ptr_, EPOLLIN | EPOLLERR | EPOLLRDHUP,
            EPOLL_CTL_DEL); //just in case
#endif

#if defined __APPLE__ || defined __linux__ 
        delete listen_context_ptr_;
        if (sock_usage_ == SOCK_USAGE_IPC_SERVER)
        {
            unlink(server_ipc_socket_path_.c_str());
        }
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SetCbOnCalculatePacketLen(std::function<size_t(Context*)> cb)
{
    //for composition usage 
    if (cb != nullptr)
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
bool ASock::SetCbOnRecvedCompletePacket(std::function<bool(Context*, char*, int)> cb)
{
    //for composition usage 
    if (cb != nullptr)
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
bool  ASock::SetCbOnDisconnectedFromServer(std::function<void()> cb)
{
    //for composition usage 
    if (cb != nullptr)
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
bool ASock::SetCbOnClientConnected(std::function<void(Context*)> cb)
{
    //for composition usage 
    if (cb != nullptr)
    {
        cb_on_client_connected_ = cb;
    }
    else
    {
        err_msg_ = "callback is null";
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SetCbOnClientDisconnected(std::function<void(Context*)> cb)
{
    //for composition usage 
    if (cb != nullptr)
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
bool ASock::SetBufferCapacity(int max_data_len)
{
    if (max_data_len <= 0)
    {
        err_msg_ = " length is invalid";
        return false;
    }

    max_data_len_ = max_data_len;

    complete_packet_data_ = new (std::nothrow) char[max_data_len_];
    if (complete_packet_data_ == NULL)
    {
        err_msg_ = "memory alloc failed!";
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool   ASock::SetSocketNonBlocking(int sock_fd)
{
    int oldflags;

    if ((oldflags = fcntl(sock_fd, F_GETFL, 0)) < 0)
    {
        err_msg_ = "fcntl F_GETFL error [" + std::string(strerror(errno)) + "]";
        return  false;
    }

    int ret = fcntl(sock_fd, F_SETFL, oldflags | O_NONBLOCK);
    if (ret < 0)
    {
        err_msg_ = "fcntl O_NONBLOCK error [" + std::string(strerror(errno)) + "]";
        return  false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SetSockoptSndRcvBufUdp(int socket)
{
    size_t opt_cur;
    int opt_val = max_data_len_;
    int opt_len = sizeof(opt_cur);
    if (getsockopt(socket, SOL_SOCKET, SO_SNDBUF, &opt_cur, (SOCKLEN_T *)&opt_len) == -1)
    {
        err_msg_ = "gsetsockopt SO_SNDBUF error [" + std::string(strerror(errno)) + "]";
        return false;
    }

    //std::cout << "curr SO_SNDBUF = " << opt_cur << "\n";
    if (max_data_len_ > opt_cur)
    {
        if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&opt_val, sizeof(opt_val)) == -1)
        {
            err_msg_ = "setsockopt SO_SNDBUF error [" + std::string(strerror(errno)) + "]";
            return false;
        }
        std::cout << "set SO_SNDBUF = " << opt_val << "\n";
    }

    //--------------
    if (getsockopt(socket, SOL_SOCKET, SO_RCVBUF, &opt_cur, (SOCKLEN_T *)&opt_len) == -1)
    {
        err_msg_ = "setsockopt SO_RCVBUF error [" + std::string(strerror(errno)) + "]";
        return false;
    }
    //std::cout << "curr SO_RCVBUF = " << opt_cur << "\n";

    if (max_data_len_ > opt_cur)
    {
        if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&opt_val, sizeof(opt_val)) == -1)
        {
            err_msg_ = "setsockopt SO_RCVBUF error [" + std::string(strerror(errno)) + "]";
            return false;
        }
        std::cout << "set SO_RCVBUF = " << opt_val << "\n";
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RecvfromData(Context* context_ptr) //XXX context 가 지금 서버 context 임!!
{
    if (max_data_len_ > context_ptr->recv_buffer.GetLinearFreeSpace())
    {
        err_msg_ = "no linear free space left " + std::to_string(context_ptr->recv_buffer.GetLinearFreeSpace());
        return false;
    }

    SOCKLEN_T addrlen = sizeof(context_ptr->udp_remote_addr);
    int recved_len = recvfrom(context_ptr->socket, //--> is listen_socket_
        context_ptr->recv_buffer.GetLinearAppendPtr(),
        max_data_len_,
        0,
        (struct sockaddr *)&context_ptr->udp_remote_addr,
        &addrlen);
    if (recved_len > 0)
    {
        context_ptr->recv_buffer.IncreaseData(recved_len);

        //udp got complete packet 
        if (cumbuffer_defines::OP_RSLT_OK !=
            context_ptr->recv_buffer.GetData(recved_len, complete_packet_data_))
        {
            //error !
            err_msg_ = context_ptr->recv_buffer.GetErrMsg();
            std::cerr << err_msg_ << "\n";
            context_ptr->is_packet_len_calculated = false;
            return false;
        }
        //XXX UDP 이므로 받는 버퍼를 초기화해서, linear free space를 초기화 상태로!! XXX 
        context_ptr->recv_buffer.ReSet(); //this is udp. all data has arrived!

        if (cb_on_recved_complete_packet_ != nullptr)
        {
            //invoke user specific callback
            cb_on_recved_complete_packet_(context_ptr, complete_packet_data_, recved_len);
        }
        else
        {
            //invoke user specific implementation
            OnRecvedCompleteData(context_ptr, complete_packet_data_, recved_len);
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RecvData(Context* context_ptr)
{
    int want_recv_len = max_data_len_;
    if (max_data_len_ > context_ptr->recv_buffer.GetLinearFreeSpace())
    {
        want_recv_len = context_ptr->recv_buffer.GetLinearFreeSpace();
    }

    if (want_recv_len == 0)
    {
        err_msg_ = "no linear free space left ";
        return false;
    }

    int recved_len = recv(context_ptr->socket,
        context_ptr->recv_buffer.GetLinearAppendPtr(),
        want_recv_len, 0);

    if (recved_len > 0)
    {
        context_ptr->recv_buffer.IncreaseData(recved_len);

        while (context_ptr->recv_buffer.GetCumulatedLen())
        {
            //invoke user specific implementation
            if (!context_ptr->is_packet_len_calculated)
            {
                //only when calculation is necessary
                if (cb_on_calculate_data_len_ != nullptr)
                {
                    //invoke user specific callback
                    context_ptr->complete_packet_len_ = cb_on_calculate_data_len_(context_ptr);
                }
                else
                {
                    //invoke user specific implementation
                    context_ptr->complete_packet_len_ = OnCalculateDataLen(context_ptr);
                }
                context_ptr->is_packet_len_calculated = true;
            }

            if (context_ptr->complete_packet_len_ == asock::MORE_TO_COME)
            {
                context_ptr->is_packet_len_calculated = false;
                return true; //need to recv more
            }
            else if (context_ptr->complete_packet_len_ > context_ptr->recv_buffer.GetCumulatedLen())
            {
                return true; //need to recv more
            }
            else
            {
                //got complete packet 
                if (cumbuffer_defines::OP_RSLT_OK !=
                    context_ptr->recv_buffer.GetData(context_ptr->complete_packet_len_,
                        complete_packet_data_))
                {
                    //error !
                    err_msg_ = context_ptr->recv_buffer.GetErrMsg();
                    context_ptr->is_packet_len_calculated = false;
                    return false;
                }

                if (cb_on_recved_complete_packet_ != nullptr)
                {
                    //invoke user specific callback
                    cb_on_recved_complete_packet_(context_ptr,
                        complete_packet_data_,
                        context_ptr->complete_packet_len_);
                }
                else
                {
                    //invoke user specific implementation
                    OnRecvedCompleteData(context_ptr,
                        complete_packet_data_,
                        context_ptr->complete_packet_len_);
                }

                context_ptr->is_packet_len_calculated = false;
            }
        } //while
    }
    else if (recved_len == 0)
    {
        err_msg_ = "recv 0, client disconnected , fd:" + std::to_string(context_ptr->socket);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SendData(Context* context_ptr, const char* data_ptr, int len)
{
    std::lock_guard<std::mutex> lock(context_ptr->send_lock);
    char* data_position_ptr = const_cast<char*>(data_ptr);
    int total_sent = 0;

    //if sent is pending, just push to queue. 
    if (context_ptr->is_sent_pending) //tcp, domain socket only  
    {
        PENDING_SENT pending_sent;
        pending_sent.pending_sent_data = new char[len];
        pending_sent.pending_sent_len = len;
        memcpy(pending_sent.pending_sent_data, data_ptr, len);
        if (sock_usage_ == SOCK_USAGE_UDP_SERVER)
        {
            //udp(server) 인 경우엔, 데이터와 client remote addr 정보를 함께 queue에 저장
            memcpy(&pending_sent.udp_remote_addr,
                &context_ptr->udp_remote_addr,
                sizeof(pending_sent.udp_remote_addr));
        }
        context_ptr->pending_send_deque_.push_back(pending_sent);

#ifdef __APPLE__
        if (!ControlKq(context_ptr, EVFILT_WRITE, EV_ADD | EV_ENABLE))
#elif __linux__
        if (!ControlEpoll(context_ptr, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_MOD))
#endif
        {
            delete[] pending_sent.pending_sent_data;
            context_ptr->pending_send_deque_.pop_back();
            return false;
        }
        return true;
    }

    while (total_sent < len)
    {
        int sent_len = 0;
        if (sock_usage_ == SOCK_USAGE_UDP_SERVER)
        {
            //XXX UDP 인 경우엔 all or nothing 으로 동작할것임.. no partial sent!
            sent_len = sendto(context_ptr->socket,
                data_position_ptr,
                len - total_sent,
                0,
                (struct sockaddr*)& context_ptr->udp_remote_addr,
                sizeof(context_ptr->udp_remote_addr));
        }
        else if (sock_usage_ == SOCK_USAGE_UDP_CLIENT)
        {
            //XXX UDP 인 경우엔 all or nothing 으로 동작할것임.. no partial sent!
            sent_len = sendto(context_ptr->socket,
                data_position_ptr,
                len - total_sent,
                0,
                0, //XXX client : already set! (via connect)  
                sizeof(context_ptr->udp_remote_addr));
        }
        else
        {
            sent_len = send(context_ptr->socket, data_position_ptr, len - total_sent, 0);
        }

        if (sent_len > 0)
        {
            total_sent += sent_len;
            data_position_ptr += sent_len;
        }
        else if (sent_len < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                //send later
                PENDING_SENT pending_sent;
                pending_sent.pending_sent_data = new char[len - total_sent];
                pending_sent.pending_sent_len = len - total_sent;
                memcpy(pending_sent.pending_sent_data, data_position_ptr, len - total_sent);
                if (sock_usage_ == SOCK_USAGE_UDP_SERVER)
                {
                    //udp(server) 인 경우엔, 데이터와 client remote addr 정보를 함께 queue에 저장
                    memcpy(&pending_sent.udp_remote_addr,
                        &context_ptr->udp_remote_addr,
                        sizeof(pending_sent.udp_remote_addr));
                }

                context_ptr->pending_send_deque_.push_back(pending_sent);
#ifdef __APPLE__
                if (!ControlKq(context_ptr, EVFILT_WRITE, EV_ADD | EV_ENABLE))
#elif __linux__
                if (!ControlEpoll(context_ptr, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_MOD))
#endif
                {
                    delete[] pending_sent.pending_sent_data;
                    context_ptr->pending_send_deque_.pop_back();
                    return false;
                }
                context_ptr->is_sent_pending = true;
                return true;
            }
            else if (errno != EINTR)
            {
                err_msg_ = "send error [" + std::string(strerror(errno)) + "]";
                return false;
            }
        }
    }//while

    return true;
}

#ifdef __APPLE__
///////////////////////////////////////////////////////////////////////////////
bool ASock::ControlKq(Context* context_ptr, uint32_t events, uint32_t fflags)
{
    struct  kevent kq_event;
    memset(&kq_event, 0, sizeof(struct kevent));
    EV_SET(&kq_event, context_ptr->socket, events, fflags, 0, 0, context_ptr);
    //udata = context_ptr

    int result = kevent(kq_fd_, &kq_event, 1, NULL, 0, NULL);
    if (result == -1)
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
bool ASock::ControlEpoll(Context* context_ptr, uint32_t events, int op)
{
    struct  epoll_event ev_client {};
    ev_client.data.fd = context_ptr->socket;
    ev_client.events = events;
    ev_client.data.ptr = context_ptr;

    if (epoll_ctl(ep_fd_, op, context_ptr->socket, &ev_client) < 0)
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
bool ASock::InitTcpServer(const char* bind_ip,
    int         bind_port,
    int         max_data_len /*=DEFAULT_PACKET_SIZE*/,
    int         max_client /*=DEFAULT_MAX_CLIENT*/)
{
    sock_usage_ = SOCK_USAGE_TCP_SERVER;

    server_ip_ = bind_ip;
    server_port_ = bind_port;
    max_client_limit_ = max_client;
    if (max_client_limit_ < 0)
    {
        return false;
    }
    if (!SetBufferCapacity(max_data_len))
    {
        return false;
    }

    return RunServer();
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::InitUdpServer(const char* bind_ip,
    int         bind_port,
    int         max_data_len /*=DEFAULT_PACKET_SIZE*/,
    int         max_client /*=DEFAULT_MAX_CLIENT*/)
{
    sock_usage_ = SOCK_USAGE_UDP_SERVER;

    server_ip_ = bind_ip;
    server_port_ = bind_port;
    max_client_limit_ = max_client;
    if (max_client_limit_ < 0)
    {
        return false;
    }
    if (!SetBufferCapacity(max_data_len))
    {
        return false;
    }

    return RunServer();
}

///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitIpcServer(const char* sock_path,
    int         max_data_len /*=DEFAULT_PACKET_SIZE*/,
    int         max_client /*=DEFAULT_MAX_CLIENT*/)
{
    sock_usage_ = SOCK_USAGE_IPC_SERVER;
    server_ipc_socket_path_ = sock_path;

    max_client_limit_ = max_client;
    if (max_client_limit_ < 0)
    {
        return false;
    }
    if (!SetBufferCapacity(max_data_len))
    {
        return false;
    }

    return RunServer();
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RunServer()
{
#ifdef __APPLE__
    if (kq_events_ptr_)
#elif __linux__
    if (ep_events_)
#endif
    {
        err_msg_ = "error [server is already running]";
        return false;
    }

    if (sock_usage_ == SOCK_USAGE_IPC_SERVER)
    {
        listen_socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    }
    else if (sock_usage_ == SOCK_USAGE_TCP_SERVER)
    {
        listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    }
    else if (sock_usage_ == SOCK_USAGE_UDP_SERVER)
    {
        listen_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    }

    if (listen_socket_ < 0)
    {
        err_msg_ = "init error [" + std::string(strerror(errno)) + "]";
        return false;
    }

    if (!SetSocketNonBlocking(listen_socket_))
    {
        return  false;
    }

    int opt_on = 1;
    int result = -1;

    if (setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &opt_on, sizeof(opt_on)) == -1)
    {
        err_msg_ = "setsockopt SO_REUSEADDR error [" + std::string(strerror(errno)) + "]";
        return false;
    }

    if (setsockopt(listen_socket_, SOL_SOCKET, SO_KEEPALIVE, &opt_on, sizeof(opt_on)) == -1)
    {
        err_msg_ = "setsockopt SO_KEEPALIVE error [" + std::string(strerror(errno)) + "]";
        return false;
    }

    if (sock_usage_ == SOCK_USAGE_UDP_SERVER)
    {
        if (!SetSockoptSndRcvBufUdp(listen_socket_))
        {
            return false;
        }
    }

    //-------------------------------------------------
    if (sock_usage_ == SOCK_USAGE_IPC_SERVER)
    {
        SOCKADDR_UN ipc_server_addr;
        memset((void *)&ipc_server_addr, 0x00, sizeof(ipc_server_addr));
        ipc_server_addr.sun_family = AF_UNIX;
        snprintf(ipc_server_addr.sun_path, sizeof(ipc_server_addr.sun_path),
            "%s", server_ipc_socket_path_.c_str());

        result = bind(listen_socket_, (SOCKADDR*)&ipc_server_addr, sizeof(ipc_server_addr));
    }
    else if (sock_usage_ == SOCK_USAGE_TCP_SERVER ||
        sock_usage_ == SOCK_USAGE_UDP_SERVER)
    {
        SOCKADDR_IN    server_addr;
        memset((void *)&server_addr, 0x00, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(server_ip_.c_str());
        server_addr.sin_port = htons(server_port_);

        result = bind(listen_socket_, (SOCKADDR*)&server_addr, sizeof(server_addr));
    }

    //-------------------------------------------------

    if (result < 0)
    {
        err_msg_ = "bind error [" + std::string(strerror(errno)) + "]";
        return false;
    }

    if (sock_usage_ == SOCK_USAGE_IPC_SERVER ||
        sock_usage_ == SOCK_USAGE_TCP_SERVER)
    {
        result = listen(listen_socket_, SOMAXCONN);
        if (result < 0)
        {
            err_msg_ = "listen error [" + std::string(strerror(errno)) + "]";
            return false;
        }
    }

    struct sigaction act;
    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGPIPE, &act, NULL);

#if defined __APPLE__ || defined __linux__ 
    listen_context_ptr_ = new (std::nothrow) Context();
    if (!listen_context_ptr_)
    {
        err_msg_ = "Context alloc failed !";
        return false;
    }

    listen_context_ptr_->socket = listen_socket_;
#endif

#ifdef __APPLE__
    kq_fd_ = kqueue();
    if (kq_fd_ == -1)
    {
        err_msg_ = "kqueue error [" + std::string(strerror(errno)) + "]";
        return false;
    }
    if (!ControlKq(listen_context_ptr_, EVFILT_READ, EV_ADD))
    {
        return false;
    }
#elif __linux__
    ep_fd_ = epoll_create1(0);
    if (ep_fd_ == -1)
    {
        err_msg_ = "epoll create error [" + std::string(strerror(errno)) + "]";
        return false;
    }

    if (!ControlEpoll(listen_context_ptr_, EPOLLIN | EPOLLERR, EPOLL_CTL_ADD))
    {
        return false;
    }
#endif

    //start server thread
    is_need_server_run_ = true;
    is_server_running_ = true;

#ifdef __APPLE__
    kq_events_ptr_ = new struct kevent[max_client_limit_];
    memset(kq_events_ptr_, 0x00, sizeof(struct kevent) * max_client_limit_);
#elif __linux__
    ep_events_ = new struct epoll_event[max_client_limit_];
    memset(ep_events_, 0x00, sizeof(struct epoll_event) * max_client_limit_);
#endif

    if (sock_usage_ == SOCK_USAGE_UDP_SERVER)
    {
        //UDP is special~~~ XXX 
        std::thread server_thread(&ASock::ServerThreadUdpRoutine, this);
        server_thread.detach();
    }
    else
    {
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
    Context* context_ptr = nullptr;
    if (!queue_client_cache_.empty())
    {
        context_ptr = queue_client_cache_.front();
        queue_client_cache_.pop();

        return context_ptr;
    }

    context_ptr = new (std::nothrow) Context();
    if (!context_ptr)
    {
        err_msg_ = "Context alloc failed !";
        return nullptr;
    }
    return context_ptr;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::PushClientContextToCache(Context* context_ptr)
{
    CLIENT_UNORDERMAP_ITER_T it_found;
    it_found = client_map_.find(context_ptr->socket);
    if (it_found != client_map_.end())
    {
        client_map_.erase(it_found);
    }

    //reset
    context_ptr->recv_buffer.ReSet();
    context_ptr->socket = -1;
    context_ptr->is_packet_len_calculated = false;
    context_ptr->is_sent_pending = false;
    context_ptr->complete_packet_len_ = 0;

    while (!context_ptr->pending_send_deque_.empty())
    {
        PENDING_SENT pending_sent = context_ptr->pending_send_deque_.front();
        delete[] pending_sent.pending_sent_data;
        context_ptr->pending_send_deque_.pop_front();
    }

    queue_client_cache_.push(context_ptr);
}


///////////////////////////////////////////////////////////////////////////////
void ASock::ClearClientCache()
{
    while (!queue_client_cache_.empty())
    {
        Context* context_ptr = queue_client_cache_.front();
        while (!context_ptr->pending_send_deque_.empty())
        {
            PENDING_SENT pending_sent = context_ptr->pending_send_deque_.front();
            delete[] pending_sent.pending_sent_data;
            context_ptr->pending_send_deque_.pop_front();
        }
        delete context_ptr;
        queue_client_cache_.pop();
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::AcceptNewClient()
{
    while (true)
    {
        int client_fd = -1;

        if (sock_usage_ == SOCK_USAGE_IPC_SERVER)
        {
            SOCKADDR_UN client_addr;
            SOCKLEN_T client_addr_size = sizeof(client_addr);
            client_fd = accept(listen_socket_, (SOCKADDR*)&client_addr, &client_addr_size);
        }
        else if (sock_usage_ == SOCK_USAGE_TCP_SERVER)
        {
            SOCKADDR_IN client_addr;
            SOCKLEN_T client_addr_size = sizeof(client_addr);
            client_fd = accept(listen_socket_, (SOCKADDR*)&client_addr, &client_addr_size);
        }

        if (client_fd == -1)
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
                return false;
            }
        }

        ++client_cnt_;
        SetSocketNonBlocking(client_fd);

        Context* client_context_ptr = PopClientContextFromCache();
        if (client_context_ptr == nullptr)
        {
            is_server_running_ = false;
            return false;
        }

        if (cumbuffer_defines::OP_RSLT_OK !=
            client_context_ptr->recv_buffer.Init(max_data_len_))
        {
            err_msg_ = "cumBuffer Init error : " +
                client_context_ptr->recv_buffer.GetErrMsg();
            is_server_running_ = false;
            return false;
        }

        client_context_ptr->socket = client_fd;

        std::pair<CLIENT_UNORDERMAP_ITER_T, bool> client_map_rslt;
        client_map_rslt = client_map_.insert(std::pair<int, Context*>(client_fd, client_context_ptr));
        if (!client_map_rslt.second)
        {
            err_msg_ = "client_map_ insert error [" +
                std::to_string(client_fd) + " already exist]";
            return false;
        }

        if (cb_on_client_connected_ != nullptr)
        {
            cb_on_client_connected_(client_context_ptr);
        }
        else
        {
            OnClientConnected(client_context_ptr);
        }
#ifdef __APPLE__
        if (!ControlKq(client_context_ptr, EVFILT_READ, EV_ADD))
#elif __linux__
        if (!ControlEpoll(client_context_ptr, EPOLLIN | EPOLLRDHUP, EPOLL_CTL_ADD))
#endif
        {
            is_server_running_ = false;
            return false;
        }
    }//while : accept
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ServerThreadUdpRoutine()
{
#ifdef __APPLE__
    struct timespec ts;
    ts.tv_sec = 1;
    ts.tv_nsec = 0;
#endif

    if (cumbuffer_defines::OP_RSLT_OK != listen_context_ptr_->recv_buffer.Init(max_data_len_))
    {
        err_msg_ = "cumBuffer Init error : " +
            listen_context_ptr_->recv_buffer.GetErrMsg();
        is_server_running_ = false;
        return;
    }
    //std::cout << "DEBUG : GetTotalFreeSpace: " << "" <<listen_context_ptr_->recv_buffer.GetTotalFreeSpace() <<"\n";

    while (is_need_server_run_)
    {
#ifdef __APPLE__
        int event_cnt = kevent(kq_fd_, NULL, 0, kq_events_ptr_, max_client_limit_, &ts);
        if (event_cnt < 0)
        {
            err_msg_ = "kevent error [" + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#elif __linux__
        int event_cnt = epoll_wait(ep_fd_, ep_events_, max_client_limit_, 1000);
        if (event_cnt < 0)
        {
            err_msg_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#endif

        for (int i = 0; i < event_cnt; i++)
        {
#ifdef __APPLE__
            if (EVFILT_READ == kq_events_ptr_[i].filter)
#elif __linux__
            if (ep_events_[i].events & EPOLLIN)
#endif
            {
                //# recv #----------
                if (!RecvfromData(listen_context_ptr_))
                {
                    break;
                }
            }
#ifdef __APPLE__
            else if (EVFILT_WRITE == kq_events_ptr_[i].filter)
#elif __linux__
            else if (ep_events_[i].events & EPOLLOUT)
#endif
            {
                //# send #----------
                if (!SendPendingData(listen_context_ptr_))
                {
                    return; //error!
                }
            }
        }
    }//while

    is_server_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ServerThreadRoutine()
{
#ifdef __APPLE__
    struct timespec ts;
    ts.tv_sec = 1;
    ts.tv_nsec = 0;
#endif

    while (is_need_server_run_)
    {
#ifdef __APPLE__
        int event_cnt = kevent(kq_fd_, NULL, 0, kq_events_ptr_, max_client_limit_, &ts);
        if (event_cnt < 0)
        {
            err_msg_ = "kevent error [" + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#elif __linux__
        int event_cnt = epoll_wait(ep_fd_, ep_events_, max_client_limit_, 1000);
        if (event_cnt < 0)
        {
            err_msg_ = "epoll wait error [" + std::string(strerror(errno)) + "]";
            is_server_running_ = false;
            return;
        }
#endif

        for (int i = 0; i < event_cnt; i++)
        {
#ifdef __APPLE__
            if (kq_events_ptr_[i].ident == listen_socket_)
#elif __linux__
            if (((Context*)ep_events_[i].data.ptr)->socket == listen_socket_)
#endif
            {
                //# accept #----------
                if (!AcceptNewClient())
                {
                    std::cerr << "accept error:" << err_msg_ << "\n";
                    return;
                }
            }
            else
            {
#ifdef __APPLE__
                Context* context_ptr = (Context*)kq_events_ptr_[i].udata;
#elif __linux__
                Context* context_ptr = (Context*)ep_events_[i].data.ptr;
#endif

#ifdef __APPLE__
                if (kq_events_ptr_[i].flags & EV_EOF)
#elif __linux__
                if (ep_events_[i].events & EPOLLRDHUP || ep_events_[i].events & EPOLLERR)
#endif
                {
                    //# close #----------
                    TerminateClient(context_ptr);
                }
#ifdef __APPLE__
                else if (EVFILT_READ == kq_events_ptr_[i].filter)
#elif __linux__
                else if (ep_events_[i].events & EPOLLIN)
#endif
                {
                    //# recv #----------
                    if (!RecvData(context_ptr))
                    {
                        TerminateClient(context_ptr);
                    }
                }
#ifdef __APPLE__
                else if (EVFILT_WRITE == kq_events_ptr_[i].filter)
#elif __linux__
                else if (ep_events_[i].events & EPOLLOUT)
#endif
                {
                    //# send #----------
                    if (!SendPendingData(context_ptr))
                    {
                        return; //error!
                    }
                }
            }
        }
    } //while
    is_server_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SendPendingData(Context* context_ptr)
{
    //std::cout <<"["<< __func__ <<"-"<<__LINE__  <<"\n"; //debug 

    std::lock_guard<std::mutex> guard(context_ptr->send_lock);
    while (!context_ptr->pending_send_deque_.empty())
    {
        PENDING_SENT pending_sent = context_ptr->pending_send_deque_.front();

        int sent_len = 0;
        if (sock_usage_ == SOCK_USAGE_UDP_SERVER)
        {
            //XXX UDP 인 경우엔 all or nothing 으로 동작할것임.. no partial sent!
            sent_len = sendto(context_ptr->socket,
                pending_sent.pending_sent_data,
                pending_sent.pending_sent_len,
                0,
                (struct sockaddr*)& pending_sent.udp_remote_addr,
                sizeof(pending_sent.udp_remote_addr));
        }
        else if (sock_usage_ == SOCK_USAGE_UDP_CLIENT)
        {
            //XXX UDP 인 경우엔 all or nothing 으로 동작할것임.. no partial sent!
            sent_len = sendto(context_ptr->socket,
                pending_sent.pending_sent_data,
                pending_sent.pending_sent_len,
                0,
                0, //XXX client : already set! (via connect)  
                sizeof(pending_sent.udp_remote_addr));
        }
        else
        {
            sent_len = send(context_ptr->socket,
                pending_sent.pending_sent_data,
                pending_sent.pending_sent_len,
                0);
        }

        if (sent_len > 0)
        {
            if (sent_len == pending_sent.pending_sent_len)
            {
                delete[] pending_sent.pending_sent_data;
                context_ptr->pending_send_deque_.pop_front();

                if (context_ptr->pending_send_deque_.empty())
                {
                    //sent all data
                    context_ptr->is_sent_pending = false;
#ifdef __APPLE__
                    if (!ControlKq(context_ptr, EVFILT_WRITE, EV_DELETE) ||
                        !ControlKq(context_ptr, EVFILT_READ, EV_ADD))
#elif __linux__
                    if (!ControlEpoll(context_ptr, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_MOD))
#endif
                    {
                        //error!!!
                        if (sock_usage_ == SOCK_USAGE_TCP_CLIENT ||
                            sock_usage_ == SOCK_USAGE_IPC_CLIENT)
                        {
                            close(context_ptr->socket);
                            InvokeServerDisconnectedHandler();
                            is_client_thread_running_ = false;
                        }
                        else if (sock_usage_ == SOCK_USAGE_TCP_SERVER ||
                            sock_usage_ == SOCK_USAGE_IPC_SERVER)
                        {
                            is_server_running_ = false;
                        }
                        return false;
                    }
                    break;
                }
            }
            else
            {
                //TCP : partial sent ---> 남은 부분을 다시 제일 처음으로
                PENDING_SENT partial_pending_sent;
                int alloc_len = pending_sent.pending_sent_len - sent_len;
                partial_pending_sent.pending_sent_data = new char[alloc_len];
                partial_pending_sent.pending_sent_len = alloc_len;
                memcpy(partial_pending_sent.pending_sent_data,
                    pending_sent.pending_sent_data + sent_len,
                    alloc_len);

                //remove first.
                delete[] pending_sent.pending_sent_data;
                context_ptr->pending_send_deque_.pop_front();

                //push_front
                context_ptr->pending_send_deque_.push_front(partial_pending_sent);

                break; //next time
            }
        }
        else if (sent_len < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                break; //next time
            }
            else if (errno != EINTR)
            {
                err_msg_ = "send error [" + std::string(strerror(errno)) + "]";
                if (sock_usage_ == SOCK_USAGE_TCP_CLIENT ||
                    sock_usage_ == SOCK_USAGE_IPC_CLIENT)
                {
                    //client error!!!
                    close(context_ptr->socket);
                    InvokeServerDisconnectedHandler();
                    is_client_thread_running_ = false;
                    return false;
                }
                else if (sock_usage_ == SOCK_USAGE_TCP_SERVER ||
                    sock_usage_ == SOCK_USAGE_IPC_SERVER)
                {
                    TerminateClient(context_ptr);
                }
                break;
            }
        }
    } //while

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void  ASock::TerminateClient(Context* context_ptr)
{
    client_cnt_--;
#ifdef __APPLE__
    ControlKq(context_ptr, EVFILT_READ, EV_DELETE);
#elif __linux__
    ControlEpoll(context_ptr, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_DEL); //just in case
#endif

    close(context_ptr->socket);
    if (cb_on_client_connected_ != nullptr)
    {
        cb_on_client_disconnected_(context_ptr);
    }
    else
    {
        OnClientDisconnected(context_ptr);
    }

    PushClientContextToCache(context_ptr);
}


///////////////////////////////////////////////////////////////////////////////
// CLIENT
///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitTcpClient(const char* server_ip,
    int         server_port,
    int         connect_timeout_secs,
    int         max_data_len /*DEFAULT_PACKET_SIZE*/)
{
    sock_usage_ = SOCK_USAGE_TCP_CLIENT;
    connect_timeout_secs_ = connect_timeout_secs;

    if (!SetBufferCapacity(max_data_len))
    {
        return false;
    }

    context_.socket = socket(AF_INET, SOCK_STREAM, 0);
    memset((void *)&tcp_server_addr_, 0x00, sizeof(tcp_server_addr_));
    tcp_server_addr_.sin_family = AF_INET;
    tcp_server_addr_.sin_addr.s_addr = inet_addr(server_ip);
    tcp_server_addr_.sin_port = htons(server_port);

    return ConnectToServer();
}

///////////////////////////////////////////////////////////////////////////////
bool  ASock::InitUdpClient(const char* server_ip,
    int         server_port,
    int         max_data_len /*DEFAULT_PACKET_SIZE*/)
{
    sock_usage_ = SOCK_USAGE_UDP_CLIENT;

    if (!SetBufferCapacity(max_data_len))
    {
        return false;
    }

    context_.socket = socket(AF_INET, SOCK_DGRAM, 0);
    memset((void *)&udp_server_addr_, 0x00, sizeof(udp_server_addr_));
    udp_server_addr_.sin_family = AF_INET;
    udp_server_addr_.sin_addr.s_addr = inet_addr(server_ip);
    udp_server_addr_.sin_port = htons(server_port);

    return ConnectToServer();
}
///////////////////////////////////////////////////////////////////////////////
bool ASock::init_ipc_client(const char* sock_path,
    int         connect_timeout_secs,
    int         max_data_len)
{
    sock_usage_ = SOCK_USAGE_IPC_CLIENT;
    connect_timeout_secs_ = connect_timeout_secs;

    if (!SetBufferCapacity(max_data_len))
    {
        return false;
    }
    server_ipc_socket_path_ = sock_path;

    context_.socket = socket(AF_UNIX, SOCK_STREAM, 0);
    memset((void *)&ipc_conn_addr_, 0x00, sizeof(ipc_conn_addr_));
    ipc_conn_addr_.sun_family = AF_UNIX;
    snprintf(ipc_conn_addr_.sun_path, sizeof(ipc_conn_addr_.sun_path), "%s", sock_path);

    return ConnectToServer();
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::ConnectToServer()
{
    if (context_.socket < 0)
    {
        err_msg_ = "error : server socket is invalid";
        return false;
    }

    if (!SetSocketNonBlocking(context_.socket))
    {
        close(context_.socket);
        return  false;
    }

    if (!is_buffer_init_)
    {
        if (cumbuffer_defines::OP_RSLT_OK != context_.recv_buffer.Init(max_data_len_))
        {
            err_msg_ = "cumBuffer Init error :" + context_.recv_buffer.GetErrMsg();
            close(context_.socket);
            return false;
        }
        is_buffer_init_ = true;
    }
    else
    {
        //in case of reconnect
        context_.recv_buffer.ReSet();
    }

    struct timeval timeoutVal;
    timeoutVal.tv_sec = connect_timeout_secs_;
    timeoutVal.tv_usec = 0;
    int result = -1;

    //-------------------------------------------------
    if (sock_usage_ == SOCK_USAGE_IPC_CLIENT)
    {
        result = connect(context_.socket, (SOCKADDR*)&ipc_conn_addr_, (SOCKLEN_T)sizeof(SOCKADDR_UN));
    }
    else if (sock_usage_ == SOCK_USAGE_TCP_CLIENT)
    {
        result = connect(context_.socket, (SOCKADDR*)&tcp_server_addr_, (SOCKLEN_T)sizeof(SOCKADDR_IN));
    }
    else if (sock_usage_ == SOCK_USAGE_UDP_CLIENT)
    {
        if (!SetSockoptSndRcvBufUdp(context_.socket))
        {
            close(context_.socket);
            return false;
        }
        result = connect(context_.socket, (SOCKADDR*)&udp_server_addr_, (SOCKLEN_T)sizeof(SOCKADDR_IN));
    }
    else
    {
        err_msg_ = "invalid socket usage";
        close(context_.socket);
        return false;
    }
    //-------------------------------------------------

    if (result < 0)
    {
        if (errno != EINPROGRESS)
        {
            err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
            close(context_.socket);
            return false;
        }
    }

    if (result == 0)
    {
        is_connected_ = true;
        return RunClientThread();
    }

    fd_set   rset, wset;
    FD_ZERO(&rset);
    FD_SET(context_.socket, &rset);
    wset = rset;

    result = select(context_.socket + 1, &rset, &wset, NULL, &timeoutVal);
    if (result == 0)
    {
        err_msg_ = "connect timeout";
        close(context_.socket);
        return false;
    }
    else if (result < 0)
    {
        err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
        close(context_.socket);
        return false;
    }

    if (FD_ISSET(context_.socket, &rset) || FD_ISSET(context_.socket, &wset))
    {
        int  socketerror = 0;
        SOCKLEN_T  len = sizeof(socketerror);
        if (getsockopt(context_.socket, SOL_SOCKET, SO_ERROR, &socketerror, &len) < 0)
        {
            err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
            close(context_.socket);
            return false;
        }

        if (socketerror)
        {
            err_msg_ = "connect error [" + std::string(strerror(errno)) + "]";
            close(context_.socket);
            return false;
        }
    }
    else
    {
        err_msg_ = "connect error : fd not set ";
        std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " << err_msg_ << "\n";
        close(context_.socket);
        return false;
    }

    is_connected_ = true;

    return RunClientThread();
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::RunClientThread()
{
    if (!is_client_thread_running_)
    {
#ifdef __APPLE__
        kq_events_ptr_ = new struct kevent;
        memset(kq_events_ptr_, 0x00, sizeof(struct kevent));
        kq_fd_ = kqueue();
        if (kq_fd_ == -1)
        {
            err_msg_ = "kqueue error [" + std::string(strerror(errno)) + "]";
            close(context_.socket);
            return false;
        }
#elif __linux__
        ep_events_ = new struct epoll_event;
        memset(ep_events_, 0x00, sizeof(struct epoll_event));
        ep_fd_ = epoll_create1(0);
        if (ep_fd_ == -1)
        {
            err_msg_ = "epoll create error [" + std::string(strerror(errno)) + "]";
            close(context_.socket);
            return false;
        }
#endif

#ifdef __APPLE__
        if (!ControlKq(&context_, EVFILT_READ, EV_ADD))
        {
            close(context_.socket);
            return false;
        }
#elif __linux__
        if (!ControlEpoll(&context_, EPOLLIN | EPOLLERR, EPOLL_CTL_ADD))
        {
            close(context_.socket);
            return false;
        }
#endif
        std::thread client_thread(&ASock::ClientThreadRoutine, this);
        client_thread.detach();
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ClientThreadRoutine()
{
    is_client_thread_running_ = true;

    while (is_connected_)
    {
#ifdef __APPLE__
        struct timespec ts;
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        int event_cnt = kevent(kq_fd_, NULL, 0, kq_events_ptr_, 1, &ts);
#elif __linux__
        int event_cnt = epoll_wait(ep_fd_, ep_events_, 1, 1000);
#endif
        if (event_cnt < 0)
        {
#ifdef __APPLE__
            err_msg_ = "kevent error [" + std::string(strerror(errno)) + "]";
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
            //############## close ###########################
            close(context_.socket);
            InvokeServerDisconnectedHandler();
            break;
        }
#ifdef __APPLE__
        else if (EVFILT_READ == kq_events_ptr_->filter)
#elif __linux__
        else if (ep_events_->events & EPOLLIN)
#endif
        {
            //############## recv ############################
            if (sock_usage_ == SOCK_USAGE_UDP_CLIENT)
            {
                if (!RecvfromData(&context_))
                {
                    close(context_.socket);
                    InvokeServerDisconnectedHandler();
                    break;
                }
            }
            else
            {
                if (!RecvData(&context_))
                {
                    close(context_.socket);
                    InvokeServerDisconnectedHandler();
                    break;
                }
            }
        }
#ifdef __APPLE__
        else if (EVFILT_WRITE == kq_events_ptr_->filter)
#elif __linux__
        else if (ep_events_->events & EPOLLOUT)
#endif
        {
            //############## send ############################
            if (!SendPendingData(&context_))
            {
                return; //error!
            }
        }//send
    } //while

    is_client_thread_running_ = false;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::InvokeServerDisconnectedHandler()
{
    if (cb_on_disconnected_from_server_ != nullptr)
    {
        cb_on_disconnected_from_server_();
    }
    else
    {
        OnDisconnectedFromServer();
    }
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::SendToServer(const char* data, int len)
{
    if (!is_connected_)
    {
        err_msg_ = "not connected";
        return false;
    }

    return SendData(&context_, data, len);
}

///////////////////////////////////////////////////////////////////////////////
void ASock::Disconnect()
{
    if (context_.socket > 0)
    {
        close(context_.socket);
    }
    context_.socket = -1;
}







///////////////////////////////////////////////////////////////////////////////
void ASock::BuildErrMsgString(int nErrNo)
{
    LPVOID lpMsgBuf;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, WSAGetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);

    err_msg_ += "(";
	err_msg_ += std::to_string(nErrNo);
    err_msg_ += ") ";
	err_msg_ += std::string((LPCTSTR)lpMsgBuf);
    LocalFree(lpMsgBuf);
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::InitWinsock()
{
	WSADATA      wsaData;
	int nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (nResult != 0) 
	{
		BuildErrMsgString(WSAGetLastError());
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
		WSACleanup();
		return false;
	}

	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) 
	{
        err_msg_ = "Could not find a usable version of Winsock.dll";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
		WSACleanup();
		return false;
	}
	return true;
}
//TODO linux
//if (strerror_r(err_code, sys_msg, sizeof sys_msg) != 0)

///////////////////////////////////////////////////////////////////////////////
bool   ASock::SetNonBlocking(int nSockFd)
{
	unsigned long nMode = 1;
	int nResult = ioctlsocket(nSockFd, FIONBIO, &nMode);
	if (nResult != NO_ERROR)
	{
		BuildErrMsgString(WSAGetLastError());
        return  false;
	}
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::IssueRecv(Context* pClientContext)
{
    DWORD dwFlags = 0;
    DWORD dwRecvBytes;
    //memset(&(pClientContext->overlapped), 0, sizeof(OVERLAPPED));
    //pClientContext->socket_ = newClientSock;
    pClientContext->wsaBuf.buf = pClientContext->buffer;
    pClientContext->wsaBuf.len = DEFAULT_CAPACITY;
    pClientContext->nIoType = EnumIOType::IO_RECV;
    SecureZeroMemory((PVOID)& pClientContext->overlapped, sizeof(WSAOVERLAPPED));

    //////////////////////////////////////////////
    if (SOCKET_ERROR == WSARecv(pClientContext->socket,
        &(pClientContext->wsaBuf),
        1,
        &dwRecvBytes,
        &dwFlags,
        &(pClientContext->overlapped),
        NULL
    ))
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            BuildErrMsgString(WSAGetLastError());
            std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                      << GetLastErrMsg() << "\n";
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::Recv(Context* pContext) //XXX 이부분이 불필요?? 
{
#ifdef WIN32
#endif
    return true ;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock::Send (Context* pClientContext, const char* pData, int nLen) 
{
#ifdef WIN32
    /*
    DWORD dwFlags = 0;
    DWORD dwRecvBytes;
    pClientContext->wsaBuf.buf = pClientContext->buffer;
    pClientContext->wsaBuf.len = DEFAULT_CAPACITY;
    pClientContext->nIoType = EnumIOType::IO_RECV;

    //////////////////////////////////////////////
    if (SOCKET_ERROR == WSARecv(pClientContext->socket_,
    &(pClientContext->wsaBuf),
    1,
    &dwRecvBytes,
    &dwFlags,
    &(pClientContext->overlapped),
    NULL
    ))
    {
    if (WSAGetLastError() != WSA_IO_PENDING)
    {
    BuildErrMsgString(WSAGetLastError());
    std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " << GetLastErrMsg() << "\n";
    return false   ;
    }
    }
    */
    //--------------------------------------------------------------------------------- XXX
    //XXX 성능을 고려할것... 버퍼가 유지되어야 하는 조건을 만족하면서 성능도 고려할 방법은??????
    /*
    Overlapped Socket I/O

    If an overlapped operation completes immediately, WSASend returns a value of zero and the lpNumberOfBytesSent parameter is updated with the number of bytes sent. 
    
    If the overlapped operation is successfully initiated and will complete later, WSASend returns SOCKET_ERROR and indicates error code WSA_IO_PENDING. 
    In this case, lpNumberOfBytesSent is not updated. When the overlapped operation completes the amount of data transferred is indicated either through 
    the cbTransferred parameter in the completion routine (if specified), or through the lpcbTransfer parameter in WSAGetOverlappedResult.
    */
    //memcpy(pClientContext->buffer, pData, nLen);
    //pClientContext->sendBuffer_.Append(nLen, (char*)pData);
    //--------------------------------------------------------------------------------- XXX

    DWORD dwSendNumBytes = 0;
    pClientContext->wsaBuf.buf = (char*) pData;
    pClientContext->wsaBuf.len = nLen;
    pClientContext->nIoType = EnumIOType::IO_SEND;

    SecureZeroMemory((PVOID)& pClientContext->overlapped, sizeof(WSAOVERLAPPED));

    //////////////////////////////////////////////
    int nReturn = WSASend(pClientContext->socket,
        &(pClientContext->wsaBuf),
        1,
        &dwSendNumBytes,
        0,
        &(pClientContext->overlapped),
        NULL
    );

    if (nReturn == 0)
    {
        //모두 전송했는지?
        if (dwSendNumBytes == nLen)
        {
            std::cout << "[" << __func__ << "-" << __LINE__ 
                      << "] all sent : OK \n"; //debug
            //Sleep(1000); //TEST
        }
        else
        {
            std::cerr << "[" << __func__ << "-" << __LINE__ 
                      << "] NOT all sent : Error \n"; //debug
        }
    }
    else if (nReturn == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            BuildErrMsgString(WSAGetLastError());
            std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                      << GetLastErrMsg() << "\n";
            return false;
        }
        /*
        if (WSAGetLastError() != WSAENOTSOCK)
        {
    #ifndef _DEBUG
            // Remove Unnecessary Disconnect messages in release mode..
            if (WSAGetLastError() != WSAECONNRESET&&WSAGetLastError() != WSAECONNABORTED)
    #endif
            {
                CString msg;
                msg.Format("Error in OnWrite..: %s", ErrorCode2Text(WSAGetLastError()));
                AppendLog(msg);
            }
        }
        ReleaseBuffer(pOverlapBuff);
        DisconnectClient(pContext);
        pOverlapBuff = NULL;
        IncreaseSendSequenceNumber(pContext);
        if (m_bSendInOrder && !m_bShutDown)
            pOverlapBuff = GetNextSendBuffer(pContext);
        TRACE(">OnWrite(%x)\r\n", pContext);
        ReleaseClientContext(pContext); 	// pContext may not exist after this call 	
                                            //break; // removed due to fix. 
        */
    }
#endif

    return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// XXX XXX



///////////////////////////////////////////////////////////////////////////////
bool ASock::Connect(const char* connIP, int nPort, 
                               int nConnectTimeoutSecs)
{
    Disconnect(); 

	if(!InitWinsock())
	{
		return false;
	}
	hCompletionPort_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

    context_.socket_ = socket(AF_INET,SOCK_STREAM,0) ;
	if (context_.socket_ == INVALID_SOCKET)
	{
		BuildErrMsgString(WSAGetLastError());
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
		WSACleanup();
		return false;
	}

    if(!SetNonBlocking (context_.socket_))
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return  false;
    }
    
    if(!bCumBufferInit_ )
    {
        if  ( 
                cumbuffer_defines::OP_RSLT_OK != 
                               context_.recvBuffer_.Init(nBufferCapcity_) ||
                cumbuffer_defines::OP_RSLT_OK != 
                               context_.sendBuffer_.Init(nBufferCapcity_)
            )
        {
            err_msg_ ="cumBuffer Init error :"+context_.recvBuffer_.GetErrMsg();
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                      << GetLastErrMsg() <<"\n"; 
            return false;
        }
        bCumBufferInit_ = true;
    }
    else
    {
        //in case of reconnect
        context_.recvBuffer_.ReSet(); 
        context_.sendBuffer_.ReSet(); 
    }

    memset((void *)&connAddr_,0x00,sizeof(connAddr_)) ;
    connAddr_.sin_family      = AF_INET ;

    //connAddr_.sin_addr.s_addr = inet_addr( connIP ) ;
	inet_pton(AF_INET, connIP, &(connAddr_.sin_addr));

    connAddr_.sin_port = htons( nPort );

    struct timeval timeoutVal;
    timeoutVal.tv_sec  = nConnectTimeoutSecs ;  
    timeoutVal.tv_usec = 0;

    int nRslt = connect(context_.socket_,(SOCKADDR *)&connAddr_, 
                        (SOCKLEN_T )sizeof(SOCKADDR_IN)) ;

    if ( nRslt == SOCKET_ERROR)
    {
		int nLastErr = WSAGetLastError();
        if (nLastErr != WSAEWOULDBLOCK)
        {
			BuildErrMsgString(WSAGetLastError());
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                      << GetLastErrMsg() <<"\n"; 
            return false;
        }
    }

    if (nRslt == 0)
    {
        bConnected_ = true;
        return true;
    }

    fd_set   rset, wset, exset; //win32 !
    FD_ZERO(&rset);
    FD_SET(context_.socket_, &rset);
    wset  = rset;
    exset = rset;

    nRslt = select(context_.socket_+1, &rset, &wset, &exset, &timeoutVal ) ;
    if (nRslt == 0 )
    {
        err_msg_ = "connect timeout";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return false;
    }
    else if (nRslt< 0)
    {
		BuildErrMsgString(WSAGetLastError());
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return false;
    }

#ifdef WIN32
	WSASetLastError(0);
    if (!FD_ISSET(context_.socket_, &rset) && 
        !FD_ISSET(context_.socket_, &wset)) 
    {
        err_msg_ = "not connected!";
		std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                  << GetLastErrMsg() << "\n";
		return false;
    } 
    if (FD_ISSET(context_.socket_, &exset) ) //win32 !
    {
        err_msg_ = "not connected!";
		std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                  << GetLastErrMsg() << "\n";
		return false;
	}
#endif

    bConnected_ = true;

    if(!bClientThreadRunning_ )
    {
#ifdef WIN32
		if (NULL == CreateIoCompletionPort((HANDLE)context_.socket_, 
                                        hCompletionPort_, (DWORD)&context_, 0))
		{
			BuildErrMsgString(WSAGetLastError());
			std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                      << GetLastErrMsg() << "\n";
			return false;
		}
        /*
        if (!IssueRecv(&context_))
        {
            err_msg_ = "proacitive recv error ["  + GetLastErrMsg() + "]";
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
            return false;
        }
        */
#endif

        std::thread client_thread(&ASock::ClientThreadRoutine,this);
        client_thread.detach();
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void ASock::ClientThreadRoutine()
{
    bClientThreadRunning_ = true;
    std::cout << "[" << __func__ << "-" << __LINE__ << "] debug : \n"; //debug

#ifdef WIN32
	DWORD		dwBytesTransferred;
	PContext	pContext;

	while (true)
	{
        //XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
        //XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
        //XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
		if (FALSE == GetQueuedCompletionStatus(	hCompletionPort_,
												&dwBytesTransferred,
												(LPDWORD)&pContext, //XXX TODO --->  issue 할때마다 할당하거나, free list 에서 가져와서 처리할것!!!! 
												(LPOVERLAPPED *)&pContext,
												INFINITE
		))
        //XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
        //XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
        //XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
		{
            int nErr = WSAGetLastError();
            //TODO : hardclose 고려할것!! --> ERROR_NETNAME_DELETED 방지 : 연결이 종료된 경우 post recv 안되게 할것!!
			if (NULL != pContext)
			{
                if (nErr == ERROR_NETNAME_DELETED)
                {
                    //client hard close --> not an error
                    closesocket(pContext->socket_);
                    OnDisConnected();
                    bClientThreadRunning_ = false;
                    return;
                }
                else
                {
                    //error
                    BuildErrMsgString(nErr);
                    std::cerr << "[" << __func__ << "-" << __LINE__ 
                              << "] error! " << GetLastErrMsg() << "\n";
                    closesocket(pContext->socket_);
                    OnDisConnected();
                    bClientThreadRunning_ = false;
                    return;
                }
			}
		}

		switch (pContext->nIoType)
		{
		case EnumIOType::IO_RECV:
            std::cerr << "[" << __func__ << "-" << __LINE__ << "] recved: " 
                      << dwBytesTransferred << "\n"; //debug
            if (dwBytesTransferred == 0)
            {
                //err_msg_ = "recv 0, client disconnected , fd:" + std::to_string(pContext->socket_);
                //std::cerr << "[" << __func__ << "-" << __LINE__ << "] " << err_msg_ << "\n";
                closesocket(pContext->socket_);
                OnDisConnected();
                bClientThreadRunning_ = false;
                return;
                //pContext->nOverlappedPendingCount; //TODO XXX
            }
            pContext->recvBuffer_.Append(dwBytesTransferred,pContext->buffer);
            while (pContext->recvBuffer_.GetCumulatedLen())
            {
                //invoke user specific implementation
                size_t nOnePacketLength = GetOnePacketLength(pContext);

                if (nOnePacketLength == asocklib::MORE_TO_COME)
                {
                    //return true; //need to recv more
                    std::cerr << "[" << __func__ << "-" << __LINE__ 
                              << "] More to come  " <<  "\n"; //debug
                    break;
                }
                else if (nOnePacketLength > pContext->recvBuffer_.GetCumulatedLen())
                {
                    //return true; //need to recv more
                    std::cerr << "[" << __func__ << "-" << __LINE__ 
                              << "] More to come \n"; //debug
                    break;
                }
                else
                {
                    //got complete packet 
                    if (cumbuffer_defines::OP_RSLT_OK != 
                        pContext->recvBuffer_.GetData(nOnePacketLength, 
                                                      szOnePacketData_))
                    {
                        //error !
                        err_msg_ = pContext->recvBuffer_.GetErrMsg();
                        std::cerr << "[" << __func__ << "-" << __LINE__ 
                                  << "] " << err_msg_ << "\n";
                        //continue;
                        return ;
                    }

                    //invoke user specific implementation
                    OnRecvOnePacketData(pContext, 
                                        szOnePacketData_, 
                                        nOnePacketLength);
                }
            } //while

            if (!IssueRecv(pContext))
            {
                break;
            }
			break;

		case EnumIOType::IO_SEND:
            //TODO : XXX send 2번 보내고 응답을 기다릴때 IO_RECV , IO_SEND context 구분이 필요함!!!
            std::cout << "[" << __func__ << "-" << __LINE__ 
                      << "] IO_SEND : sent: " << dwBytesTransferred << "\n"; 
            if (!IssueRecv(pContext))
            {
                break;
            }
			break;
		}
	} //while
#endif

    bClientThreadRunning_ = false;
}

///////////////////////////////////////////////////////////////////////////////
bool ASock:: SendToServer(const char* pData, int nLen)
{
    if ( !bConnected_ )
    {
        err_msg_ = "not connected";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error : "
                  << GetLastErrMsg() <<"\n"; 
        return false;
    }

    return Send(&context_, pData, nLen);
}

///////////////////////////////////////////////////////////////////////////////
void ASock:: Disconnect()
{
    if(context_.socket_ > 0 )
    {
        closesocket(context_.socket_);
    }
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// XXX XXX



//TODO : multiple pending socket read and write operations
//TODO : options: high throughput or maximizing connections --> zero byte recv
//TODO : Post accept

/*
  https://www.codeproject.com/Articles/10330/A-simple-IOCP-Server-Client-Class
  http://www.serverframework.com/handling-multiple-pending-socket-read-and-write-operations.html
  http://microsoft.public.win32.programmer.networks.narkive.com/FRa81Gzo/about-zero-byte-receive-iocp-server
  http://www.nevelsteen.com/coding/completion_ports_in_delphi.htm
  Keep in mind that for each IssueRead/Write there will be a complimentary release of a thread resulting 
  in the case Successful IO (except in the event of an internal error, of course). 
  The thread being released is NOT necessarily the same as the one that issued the read or write. 
  It is also extremely important to keep in mind that, as SOON as the IssueRead/Write is called 
  it is best to assume a parallel running thread is already busy handling the completed I/O of 
  the current IssueRead/Write before the call exits. 
  Failure to adhere to this thinking will result in a race condition between threads. 
  So what does this mean practically? Common memory resources should not be accessed after 
  the IssueRead/Write is executed, including essentially the PerHandleData structure and members thereof.
  
  각 IssueRead / Write에는 Thread IO의 성공적인 릴리스가있을 것입니다 (물론 내부 오류가 발생한 경우는 예외). 
  릴리스되는 스레드는 반드시 읽거나 쓰는 스레드와 동일하지는 않습니다. 
  IssueRead / Write가 호출 될 때 SOON이 호출됨에 따라 호출이 종료되기 전에 병렬 실행 스레드가 
  현재 IssueRead / Write의 완료된 I / O를 처리하는 것이 이미 바쁜 것으로 가정하는 것이 가장 좋습니다. 
  이 사고를 고수하지 않으면 스레드간에 경쟁 조건이 발생합니다. 
  그렇다면 이것은 실질적으로 무엇을 의미합니까? 본질적으로 PerHandleData 구조체와 그 멤버를 포함하여 
  IssueRead / Write가 실행 된 후에는 일반적인 메모리 리소스에 액세스해서는 안됩니다. 

  http://mindgear.tistory.com/191

- Zero Byte Recv를 통한 PAGE_LOKING 최소화 http://ozt88.tistory.com/26
- http://www.viper.pe.kr/wiki2/wiki.php/Overlapped%20I/O%20%BF%CD%20IOCP%20Programming
- 에러 코드를 반드시 확인한다
- 참조 카운트를 유지한다
- http://eltgroup.tistory.com/16
	걸어놓은 recv에 대한 응답이 안 온 상태에서 send를 할 수 있으므로 소켓 1개에 대해 2개의 참조 카운트가 발생할 수 있다. 
	GetQueuedCompletionStatus가 FALSE를 리턴한 경우, 대부분의 에코서버 소스에서는 바로 소켓을 close한 다음 커넥션 객체를 삭제해 버리는데, 
	이것은 참조 카운트가 1이상 올라가지 않기 때문이다. 이런 경우 게임서버에서는 그 소켓에 대한 참조카운트가 2라면, 
	그냥 소켓을 close한 다음 나머지 작업에 대한 실패 통보가 와서 참조 카운트가 0이 되었을때 커넥션 객체를 삭제해야만 한다. 
	마찬가지로 WSARecv에 대한 리턴이 왔는데 전송 바이트가 0인 경우에도 무조건 객체를 지워서는 안된다.

	서버에서 소켓을 close하는 것이 closesocket() 호출 이전에 포스팅한 WSASend, WSARecv를 꼭 실패시키지는 않는다. 성공할 수도 있고, 실패할 수도 있다.
	서버에서 소켓을 close하면 이전에 포스팅된 연산에 대한 결과는 반드시 GetQueuedCompletionStatus에서 각각 모두 리턴된다.

	http://myblog.opendocs.co.kr/archives/1206
*/


///////////////////////////////////////////////////////////////////////////////
bool AServerSocketTCP::SetConnInfo (const char* connIP, int nPort, 
                                    int nMaxClient, int nMaxMsgLen)
{
    strServerIp_    =   connIP; 
    nServerPort_    =   nPort; 

    nMaxClientNum_  =   nMaxClient; 
    if(nMaxClientNum_<0)
    {
        return false;
    }

    return SetBufferCapacity(nMaxMsgLen);
}

///////////////////////////////////////////////////////////////////////////////
bool AServerSocketTCP::RunServer()
{
	if(!InitWinsock())
	{
		return false;
	}

	hCompletionPort_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);


    if(bServerRunning_ )
    { 
        err_msg_ = "error [server is already running]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return false;
    }

    if(nBufferCapcity_ <0)
    {
        err_msg_ = "init error [packet length is negative]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return  false;
    }

	listen_socket_ = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, 
                                WSA_FLAG_OVERLAPPED);
	if (listen_socket_ == INVALID_SOCKET)
	{
		BuildErrMsgString(WSAGetLastError());
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return false;
	}


    //XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX TODO
    /* XXX 일단 blocking 으로 처리하자!! 나중에 proactive accept 로 변경필요!!
    if(!SetNonBlocking (listen_socket_))
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return  false;
    }
    */
    //XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX TODO

    int opt_on=1;
    int nRtn = -1;

    if (setsockopt(listen_socket_,SOL_SOCKET,SO_REUSEADDR,(char*)&opt_on ,
                   sizeof(opt_on))==-1) 
    {
		BuildErrMsgString(WSAGetLastError());
        //err_msg_ = "setsockopt SO_REUSEADDR error ["  + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return false;
    }

    if (setsockopt(listen_socket_,SOL_SOCKET,SO_KEEPALIVE,(char*)&opt_on ,
                   sizeof(opt_on))==-1) 
    {
		BuildErrMsgString(WSAGetLastError());
        //err_msg_ = "setsockopt SO_REUSEADDR error ["  + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return false;
    }

    memset((void *)&serverAddr_,0x00,sizeof(serverAddr_)) ;
    serverAddr_.sin_family      = AF_INET ;
    //serverAddr_.sin_addr.s_addr = inet_addr(strServerIp_.c_str()) ;
	inet_pton(AF_INET, strServerIp_.c_str(), &(serverAddr_.sin_addr));
    serverAddr_.sin_port = htons(nServerPort_);

    nRtn = bind(listen_socket_,(SOCKADDR*)&serverAddr_,sizeof(serverAddr_)) ;
    if ( nRtn < 0 )
    {
		BuildErrMsgString(WSAGetLastError());
        //err_msg_ = "bind error ["  + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return false ;
    }

    nRtn = listen(listen_socket_,SOMAXCONN) ;
    if ( nRtn < 0 )
    {
		BuildErrMsgString(WSAGetLastError());
        //err_msg_ = "listrn error [" + std::string(strerror(errno)) + "]";
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                  << GetLastErrMsg() <<"\n"; 
        return false ;
    }

    //start server thread
    bServerRun_ = true;
    std::thread server_thread(&AServerSocketTCP::ServerThreadRoutine, this, 0);
    server_thread.detach();

    bServerRunning_ = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void AServerSocketTCP::StopServer()
{
    bServerRun_ = false;
}



///////////////////////////////////////////////////////////////////////////////
/*
bool AServerSocketTCP::IssueRecv(Context* pClientContext)
{
#ifdef WIN32
	DWORD dwFlags = 0;
	DWORD dwRecvBytes;
    //memset(&(pClientContext->overlapped), 0, sizeof(OVERLAPPED));
    //pClientContext->socket_ = newClientSock;
    pClientContext->wsaBuf.buf = pClientContext->buffer;
    pClientContext->wsaBuf.len = DEFAULT_CAPACITY;
    pClientContext->nIoType = EnumIOType::IO_RECV;
    SecureZeroMemory((PVOID)& pClientContext->overlapped, sizeof(WSAOVERLAPPED));

    //////////////////////////////////////////////
    if (SOCKET_ERROR == WSARecv(pClientContext->socket_,
        &(pClientContext->wsaBuf),
        1,
        &dwRecvBytes,
        &dwFlags,
        &(pClientContext->overlapped),
        NULL
    ))
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            BuildErrMsgString(WSAGetLastError());
            std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                      << GetLastErrMsg() << "\n";
            return false   ;
        }
    }
#endif

    return true;
}
*/

/*
///////////////////////////////////////////////////////////////////////////////
bool AServerSocketTCP::IssueSend(Context* pClientContext)
{
#ifdef WIN32
	DWORD dwSentBytes;
    pClientContext->wsaBuf.buf = pClientContext->buffer;
    pClientContext->wsaBuf.len = DEFAULT_CAPACITY;
    pClientContext->nIoType = EnumIOType::IO_SEND;
    SecureZeroMemory((PVOID)& pClientContext->overlapped, sizeof(WSAOVERLAPPED));

    //////////////////////////////////////////////
    if (SOCKET_ERROR == WSASend(pClientContext->socket_,
        &(pClientContext->wsaBuf),
        1,
        &dwSentBytes,
        0,
        &(pClientContext->overlapped),
        NULL
    ))
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            BuildErrMsgString(WSAGetLastError());
            std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                      << GetLastErrMsg() << "\n";
            return false   ;
        }
    }
#endif

    return true;
}
*/

///////////////////////////////////////////////////////////////////////////////
void AServerSocketTCP:: ServerThreadRoutine(int nCoreIndex)
{
    std::cout << "[" << __func__ << "-" << __LINE__ << "] debug \n"; //debug
#ifdef WIN32
	//DWORD dwFlags = 0;
	//DWORD dwRecvBytes;
	SYSTEM_INFO systemInfo;

	GetSystemInfo(&systemInfo);
    int nMaxWorkerThreadCnt = systemInfo.dwNumberOfProcessors * 2;
	for (int i = 0; i<nMaxWorkerThreadCnt; i++)
	{
		//_beginthreadex(NULL, 0, CompletionThread, (LPVOID)hCompletionPort, 0, NULL);
		std::thread worker_thread(&AServerSocketTCP::WorkerThreadRoutine, this);
		worker_thread.detach();
	}

	SOCKLEN_T socklen=sizeof(SOCKADDR_IN);
	SOCKADDR_IN clientAddr;
    while(bServerRun_)
	{
        //SOCKET_T newClientSock = WSAAccept(listen_socket_, 
        //                          (SOCKADDR*)&clientAddr, &socklen, 0, 0);
	    SOCKET_T newClientSock = accept(listen_socket_,
                                        (SOCKADDR*)&clientAddr,&socklen ) ;
		if (newClientSock == INVALID_SOCKET)
		{
            /* for blocking call...not in use!!
            int nLastError = WSAGetLastError();
            if (WSAEWOULDBLOCK == nLastError)
            {
                continue;
            }
			BuildErrMsgString(nLastError);
            */

			BuildErrMsgString(WSAGetLastError());
			std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                      << GetLastErrMsg() << "\n";
			break;
		}

		//////////////////////////////////////////////
		++nClientCnt_;
		SetNonBlocking(newClientSock);

		//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		//XXX : linux --> reactor
		//      IOCP  --> proactor
		//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		Context* pClientContext = PopClientContextFromCache();
		if (pClientContext == nullptr)
		{
			pClientContext = new (std::nothrow) Context();
			if (!pClientContext)
			{
				err_msg_ = "Context alloc failed !";
				std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                          << GetLastErrMsg() << "\n";
				break;
			}

			pClientContext->socket_ = newClientSock;
			if ( cumbuffer_defines::OP_RSLT_OK != 
                        pClientContext->recvBuffer_.Init(nBufferCapcity_) ||
				 cumbuffer_defines::OP_RSLT_OK != 
                        pClientContext->sendBuffer_.Init(nBufferCapcity_))
			{
				err_msg_ = "cumBuffer Init error : " + 
                                    pClientContext->recvBuffer_.GetErrMsg();
				std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                          << GetLastErrMsg() << "\n";
				break;
			}
		}

		std::pair<CLIENT_UNORDERMAP_ITER_T, bool> clientMapRslt;
		clientMapRslt =clientMap_.insert(std::pair<int,Context*>(newClientSock, 
                                                            pClientContext));
		if (!clientMapRslt.second)
		{
			err_msg_ = "clientMap_ insert error [" + 
                        std::to_string(newClientSock) + " already exist]";
			std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                      << GetLastErrMsg() << "\n";
			break;
		}

		OnClientConnected(pClientContext);

		//////////////////////////////////////////////
		if (NULL == CreateIoCompletionPort((HANDLE)newClientSock, 
                                   hCompletionPort_, (DWORD)pClientContext, 0))
		{
			BuildErrMsgString(WSAGetLastError());
			std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! " 
                      << GetLastErrMsg() << "\n";
			break;
		}

		//memset(&(pClientContext->overlapped), 0, sizeof(OVERLAPPED));
		pClientContext->socket_		= newClientSock;
        if (!IssueRecv(pClientContext))
        {
			std::cerr << "[" << __func__ << "-" << __LINE__ << "] error! \n";
            break;
        }
	} //while

    std::cerr << "[" << __func__ << "-" << __LINE__ << "] exiting! \n"; //debug
    bServerRunning_ = false;
#endif
}

///////////////////////////////////////////////////////////////////////////////
void AServerSocketTCP:: WorkerThreadRoutine()
{
    //std::cout << "[" << __func__ << "-" << __LINE__ << "] debug \n"; //debug
#ifdef WIN32
	DWORD		dwBytesTransferred;
	PContext	pContext;
	//DWORD		dwFlags;

	while (true)
	{
		if (FALSE == GetQueuedCompletionStatus(	hCompletionPort_,
												&dwBytesTransferred,
												(LPDWORD)&pContext,
												(LPOVERLAPPED *)&pContext,
												INFINITE
		))
		{
            int nErr = WSAGetLastError();
            //TODO : hardclose 고려할것!! --> ERROR_NETNAME_DELETED 방지 : 
            //       연결이 종료된 경우 post recv 안되게 할것!!
			if (NULL != pContext)
			{
                if (nErr == ERROR_NETNAME_DELETED)
                {
                    //client hard close --> not an error
                    TerminateClient(pContext);
                }
                else
                {
                    //error
                    BuildErrMsgString(nErr);
                    std::cerr << "[" << __func__ << "-" << __LINE__ 
                              << "] error! " << GetLastErrMsg() << "\n";
                    TerminateClient(pContext);
                    return;
                }
			}
		}

		switch (pContext->nIoType)
		{
		case EnumIOType::IO_ACCEPT:
            //TODO -----> 지금 blocking accept 인것을 개선할것!!
            break;

		case EnumIOType::IO_RECV:
            if (dwBytesTransferred == 0)
            {
                //err_msg_ = "recv 0, client disconnected , fd:" + 
                //          std::to_string(pContext->socket_);
                //std::cerr << "[" << __func__ << "-" << __LINE__ << "] " 
                //          << err_msg_ << "\n";
                TerminateClient(pContext);
                //pContext->nOverlappedPendingCount; //TODO XXX
                continue;
            }
            pContext->recvBuffer_.Append(dwBytesTransferred,pContext->buffer );
            while (pContext->recvBuffer_.GetCumulatedLen())
            {
                std::cerr << "[" << __func__ << "-"<< __LINE__ 
                          << "] IO_RECV : recved: "<<dwBytesTransferred<<"\n"; 
                //invoke user specific implementation
                size_t nOnePacketLength = GetOnePacketLength(pContext);

                if (nOnePacketLength == asocklib::MORE_TO_COME)
                {
                    //return true; //need to recv more
                    std::cerr << "[" << __func__ << "-" << __LINE__ 
                              << "] More to come  " <<  "\n"; //debug
                    break;
                }
                else if (nOnePacketLength > pContext->recvBuffer_.GetCumulatedLen())
                {
                    //return true; //need to recv more
                    std::cerr << "[" << __func__ << "-" << __LINE__ 
                              << "] More to come \n"; //debug
                    break;
                }
                else
                {
                    //got complete packet 
                    if (cumbuffer_defines::OP_RSLT_OK != 
                        pContext->recvBuffer_.GetData(nOnePacketLength, 
                                                      szOnePacketData_))
                    {
                        //error !
                        err_msg_ = pContext->recvBuffer_.GetErrMsg();
                        std::cerr << "[" << __func__ << "-" << __LINE__ << "] " 
                                  << err_msg_ << "\n";
                        //continue;
                        return ;
                    }

                    //invoke user specific implementation
                    OnRecvOnePacketData(pContext, szOnePacketData_, 
                                        nOnePacketLength);
                }
            } //while

            if (!IssueRecv(pContext))
            {
                break;
            }
			break;

		case EnumIOType::IO_SEND:
            //TODO : send 모든 데이터가 전송되었는지 확인!!
            std::cout << "[" << __func__ << "-" << __LINE__ 
                      << "] debug : IO_SEND noti:  \n"; //debug
            std::cout << "[" << __func__ << "-" << __LINE__ 
                      << "] IO_SEND : sent: " << dwBytesTransferred << "\n"; 
            /*
            if (!IssueSend(pContext))
            {
                break;
            }
            */
			break;
		}
	} //while
#endif
    std::cout << "[" << __func__ << "-" << __LINE__ << "] debug \n"; //debug
}

///////////////////////////////////////////////////////////////////////////////
//void  AServerSocketTCP::TerminateClient(int nClientIndex, Context* pClientContext)
void  AServerSocketTCP::TerminateClient( Context* pClientContext)
{
    --nClientCnt_;
	/*
#ifdef __APPLE__
    KqueueCtl(pClientContext->socket_, EVFILT_READ, EV_DELETE );
#elif __linux__
    EpollCtl (pClientContext->socket_, EPOLLIN | EPOLLERR | EPOLLRDHUP, EPOLL_CTL_DEL ); //just in case
#endif
	*/

    closesocket(pClientContext->socket_);
    OnClientDisConnected(pClientContext);
    PushClientInfoToCache(pClientContext);
}

///////////////////////////////////////////////////////////////////////////////
int  AServerSocketTCP::GetCountOfClients()
{
    return nClientCnt_ ; 
}










