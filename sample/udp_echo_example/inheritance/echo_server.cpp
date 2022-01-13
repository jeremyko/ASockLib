#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"
#include "../../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class UdpEchoServer : public asock::ASock
{
  public:
    UdpEchoServer(){this_instance_ = this; }
    static void sigint_handler(int signo);

  private:
    bool    OnRecvedCompleteData(asock::Context* context_ptr,char* data_ptr, size_t len) ;
    static  UdpEchoServer* this_instance_ ;
};

UdpEchoServer* UdpEchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoServer::OnRecvedCompleteData(asock::Context* context_ptr,char* data_ptr, size_t len) 
{
    //user specific : - your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr + CHAT_HEADER_SIZE, len - CHAT_HEADER_SIZE);
    packet[len - CHAT_HEADER_SIZE] = '\0';
    std::cout << "recved [" << packet << "]\n";
    // this is echo server
    if(! SendData(context_ptr, data_ptr, len) ) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
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
    this_instance_->StopServer();
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    std::signal(SIGINT,UdpEchoServer::sigint_handler);
    //max client is 100000, 
    //max message length is approximately 1024 bytes...
    UdpEchoServer echoserver; 
    if(!echoserver.InitUdpServer("127.0.0.1", 9990, 1024 /*,default=100000*/)) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< echoserver.GetLastErrMsg() <<"\n"; 
        return 1;
    }
    std::cout << "server started" << "\n";
    while( echoserver.IsServerRunning() ) {
        sleep(1);
    }
    std::cout << "server exit...\n";
    return 0;
}

