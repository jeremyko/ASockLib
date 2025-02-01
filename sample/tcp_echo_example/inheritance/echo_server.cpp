#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"

///////////////////////////////////////////////////////////////////////////////
class EchoServer : public asock::ASock
{
  public:
    EchoServer(){this_instance_ = this; }
#if defined __APPLE__ || defined __linux__ 
    static void SigIntHandler(int signo);
#endif
  private:
    static  EchoServer* this_instance_ ;
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, size_t len ) override ;
    void    OnClientConnected(asock::Context* context_ptr) override; 
    void    OnClientDisconnected(asock::Context* context_ptr) override; 
};

EchoServer* EchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool    EchoServer::OnRecvedCompleteData(asock::Context* context_ptr, 
                                         char* data_ptr, size_t len ) {
    //user specific : - your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr,len);
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
#if defined __APPLE__ || defined __linux__ 
void EchoServer::SigIntHandler(int signo) {
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr << strerror(errno) <<"\n"; 
    }
    std::cout << "Stop Server! \n";
    this_instance_->StopServer();
}
#endif

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
#if defined __APPLE__ || defined __linux__ 
    std::signal(SIGINT,EchoServer::SigIntHandler);
#endif
    EchoServer echoserver; 
    if(!echoserver.RunTcpServer("127.0.0.1", 9990 )) {
        std::cerr << echoserver.GetLastErrMsg() <<"\n"; 
        return 1;
    }
    std::cout << "server started" << "\n";
    while( echoserver.IsServerRunning() ) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    return 0;
}

