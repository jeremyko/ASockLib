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
    bool    IsServerRunning(){return udp_server_.IsServerRunning();};
    std::string  GetLastErrMsg(){return  udp_server_.GetLastErrMsg() ; }
    static void sigint_handler(int signo);

  private:
    asock::ASock udp_server_ ; //composite usage
    static  UdpEchoServer* this_instance_ ;
  private:
    bool    OnRecvedCompleteData(asock::Context* context_ptr,char* data_ptr, size_t len) ;
};

UdpEchoServer* UdpEchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoServer::initialize_udp_server()
{
    //register callbacks
    udp_server_.SetCbOnRecvedCompletePacket(std::bind( 
                    &UdpEchoServer::OnRecvedCompleteData, this,
                    std::placeholders::_1,
                    std::placeholders::_2,
                    std::placeholders::_3));
    //max message length is 1024 bytes, max client is 10000.
    if(!udp_server_.InitUdpServer("127.0.0.1", 9990, 1024 /*,default=10000*/)) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< udp_server_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoServer::OnRecvedCompleteData(asock::Context* context_ptr,char* data_ptr, size_t len) 
{
    //user specific : your whole data has arrived. 
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    std::cout << "recved [" << packet << "]\n"; 
    //this is echo server
    if(! udp_server_.SendData(context_ptr, data_ptr, len) ) {
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
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< strerror(errno) <<"\n"; 
    }
    std::cout << "Stop Server! \n";
    this_instance_->udp_server_.StopServer();
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    std::signal(SIGINT,UdpEchoServer::sigint_handler);
    UdpEchoServer echoserver; 
    echoserver.initialize_udp_server();
    while( echoserver.IsServerRunning() ) {
        sleep(1);
    }
    std::cout << "server exit...\n";
    return 0;
}

