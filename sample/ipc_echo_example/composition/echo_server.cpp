#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"

// NOTE: The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class EchoServer 
{
  public:
    EchoServer(){this_instance_ = this; }
    static void SigintHandler(int signo);
    bool    InitIpcServer(const char* ipc_sock_path);
    bool    IsServerRunning(){return ipc_server_.IsServerRunning();};
    std::string  GetLastErrMsg(){return  ipc_server_.GetLastErrMsg() ; }
  private:
    asock::ASock ipc_server_ ; //composite usage
    static  EchoServer* this_instance_ ;
  private:
    bool    OnRecvedCompleteData(asock::Context* context_ptr, 
                                 char* data_ptr, size_t len ) ;
    void    OnClientConnected(asock::Context* context_ptr) ; 
    void    OnClientDisconnected(asock::Context* context_ptr) ; 
};

EchoServer* EchoServer::this_instance_ = nullptr;
using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

///////////////////////////////////////////////////////////////////////////////
bool EchoServer::InitIpcServer(const char* ipc_sock_path) {
    //register callbacks
    ipc_server_.SetCbOnRecvedCompletePacket(std::bind( 
                            &EchoServer::OnRecvedCompleteData, this, _1,_2,_3));
    ipc_server_.SetCbOnClientConnected      (std::bind( 
                            &EchoServer::OnClientConnected, this, _1));
    ipc_server_.SetCbOnClientDisconnected   (std::bind( 
                            &EchoServer::OnClientDisconnected, this, _1));

    if(!ipc_server_.RunIpcServer(ipc_sock_path)) {
        std::cerr << ipc_server_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool    EchoServer::OnRecvedCompleteData(asock::Context* context_ptr, 
                                         char* data_ptr, size_t len ) {
    //user specific : your whole data has arrived.
    // this is echo server
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr,len );
    packet[len] = '\0';
    std::cout << "recved [" << packet << "]\n";
    if(! ipc_server_.SendData(context_ptr, data_ptr, len) ) {
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
void EchoServer::SigintHandler(int signo) {
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr << strerror(errno) << "/"<<signo<<"\n"; 
    }
    std::cout << "Stop Server! \n";
    this_instance_->ipc_server_.StopServer();
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
    if(argc !=2) {
        std::cout << "usage : " << argv[0] << " ipc_socket_full_path \n\n";
        return 1;
    }
    std::signal(SIGINT,EchoServer::SigintHandler);
    EchoServer echoserver; 
    echoserver.InitIpcServer(argv[1]);
    std::cout << "server started" << "\n";
    while( echoserver.IsServerRunning() ) {
        sleep(1);
    }
    std::cout << "server exit...\n";
    return 0;
}

