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

#ifndef ASOCK_HPP
#define ASOCK_HPP

#include <atomic>
#include <thread>
#include <queue>
#include <deque>
#include <unordered_map>
#include <mutex> 
#include <functional>
#include <chrono>
#include "CumBuffer.h"

////////////////////////////////////////////////////////////////////////////////
#if defined __APPLE__ || defined __linux__ 
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/time.h>
#endif
////////////////////////////////////////////////////////////////////////////////
#ifdef __APPLE__
#include <sys/select.h>
#include <sys/types.h>
#include <sys/event.h>
#endif
////////////////////////////////////////////////////////////////////////////////
#if __linux__
#include <sys/epoll.h>
#endif
////////////////////////////////////////////////////////////////////////////////
#if WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#pragma comment(lib, "Ws2_32.lib")
#endif
////////////////////////////////////////////////////////////////////////////////
#if defined __APPLE__ || defined __linux__ 
typedef struct  sockaddr_un SOCKADDR_UN ;
typedef         socklen_t   SOCKLEN_T ;
#endif

#ifdef WIN32
typedef         int   SOCKLEN_T ;
#endif
////////////////////////////////////////////////////////////////////////////////
typedef struct  sockaddr_in SOCKADDR_IN ;
typedef struct  sockaddr    SOCKADDR ;

////////////////////////////////////////////////////////////////////////////////
#define LOG_WHERE "("<<__FILE__<<":"<<__func__<<":"<<__LINE__<<") "
#if defined __APPLE__ || defined __linux__ 
#define COLOR_RED  "\x1B[31m"
#define COLOR_GREEN "\x1B[32m" 
#define COLOR_BLUE "\x1B[34m"
#define COLOR_RESET "\x1B[0m"
#endif

#define  LOG(x)  std::cout<<LOG_WHERE << x << "\n"
#define  ELOG(x) std::cerr<<LOG_WHERE << x << "\n"

#ifdef DEBUG_PRINT
#if defined __APPLE__ || defined __linux__ 
#define  DBG_LOG(x)  std::cout<<LOG_WHERE << x << "\n"
#define  DBG_RED_LOG(x) std::cout<<LOG_WHERE << COLOR_RED<< x << COLOR_RESET << "\n"
#define  DBG_BLUE_LOG(x) std::cout<<LOG_WHERE << COLOR_BLUE<< x << COLOR_RESET << "\n"
#define  DBG_GREEN_LOG(x) std::cout<<LOG_WHERE << COLOR_GREEN<< x << COLOR_RESET << "\n"
#define  DBG_ELOG(x) std::cerr<<LOG_WHERE << COLOR_RED<< x << COLOR_RESET << "\n"
#endif //__APPLE__ , __linux__

#ifdef WIN32
//windows --> no color support
#define  DBG_LOG(x)  std::cout<<LOG_WHERE << x << "\n"
#define  DBG_ELOG(x) std::cerr<<LOG_WHERE << x << "\n"
#endif // WIN32

#else //DEBUG_PRINT
#define  DBG_LOG(x) 
#define  DBG_ELOG(x) 
#define  DBG_RED_LOG(x) 
#define  DBG_BLUE_LOG(x) 
#define  DBG_GREEN_LOG(x)
#endif //DEBUG_PRINT

///////////////////////////////////////////////////////////////////////////////
namespace asock {
    const size_t  DEFAULT_PACKET_SIZE =1024;
    const size_t  DEFAULT_CAPACITY    =1024;
    const size_t  DEFAULT_MAX_CLIENT  =10000;
    const size_t  MORE_TO_COME        =0;

    typedef enum _ENUM_SOCK_USAGE_ {
        SOCK_USAGE_UNKNOWN = 0 ,
        SOCK_USAGE_TCP_SERVER ,
        SOCK_USAGE_UDP_SERVER ,
        SOCK_USAGE_IPC_SERVER , 
        SOCK_USAGE_TCP_CLIENT ,
        SOCK_USAGE_UDP_CLIENT ,
        SOCK_USAGE_IPC_CLIENT 
    } ENUM_SOCK_USAGE ;

    typedef struct _PENDING_SENT_ {
        char*       pending_sent_data ; 
        size_t      pending_sent_len  ;
        SOCKADDR_IN udp_remote_addr   ; //for udp pending sent 
    } PENDING_SENT ;

    //-------------------------------------------
#if defined __APPLE__ || defined __linux__ 
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
#endif //__APPLE__ or __linux__

    //-------------------------------------------
#ifdef WIN32
    typedef SOCKET SOCKET_T;
    enum class EnumIOType {
        IO_ACCEPT,
        IO_SEND,
        IO_RECV,
        IO_QUIT,
		IO_UNKNOWN
    };
    typedef struct _PER_IO_DATA_ {
        OVERLAPPED	overlapped ;
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
        PER_IO_DATA* per_recv_io_ctx { NULL }; //XXX TODO multiple wasrecv
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
#endif //WIN32


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
class ASock
{
  public :
    ASock();
    virtual ~ASock()  ;

    bool SendData(Context* ctx_ptr, const char* data_ptr, size_t len); 
    bool SetSocketNonBlocking(int sock_fd);
    bool SetBufferCapacity(size_t max_data_len) {
        if(max_data_len<=0) {
            err_msg_ = " length is invalid";
            return false;
        }
        max_data_len_ = max_data_len * 20 ; 
        return true;
    }
    std::string GetLastErrMsg(){return err_msg_; }
    //-----------------------------------------------------
    //for composition : Assign yours to these callbacks 
    bool SetCbOnCalculatePacketLen(std::function<size_t(Context*)> cb) {
        //for composition usage 
        if (cb != nullptr) {
            cb_on_calculate_data_len_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }
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

  protected :
    size_t  recv_buffer_capcity_{0};
    size_t  max_data_len_ {0};
    std::mutex   lock_ ; 
#ifdef __APPLE__
    struct  kevent* kq_events_ptr_ {nullptr};
    int     kq_fd_ {-1};
#elif __linux__
    struct  epoll_event* ep_events_{nullptr};
    int     ep_fd_ {-1};
#elif WIN32
  public:
	HANDLE  handle_completion_port_;
#endif
  protected :
    std::mutex   err_msg_lock_ ; 
    std::string  err_msg_ ;
    size_t send_buffer_capcity_ {asock::DEFAULT_CAPACITY};
    ENUM_SOCK_USAGE sock_usage_ {asock::SOCK_USAGE_UNKNOWN};
    std::function<size_t(Context*)>   cb_on_calculate_data_len_ {nullptr} ;
    std::function<bool(Context*,char*,size_t)>cb_on_recved_complete_packet_{nullptr};

  protected :
    bool SetSockoptSndRcvBufUdp(SOCKET_T socket);
#ifdef WIN32
    bool InitWinsock();
	void StartWorkerThreads();
    bool StartServer();
    void BuildErrMsgString(int err_no);
	bool RecvData(size_t worker_id, Context* ctx_ptr, DWORD bytes_transferred);
	bool RecvfromData(size_t worker_id, Context* ctx_ptr, DWORD bytes_transferred); //udp
    void ReSetCtxPtr(Context* ctx_ptr);
    bool SendPendingData(); //client only
#endif
#if defined __APPLE__ || defined __linux__ 
    bool SendPendingData(Context* ctx_ptr);
    bool RecvData(Context* ctx_ptr);
    bool RecvfromData(Context* ctx_ptr) ; //udp
#endif
#ifdef __APPLE__
    bool ControlKq(Context* ctx_ptr,uint32_t events,uint32_t fflags);
#elif __linux__
    bool ControlEpoll(Context* ctx_ptr , uint32_t events, int op);
#elif WIN32
    bool IssueRecv(size_t worker_index, Context* client_ctx);
#endif

  private:
    //-----------------------------------------------------
    //for inheritance : Implement these virtual functions.
    virtual size_t  OnCalculateDataLen(Context* ctx_ptr) {
        std::cerr << "ERROR! OnCalculateDataLen not implemented!\n";
        return -1;
    };
    virtual bool    OnRecvedCompleteData(Context* ctx_ptr, 
            char* data_ptr, size_t size_t) {
        std::cerr << "ERROR! OnRecvedCompleteData not implemented!\n";
        return false;
    }; 

    //---------------------------------------------------------    
    // CLIENT Usage
    //---------------------------------------------------------    
  public :
    bool InitTcpClient(const char* server_ip, 
                       unsigned short  server_port, 
                       int         connect_timeout_secs=10, 
                       size_t  max_data_len = asock::DEFAULT_PACKET_SIZE);

    bool InitUdpClient(const char* server_ip, 
                       unsigned short  server_port, 
                       size_t  max_data_len = asock::DEFAULT_PACKET_SIZE);

#if defined __APPLE__ || defined __linux__ 
    bool InitIpcClient(const char* sock_path, 
                       int         connect_timeout_secs=10,
                       size_t  max_data_len=asock::DEFAULT_PACKET_SIZE); 
#endif
    bool SendToServer (const char* data, size_t len) ; 
    SOCKET_T  GetSocket () { return  context_.socket ; }
    bool IsConnected() { 
        if (is_connected_ && is_client_thread_running_) {
            return true;
        }
        return false;
    }
    void Disconnect() ;
    void WaitForClientLoopExit();

  private :
    int         connect_timeout_secs_    ;
    bool        is_buffer_init_ {false};
    std::atomic<bool>    is_connected_   {false};
    Context     context_;
    SOCKADDR_IN tcp_server_addr_ ;
    SOCKADDR_IN udp_server_addr_ ;
    std::thread client_thread_;
#if defined __APPLE__ || defined __linux__ 
    SOCKADDR_UN ipc_conn_addr_   ;
#endif
    std::atomic<bool> is_client_thread_running_ {false};

  private :
    bool RunServer();
    bool ConnectToServer();  
    bool RunClientThread();
    void ClientThreadRoutine();
    void InvokeServerDisconnectedHandler();

    //for composition : Assign yours to these callbacks 
  public :
    void TerminateClient(Context* ctx_ptr,bool is_graceful=true);
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
    // SERVER Usage
    //---------------------------------------------------------    
  public :
    bool  InitTcpServer (const char* bind_ip, 
                         int         bind_port, 
                         size_t  max_data_len=asock::DEFAULT_PACKET_SIZE,
                         size_t  max_client=asock::DEFAULT_MAX_CLIENT);

    bool  InitUdpServer (const char* bind_ip, 
                         size_t      bind_port, 
                         size_t  max_data_len=asock::DEFAULT_PACKET_SIZE,
                         size_t  max_client=asock::DEFAULT_MAX_CLIENT);

#if defined __APPLE__ || defined __linux__ 
    bool  InitIpcServer (const char* sock_path, 
                         size_t  max_data_len=asock::DEFAULT_PACKET_SIZE,
                         size_t  max_client=asock::DEFAULT_MAX_CLIENT );
#endif
    bool  IsServerRunning(){return is_server_running_;};
    void  StopServer();
    size_t  GetMaxClientLimit(){return max_client_limit_ ; }
    int   GetCountOfClients(){ return client_cnt_ ; }
#ifdef WIN32
    size_t  GetCountOfContextQueue(){ 
        std::lock_guard<std::mutex> lock(ctx_cache_lock_);
        return queue_ctx_cache_.size(); 
    }
#endif

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
#if defined __APPLE__ || defined __linux__ 
    Context*  listen_context_ptr_ {nullptr};
#endif
#ifdef WIN32
    bool use_zero_byte_receive_{ false };
    std::queue<PER_IO_DATA*> queue_per_io_data_cache_;
    std::mutex           per_io_data_cache_lock_ ; 
#endif

  private :
#if defined __APPLE__ || defined __linux__ 
    void        ServerThreadRoutine();
    void        ServerThreadUdpRoutine();
#endif
    bool        BuildClientContextCache();
    void        PushClientContextToCache(Context* ctx_ptr);
    void        ClearClientCache();
    bool        AcceptNewClient();
    Context*    PopClientContextFromCache();

#ifdef WIN32
    bool         BuildPerIoDataCache();
    PER_IO_DATA* PopPerIoDataFromCache();
    void         PushPerIoDataToCache(PER_IO_DATA* per_io_data_ptr);
    void         ClearPerIoDataCache();
    void        AcceptThreadRoutine();
    void        UdpServerThreadRoutine();
    void        WorkerThreadRoutine(size_t worker_index) ; //IOCP 

    //if you want to maximum concurrent connections TODO
    void        UseZeroByteReceiveBuffer() { use_zero_byte_receive_ = true; } 
#endif
    //for composition : Assign yours to these callbacks 
  public :
    bool  SetCbOnClientConnected(std::function<void(Context*)> cb) {
        if (cb != nullptr) {
            cb_on_client_connected_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }
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
} //namespace 
#endif // ASOCK_HPP 


