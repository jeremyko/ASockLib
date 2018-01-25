#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"
#include "../../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class EchoServer 
{
    public:
		EchoServer(){this_instance_ = this; }
        static void sigint_handler(int signo);
        bool    initialize_ipc_server(const char* ipc_sock_path);
        bool    is_server_running(){return ipc_server_.is_server_running();};
        std::string  get_last_err_msg(){return  ipc_server_.get_last_err_msg() ; }

    private:
        ASock ipc_server_ ; //composite usage
        static  EchoServer* this_instance_ ;
    private:
        size_t  on_calculate_data_len(asock::Context* context_ptr);
        bool    on_recved_complete_data(asock::Context* context_ptr, 
                                        char*           data_ptr, 
                                        int             len ) ;
        void    on_client_connected(asock::Context* context_ptr) ; 
        void    on_client_disconnected(asock::Context* context_ptr) ; 
};

EchoServer* EchoServer::this_instance_ = nullptr;

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

///////////////////////////////////////////////////////////////////////////////
bool EchoServer::initialize_ipc_server(const char* ipc_sock_path)
{
    //register callbacks
    ipc_server_.set_cb_on_calculate_packet_len  (std::bind(
                       &EchoServer::on_calculate_data_len, this, _1));

    ipc_server_.set_cb_on_recved_complete_packet(std::bind(
                       &EchoServer::on_recved_complete_data, this, _1,_2,_3));
    
    ipc_server_.set_cb_on_client_connected      (std::bind(
                       &EchoServer::on_client_connected, this, _1));
    
    ipc_server_.set_cb_on_client_disconnected   (std::bind(
                       &EchoServer::on_client_disconnected, this, _1));

    //max client is 100000, max message length is approximately 1024 bytes...
    if(!ipc_server_.init_ipc_server(ipc_sock_path))
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< ipc_server_.get_last_err_msg() <<"\n"; 
        return false;
    }

    return true;
}
///////////////////////////////////////////////////////////////////////////////
size_t EchoServer::on_calculate_data_len(asock::Context* context_ptr)
{
    //user specific : calculate your complete packet length here using buffer data.
    if(context_ptr->recv_buffer.GetCumulatedLen() < (int)CHAT_HEADER_SIZE )
    {
        return asock::MORE_TO_COME ; //more to come 
    }

    ST_MY_HEADER header ;
    context_ptr->recv_buffer.PeekData(CHAT_HEADER_SIZE, (char*)&header); 
    size_t supposed_total_len = std::atoi(header.msg_len) + CHAT_HEADER_SIZE;
    assert(supposed_total_len<=context_ptr->recv_buffer.GetCapacity());
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool    EchoServer::on_recved_complete_data(asock::Context* context_ptr, 
                                            char*           data_ptr, 
                                            int             len ) 
{
    //user specific : your whole data has arrived.
    // this is echo server
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    std::cout << "recved [" << packet << "]\n"; 
    
    if(! ipc_server_.send_data(context_ptr, data_ptr, len) )
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< get_last_err_msg() <<"\n"; 
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::on_client_connected(asock::Context* context_ptr) 
{
    std::cout << "client connected : socket fd ["<< context_ptr->socket <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::on_client_disconnected(asock::Context* context_ptr) 
{
    std::cout << "client disconnected : socket fd ["<< context_ptr->socket <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::sigint_handler(int signo)
{
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0)
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< strerror(errno) <<"\n"; 
    }
    
    std::cout << "Stop Server! \n";
    this_instance_->ipc_server_.stop_server();
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    if(argc !=2)
    {
        std::cout << "usage : " << argv[0] << " ipc_socket_full_path \n\n";
        return 1;
    }
    std::signal(SIGINT,EchoServer::sigint_handler);
    EchoServer echoserver; 
    echoserver.initialize_ipc_server(argv[1]);

    while( echoserver.is_server_running() )
    {
        sleep(1);
    }

    std::cout << "server exit...\n";
    return 0;
}

