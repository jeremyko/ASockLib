#include <cstdlib>
#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"

// NOTE: The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class UdpEchoServer : public asock::ASock
{
  public:
    UdpEchoServer(){this_instance_ = this; }
#if defined __APPLE__ || defined __linux__ 
    static void SigIntHandler(int signo);
#endif

  private:
    bool    OnRecvedCompleteData(asock::Context* context_ptr,char* data_ptr, size_t len) override;
    static  UdpEchoServer* this_instance_ ;
};

UdpEchoServer* UdpEchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoServer::OnRecvedCompleteData(asock::Context* context_ptr,char* data_ptr, size_t len) {
    //user specific : - your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr,len );
    packet[len] = '\0';
    std::cout << "recved [" << packet << "]\n";
    // this is echo server
    if(! SendData(context_ptr, data_ptr, len) ) {
        std::cerr << GetLastErrMsg() <<"\n"; 
        exit(EXIT_FAILURE);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
#if defined __APPLE__ || defined __linux__ 
void UdpEchoServer::SigIntHandler(int signo) {
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr << strerror(errno) << "/"<<signo<<"\n"; 
        exit(EXIT_FAILURE);
    }
    std::cout << "Stop Server! \n";
    this_instance_->StopServer();
}
#endif

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
#if defined __APPLE__ || defined __linux__ 
    std::signal(SIGINT,UdpEchoServer::SigIntHandler);
#endif
    UdpEchoServer echoserver; 
    // In case of UDP, you need to know the maximum receivable size in advance and allocate a buffer.
    if(!echoserver.RunUdpServer("127.0.0.1", 9990, DEFAULT_PACKET_SIZE )) {
        std::cerr << echoserver.GetLastErrMsg() <<"\n"; 
        exit(EXIT_FAILURE);
    }
    std::cout << "server started" << "\n";
    while( echoserver.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    exit(EXIT_SUCCESS);
}

