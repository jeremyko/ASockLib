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

#ifndef __ASOCK_HPP__
#define __ASOCK_HPP__

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

//======================
#ifdef __APPLE__
//======================
#include <sys/select.h>
#include <sys/types.h>
#include <sys/event.h>
typedef struct  sockaddr_un SOCKADDR_UN ;
typedef         socklen_t   SOCKLEN_T ;
#endif

//======================
#if __linux__
//======================
#include <sys/epoll.h>
typedef struct  sockaddr_un SOCKADDR_UN ;
typedef         socklen_t   SOCKLEN_T ;
#endif

//======================
#if WIN32
//======================
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#pragma comment(lib, "Ws2_32.lib")
typedef         int   SOCKLEN_T ;
#endif
//======================

#include <atomic>
#include <thread>
#include <queue>
#include <deque>
#include <unordered_map>
#include <mutex> 
#include <functional>
#include "CumBuffer.h"

typedef struct  sockaddr_in SOCKADDR_IN ;
typedef struct  sockaddr    SOCKADDR ;

///////////////////////////////////////////////////////////////////////////////
namespace asock
{
    const int       DEFAULT_PACKET_SIZE =1024;
    const int       DEFAULT_CAPACITY    =1024;
    const int       DEFAULT_MAX_CLIENT  =10000;
    const size_t    MORE_TO_COME        = -1;

    typedef struct _PENDING_SENT_
    {
        char*       pending_sent_data ; 
        int         pending_sent_len  ;
        SOCKADDR_IN udp_remote_addr   ; //for udp pending sent 
    } PENDING_SENT ;

#ifdef WIN32
	typedef		SOCKET SOCKET_T;
#endif
#if defined __APPLE__ || defined __linux__ 
	typedef		int    SOCKET_T;
#endif

#ifdef WIN32
	enum class EnumIOType 
	{
        IO_ACCEPT,
		IO_SEND,
		IO_RECV
	};

	typedef struct _Context_ 
	{
		OVERLAPPED			overlapped;
		WSABUF				wsaBuf;
		char                buffer[DEFAULT_CAPACITY];
		CumBuffer			recvBuffer;
		//CumBuffer			sendBuffer;
		asock::SOCKET_T     socket; //XXX rename
		std::mutex			clientSendLock; 
        bool                bPacketLenCalculated{ false };
		int					nOverlappedPendingCount;
		EnumIOType			nIoType;
	} Context, *PContext;


	/*
	typedef struct _PER_HANDLE_DATA_ //TODO
	{
		asock::SOCKET_T  socket_;
		SOCKADDR_STORAGE sockAddr;
	} PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

	typedef struct _PER_IO_DATA_
	{
		OVERLAPPED overlapped;
		CumBuffer       recvBuffer_;
		//CumBuffer       sendBuffer_;
		//CHAR       buffer[BUFSIZE];
		WSABUF     wsaBuf;
	} PER_IO_DATA, *LPPER_IO_DATA;

	typedef struct __WSABUF
	{
		u_long    len;
		char FAR  *buf;
	} WSABUF, *LPWABUF;

	*/
#endif

#if defined __APPLE__ || defined __linux__ 
    typedef struct _Context_
    {
        CumBuffer       recv_buffer;
        int             socket{-1};
        std::mutex      send_lock ; 
        bool            is_packet_len_calculated {false};
        size_t          complete_packet_len_ {0} ;
        std::deque<PENDING_SENT> pending_send_deque_ ; 
        bool            is_sent_pending {false}; 
        SOCKADDR_IN     udp_remote_addr ; //for udp
    } Context ;
#endif

    typedef enum _ENUM_SOCK_USAGE_
    {
        SOCK_USAGE_UNKNOWN = 0 ,
        SOCK_USAGE_TCP_SERVER ,
        SOCK_USAGE_UDP_SERVER ,
        SOCK_USAGE_IPC_SERVER , 
        SOCK_USAGE_TCP_CLIENT ,
        SOCK_USAGE_UDP_CLIENT ,
        SOCK_USAGE_IPC_CLIENT ,

    } ENUM_SOCK_USAGE ;
} 

using Context = asock::Context ;
using ENUM_SOCK_USAGE = asock::ENUM_SOCK_USAGE ;
using CLIENT_UNORDERMAP_T      = std::unordered_map<int, Context*> ;
using CLIENT_UNORDERMAP_ITER_T = std::unordered_map<int, Context*>::iterator ;

///////////////////////////////////////////////////////////////////////////////
class ASock
{
    public :
        ASock();
        virtual ~ASock()  ;

        //XXX 함수명을 카멜표기로 TODO  
        //XXX 변수명은 소문자 TODO
        bool        SetBufferCapacity(int max_data_len);
        std::string GetLastErrMsg(){return err_msg_; }
#if defined __APPLE__ || defined __linux__ 
        bool    SetSocketNonBlocking(int sock_fd);
        bool    send_data(Context* context_ptr, const char* data_ptr, int len); 
#endif
        std::string GetLastErrMsg() {
            return err_msg_; 
        }

    protected :
        char*      complete_packet_data_ {nullptr}; 
        size_t     recv_buffer_capcity_{0};
        //size_t     max_data_len_ {0};
        int        send_buffer_capcity_ {asock::DEFAULT_CAPACITY};

#ifdef WIN32
		bool	InitWinsock();
		void	BuildErrMsgString(int nErrNo);
        bool    SetNonBlocking(int sock_fd);
        bool    Send(Context* pClientContext, const char* pData, int nLen);
        bool    Recv(Context* pContext); //XXX 이부분이 불필요?? 
#endif

#ifdef __APPLE__
        struct     kevent* kq_events_ptr_ {nullptr};
        int        kq_fd_ {-1};
#elif __linux__
        struct     epoll_event* ep_events_{nullptr};
        int        ep_fd_ {-1};
#elif WIN32
		HANDLE          hCompletionPort_;
#endif
        std::string   err_msg_ ;
        ENUM_SOCK_USAGE sock_usage_ {asock::SOCK_USAGE_UNKNOWN};

    protected :
#if defined __APPLE__ || defined __linux__ 
        bool   recv_data(Context* context_ptr);
        bool   recvfrom_data(Context* context_ptr) ; //udp
#endif
#ifdef __APPLE__
        bool  control_kq(Context* context_ptr,uint32_t events,uint32_t fflags);
#elif __linux__
        bool  control_ep(Context* context_ptr , uint32_t events, int op);
#elif WIN32
        bool   IssueRecv(Context* pClientContext);
        //TODO
#endif

    private:
        bool set_sockopt_snd_rcv_buf_for_udp(int socket);
        bool send_pending_data(Context* context_ptr);

        //choose usage, inheritance or composition.
        //1.for inheritance : Implement these virtual functions.
        virtual size_t  on_calculate_data_len(Context* context_ptr)
        {
            std::cerr << "ERROR! on_calculate_data_len not implemented!\n";
            return -1;
        };

        virtual bool    on_recved_complete_data(Context* context_ptr, 
                                                char*    data_ptr, 
                                                int      len)
        {
            std::cerr << "ERROR! on_recved_complete_data not implemented!\n";
            return false;
        }; 

        //2.for composition : Assign yours to these callbacks 
    public:
        bool set_cb_on_calculate_packet_len(std::function<size_t(Context*)> cb)  ;
        bool set_cb_on_recved_complete_packet(std::function<bool(Context*,char*,int)> cb) ;
    private:
        std::function<size_t(Context*)>   cb_on_calculate_data_len_ {nullptr} ;
        std::function<bool(Context*,char*,int)>cb_on_recved_complete_packet_{nullptr};

    //---------------------------------------------------------    
    // CLIENT Usage
    //---------------------------------------------------------    
    public :
        bool   init_tcp_client(const char* server_ip, 
                               int         server_port, 
                               int         connect_timeout_secs=10, 
                               int  max_data_len = asock::DEFAULT_PACKET_SIZE);

        bool   init_udp_client(const char* server_ip, 
                               int         server_port, 
                               int  max_data_len = asock::DEFAULT_PACKET_SIZE);

        bool   init_ipc_client(const char* sock_path, 
                               int         connect_timeout_secs=10,
                               int  max_data_len=asock::DEFAULT_PACKET_SIZE); 

        bool   connect_to_server();  
        bool   send_to_server (const char* data, int len) ; 
        void   disconnect() ;
        int    get_socket () { return  context_.socket ; }
        bool   is_connected() { return is_connected_;}

    private :
        std::atomic<bool> is_client_thread_running_ {false};
        bool     is_buffer_init_ {false};
        bool     is_connected_ {false};
        Context  context_;
#if defined __APPLE__ || defined __linux__ 
        SOCKADDR_UN ipc_conn_addr_   ;
#endif
        SOCKADDR_IN tcp_server_addr_ ;
        SOCKADDR_IN udp_server_addr_ ;
        int connect_timeout_secs_    ;

    private :
        bool run_client_thread();
        void client_thread_routine();
        void invoke_server_disconnected_handler();

        //for composition : Assign yours to these callbacks 
    public :
        bool set_cb_on_disconnected_from_server(std::function<void()> cb)  ;
    private :
        std::function<void()> cb_on_disconnected_from_server_ {nullptr} ;

        //for inheritance : Implement these virtual functions.
        virtual void    on_disconnected_from_server() {}; 

    //---------------------------------------------------------    
    // SERVER Usage
    //---------------------------------------------------------    
    public :
        bool  init_tcp_server (const char* bind_ip, 
                               int         bind_port, 
                               int  max_data_len=asock::DEFAULT_PACKET_SIZE,
                               int  max_client=asock::DEFAULT_MAX_CLIENT);

        bool  init_udp_server (const char* bind_ip, 
                               int         bind_port, 
                               int  max_data_len=asock::DEFAULT_PACKET_SIZE,
                               int  max_client=asock::DEFAULT_MAX_CLIENT);

        bool  init_ipc_server (const char* sock_path, 
                               int  max_data_len=asock::DEFAULT_PACKET_SIZE,
                               int  max_client=asock::DEFAULT_MAX_CLIENT );
        bool  run_server();
        bool  is_server_running(){return is_server_running_;};
        void  stop_server();
        int   get_max_client_limit(){return max_client_limit_ ; }
        int   get_count_of_clients(){ return client_cnt_ ; }

    private :
        std::string       server_ip_   ;
        std::string       server_ipc_socket_path_ ;
        int               server_port_ {-1};
        std::atomic<int>  client_cnt_ {0}; 
        std::atomic<bool> is_need_server_run_ {false};
        std::atomic<bool> is_server_running_  {false};
        int  listen_socket_     {-1};
        int  max_client_limit_  {-1};

        CLIENT_UNORDERMAP_T  client_map_;
        std::queue<Context*> queue_client_cache_;
#if defined __APPLE__ || defined __linux__ 
        Context*  listen_context_ptr_ {nullptr};
#endif

    private :
        void        server_thread_routine();
        void        server_thread_udp_routine();
        void        terminate_client(Context* context_ptr);
        void        push_client_context_to_cache(Context* context_ptr);
        void        clear_client_cache();
        bool        accept_new_client();
        Context*    pop_client_context_from_cache();

#ifdef WIN32
		void		WorkerThreadRoutine(); //IOCP 
        //bool      IssueRecv(Context* pClientContext);
        //bool      IssueSend(Context* pClientContext);
#endif
        //for composition : Assign yours to these callbacks 
    public :
        bool  set_cb_on_client_connected(std::function<void(Context*)> cb) ;
        bool  set_cb_on_client_disconnected(std::function<void(Context*)> cb);
    private :
        std::function<void(Context*)> cb_on_client_connected_ {nullptr} ;
        std::function<void(Context*)> cb_on_client_disconnected_ {nullptr};

        //for inheritance : Implement these virtual functions.
        virtual void    on_client_connected(Context* context_ptr) {}; 
        virtual void    on_client_disconnected(Context* context_ptr) {} ;  
};

#endif 


