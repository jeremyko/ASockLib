#include <iostream>
#include <cassert>
#include <csignal>//TEST

#include "ASock.hpp"
#include "msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class EchoServer 
{
    public:
		EchoServer(){this_instance_ = this; }
        bool    initialize_tcp_server();
        bool    is_server_running(){return tcp_server_.is_server_running();};
        std::string  get_last_err_msg(){return  tcp_server_.get_last_err_msg() ; }
        void    set_sighandler();

    private:
        ASock tcp_server_ ; //composite usage
        static  EchoServer* this_instance_ ;
    private:
        size_t  on_calculate_data_len(asock::Context* context_ptr);
        bool    on_recved_complete_data(asock::Context* context_ptr, 
                                        char*           data_ptr, 
                                        int             len ) ;
        void    on_client_connected(asock::Context* context_ptr) ; 
        void    on_client_disconnected(asock::Context* context_ptr) ; 
        static void sigint_handler(int signo);
};

EchoServer* EchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool EchoServer::initialize_tcp_server()
{
    //max client is 100000, max message length is approximately 1024 bytes...
    tcp_server_.init_tcp_server("127.0.0.1", 9990, 100000, 1024);

    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    tcp_server_.set_cb_on_calculate_packet_len( std::bind(
                       &EchoServer::on_calculate_data_len, this, _1));
    tcp_server_.set_cb_on_recved_complete_packet(std::bind(
                       &EchoServer::on_recved_complete_data, this, _1,_2,_3));
    tcp_server_.set_cb_on_client_connected(std::bind(
                       &EchoServer::on_client_connected, this, _1));
    tcp_server_.set_cb_on_client_disconnected(std::bind(
                       &EchoServer::on_client_disconnected, this, _1));

    //run server
    if(!tcp_server_.run_server())
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< tcp_server_.get_last_err_msg() <<"\n"; 
        return false;
    }

    return true;
}
///////////////////////////////////////////////////////////////////////////////
size_t EchoServer::on_calculate_data_len(asock::Context* context_ptr)
{
    //user specific : calculate your complete packet length here using buffer data.
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
    //user specific : your whole data has arrived.
    // this is echo server
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    
    if(! tcp_server_.send_data(context_ptr, data_ptr, len) )
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
void EchoServer::set_sighandler()
{
    std::signal(SIGINT,EchoServer::sigint_handler);
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
    this_instance_->tcp_server_.stop_server();
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    EchoServer echoserver; 
    echoserver.set_sighandler(); 
    echoserver.initialize_tcp_server();

    while( echoserver.is_server_running() )
    {
        sleep(1);
    }

    std::cout << "server exit...\n";
    return 0;
}

