#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"
#include "../../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class UdpEchoServer 
{
    public:
		UdpEchoServer(){this_instance_ = this; }
        bool    initialize_udp_server();
        bool    is_server_running(){return udp_server_.is_server_running();};
        std::string  GetLastErrMsg(){return  udp_server_.GetLastErrMsg() ; }
        static void sigint_handler(int signo);

    private:
        ASock udp_server_ ; //composite usage
        static  UdpEchoServer* this_instance_ ;
    private:
        bool    on_recved_complete_data(Context* context_ptr,char* data_ptr, int len) ;
};

UdpEchoServer* UdpEchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoServer::initialize_udp_server()
{
    //register callbacks
    udp_server_.set_cb_on_recved_complete_packet(std::bind( 
                    &UdpEchoServer::on_recved_complete_data, this,
                    std::placeholders::_1,
                    std::placeholders::_2,
                    std::placeholders::_3));

    //max message length is 1024 bytes, max client is 10000.
    if(!udp_server_.init_udp_server("127.0.0.1", 9990, 1024 /*,default=10000*/))
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< udp_server_.GetLastErrMsg() <<"\n"; 
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool    UdpEchoServer::on_recved_complete_data(Context* context_ptr,char* data_ptr, int len) 
{
    //user specific : your whole data has arrived. 
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    std::cout << "recved [" << packet << "]\n"; 
    
    //this is echo server
    if(! udp_server_.send_data(context_ptr, data_ptr, len) ) 
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void UdpEchoServer::sigint_handler(int signo)
{
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0)
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< strerror(errno) <<"\n"; 
    }
    
    std::cout << "Stop Server! \n";
    this_instance_->udp_server_.stop_server();
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    std::signal(SIGINT,UdpEchoServer::sigint_handler);
    UdpEchoServer echoserver; 
    echoserver.initialize_udp_server();

    while( echoserver.is_server_running() )
    {
        sleep(1);
    }

    std::cout << "server exit...\n";
    return 0;
}

