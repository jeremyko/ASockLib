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
#if defined __APPLE__ || defined __linux__ 
    static void SigIntHandler(int signo);
#endif
    bool    RunTcpServer();
    bool    IsServerRunning(){return tcp_server_.IsServerRunning();};
    std::string  GetLastErrMsg(){return  tcp_server_.GetLastErrMsg() ; }

  private:
    asock::ASock tcp_server_ ; //composite usage
    static  EchoServer* this_instance_ ;
  private:
    bool    OnRecvedCompleteData(asock::Context* context_ptr, 
                                 char* data_ptr, size_t len ) ;
    void    OnClientConnected(asock::Context* context_ptr) ; 
    void    OnClientDisconnected(asock::Context* context_ptr) ; 
};

EchoServer* EchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool EchoServer::RunTcpServer() {
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    tcp_server_.SetCbOnRecvedCompletePacket(std::bind( 
                                &EchoServer::OnRecvedCompleteData, this, _1,_2,_3));
    tcp_server_.SetCbOnClientConnected     (std::bind( 
                                &EchoServer::OnClientConnected, this, _1));
    tcp_server_.SetCbOnClientDisconnected  (std::bind( 
                                &EchoServer::OnClientDisconnected, this, _1));

    if(!tcp_server_.RunTcpServer("127.0.0.1", 9990  )) {
        std::cerr<< "error! " << tcp_server_.GetLastErrMsg() <<"\n";
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
    memcpy(&packet, data_ptr,len );
    packet[len] = '\0';
    std::cout << "recved [" << packet << "]\n";
    
    if(! tcp_server_.SendData(context_ptr, data_ptr, len) ) {
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
    this_instance_->tcp_server_.StopServer();
}
#endif

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
#if defined __APPLE__ || defined __linux__ 
    std::signal(SIGINT,EchoServer::SigIntHandler);
#endif
    EchoServer echoserver; 
    echoserver.RunTcpServer();
    std::cout << "server started" <<  "\n";
    while( echoserver.IsServerRunning() ) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    return 0;
}

