#include <cstdlib>
#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"

// The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class Server {
  public:
    static void SigIntHandler(int signo);
    bool RunTcpServer();
    bool IsServerRunning(){
        return server_.IsServerRunning();
    }
    std::string GetLastErrMsg(){
        return  server_.GetLastErrMsg();
    }
  private:
    asock::ASock server_ ; //composite usage
    static Server* this_instance_ ;
  private:
    bool OnRecvedCompleteData(asock::Context* context_ptr, 
                              char* data_ptr, size_t len ) ;
    void OnClientConnected(asock::Context* context_ptr) ; 
    void OnClientDisconnected(asock::Context* context_ptr) ; 
};

Server* Server::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool Server::RunTcpServer() {
    this_instance_ = this;
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    server_.SetCbOnRecvedCompletePacket(std::bind( 
                                &Server::OnRecvedCompleteData, this, _1,_2,_3));
    server_.SetCbOnClientConnected     (std::bind( 
                                &Server::OnClientConnected, this, _1));
    server_.SetCbOnClientDisconnected  (std::bind( 
                                &Server::OnClientDisconnected, this, _1));

    if(!server_.RunTcpServer("127.0.0.1", 9990  )) {
        std::cerr<< "error! " << server_.GetLastErrMsg() <<"\n";
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool Server::OnRecvedCompleteData(asock::Context* context_ptr, 
                                         char* data_ptr, size_t len ) {
    //user specific : your whole data has arrived.
    // this is echo server
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr,len );
    packet[len] = '\0';
    std::cout << "recved [" << packet << "]\n";
    
    if(! server_.SendData(context_ptr, data_ptr, len) ) {
        std::cerr << GetLastErrMsg() <<"\n"; 
        exit(EXIT_FAILURE);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void Server::OnClientConnected(asock::Context* context_ptr) {
    std::cout << "client connected : socket fd ["<< context_ptr->socket <<"]\n";
}
///////////////////////////////////////////////////////////////////////////////
void Server::OnClientDisconnected(asock::Context* context_ptr) {
    std::cout << "client disconnected : socket fd ["<< context_ptr->socket <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
void Server::SigIntHandler(int signo) {
    if (signo == SIGINT) {
        std::cout << "stop server! \n";
        this_instance_->server_.StopServer();
        exit(EXIT_SUCCESS);
    } else {
        std::cerr << strerror(errno) << "/"<<signo<<"\n"; 
        exit(EXIT_FAILURE);
    }
}

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
    std::signal(SIGINT,Server::SigIntHandler);
    Server server;
    if(!server.RunTcpServer()){
        exit(EXIT_FAILURE);
    }
    std::cout << "server started" <<  "\n";
    while( server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    exit(EXIT_SUCCESS);
}

