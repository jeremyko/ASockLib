#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"

// NOTE: The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class UdpEchoServer 
{
  public:
    UdpEchoServer(){this_instance_ = this; }
    bool    RunUdpServer();
    bool    IsServerRunning(){return udp_server_.IsServerRunning();};
    std::string  GetLastErrMsg(){return  udp_server_.GetLastErrMsg() ; }
    static void SigIntHandler(int signo);
    asock::ASock udp_server_ ; //composite usage

    static  UdpEchoServer* this_instance_ ;
  private:
    bool    OnRecvedCompleteData(asock::Context* context_ptr,char* data_ptr, size_t len) ;
};

UdpEchoServer* UdpEchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoServer::RunUdpServer() {
    //register callbacks
    udp_server_.SetCbOnRecvedCompletePacket(
                    std::bind( &UdpEchoServer::OnRecvedCompleteData, this,
                            std::placeholders::_1, 
                            std::placeholders::_2, 
                            std::placeholders::_3));
    // In case of UDP, you need to know the maximum receivable size in advance and allocate a buffer.
    if(!udp_server_.RunUdpServer("127.0.0.1", 9990, DEFAULT_PACKET_SIZE )) {
        std::cerr << udp_server_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoServer::OnRecvedCompleteData(asock::Context* context_ptr,char* data_ptr, size_t len) {
    //user specific : your whole data has arrived. 
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr, len);
    packet[len] = '\0';
    std::cout << "recved [" << packet << "]\n";
    //this is echo server
    if(! udp_server_.SendData(context_ptr, data_ptr, len) ) {
        std::cerr << GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
#if defined __APPLE__ || defined __linux__ 
void UdpEchoServer::SigIntHandler(int signo) {
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr << strerror(errno) <<"\n"; 
    }
    std::cout << "Stop Server! \n";
    UdpEchoServer::this_instance_->udp_server_.StopServer();
}
#else
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
        // Handle the CTRL-C signal. 
    case CTRL_C_EVENT:
        DBG_LOG("Ctrl-C event");
        UdpEchoServer::this_instance_->udp_server_.StopServer();
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
#if defined __APPLE__ || defined __linux__ 
    std::signal(SIGINT,UdpEchoServer::SigIntHandler);
#else
    if (0 == SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        std::cout << "error: server exit...\n";
        return 1;
    }
#endif
    UdpEchoServer echoserver; 
    echoserver.RunUdpServer();
    std::cout << "server started" << "\n";
    while( echoserver.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    return 0;
}

