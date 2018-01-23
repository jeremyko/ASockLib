#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"
#include "../../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class EchoServer : public ASock
{
    public:
		EchoServer(){this_instance_ = this; }
        static void sigint_handler(int signo);

    private:
        size_t  on_calculate_data_len(asock::Context* context_ptr);
        bool    on_recved_complete_data(asock::Context* context_ptr, 
                                        char*           data_ptr, 
                                        int             len ) ;
        void    on_client_connected(asock::Context* context_ptr) ; 
        void    on_client_disconnected(asock::Context* context_ptr) ; 
        static  EchoServer* this_instance_ ;
};

EchoServer* EchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
size_t EchoServer::on_calculate_data_len(asock::Context* context_ptr)
{
    //---------------------------------------------------
    //user specific : 
    //calculate your complete packet length here using buffer data.
    //---------------------------------------------------
    if(context_ptr->recv_buffer_.GetCumulatedLen() < (int)CHAT_HEADER_SIZE )
    {
        return asock::MORE_TO_COME ; //more to come 
    }

    ST_MY_HEADER header ;
    context_ptr->recv_buffer_.PeekData(CHAT_HEADER_SIZE, (char*)&header); 

    size_t supposed_total_len = std::atoi(header.msg_len) + CHAT_HEADER_SIZE;
    assert(supposed_total_len<=context_ptr->recv_buffer_.GetCapacity());
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool    EchoServer::on_recved_complete_data(asock::Context* context_ptr, 
                                            char*           data_ptr, 
                                            int             len ) 
{
    //user specific : - your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    std::cout << "recved [" << packet << "]\n"; 
    
    // this is echo server
    if(! send_data(context_ptr, data_ptr, len) )
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
    std::cout << "client connected : socket fd ["<< context_ptr->socket_ <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::on_client_disconnected(asock::Context* context_ptr) 
{
    std::cout << "client disconnected : socket fd ["<< context_ptr->socket_ <<"]\n";
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
    this_instance_->stop_server();
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

    //max client is 100000, 
    //max message length is approximately 1024 bytes...
    EchoServer echoserver; 
    if(!echoserver.init_ipc_server(argv[1]))
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< echoserver.get_last_err_msg() <<"\n"; 
        exit(1);
    }

    while( echoserver.is_server_running() )
    {
        sleep(1);
    }

    std::cout << "server exit...\n";
    return 0;
}

