#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"

// NOTE: The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class EchoServer : public asock::ASock
{
  public:
    EchoServer(){this_instance_ = this; }
    static void SigIntHandler(int signo);

  private:
    bool    OnRecvedCompleteData(asock::Context* context_ptr, 
                                 char* data_ptr, size_t len ) override;
    void    OnClientConnected(asock::Context* context_ptr) override;
    void    OnClientDisconnected(asock::Context* context_ptr) override;
    static  EchoServer* this_instance_ ;
};

EchoServer* EchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool    EchoServer::OnRecvedCompleteData(asock::Context* context_ptr, 
                                         char*  data_ptr, size_t len ) {
    //user specific : - your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr, len);
    packet[len] = '\0';
    std::cout << "recved [" << packet << "]\n";
    // this is echo server
    if(! SendData(context_ptr, data_ptr, len) ) {
        std::cerr << GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::OnClientConnected(asock::Context* context_ptr) {
    std::cout << "client connected : socket fd ["<< context_ptr->socket <<"]\n";
}
///////////////////////////////////////////////////////////////////////////////
void EchoServer::OnClientDisconnected(asock::Context* context_ptr) {
    std::cout << "client disconnected : socket fd ["<< context_ptr->socket <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::SigIntHandler(int signo) {
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr << strerror(errno) << "/"<<signo<<"\n"; 
    }
    std::cout << "Stop Server! \n";
    this_instance_->StopServer();
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
    if(argc !=2) {
        std::cout << "usage : " << argv[0] << " ipc_socket_full_path \n\n";
        return 1;
    }
    std::signal(SIGINT,EchoServer::SigIntHandler);
    EchoServer echoserver; 
    if(!echoserver.RunIpcServer(argv[1])) {
        std::cerr << echoserver.GetLastErrMsg() <<"\n"; 
        exit(1);
    }std::cout << "server started" << "\n";
    while( echoserver.IsServerRunning() ) {
        sleep(1);
    }
    std::cout << "server exit...\n";
    return 0;
}

