#include <cstdlib>
#include <iostream>
#include <cassert>
#include <csignal>
#include "asock/asock_ipc_server.hpp"

// The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class Server {
  public:
    static void SigIntHandler(int signo) {
        if (signo == SIGINT) {
            std::cout << "stop server! \n";
            this_instance_->server_.StopServer();
            exit(EXIT_SUCCESS);
        } else {
            std::cerr << strerror(errno) << "/"<<signo<<"\n"; 
            exit(EXIT_FAILURE);
        }
    }
    bool InitIpcServer(const char* ipc_sock_path) {
        this_instance_ = this;
        using std::placeholders::_1;
        using std::placeholders::_2;
        using std::placeholders::_3;
        //register callbacks
        server_.SetCbOnRecvedCompletePacket(std::bind( 
                                &Server::OnRecvedCompleteData, this, _1,_2,_3));
        server_.SetCbOnClientConnected      (std::bind( 
                                &Server::OnClientConnected, this, _1));
        server_.SetCbOnClientDisconnected   (std::bind( 
                                &Server::OnClientDisconnected, this, _1));

        if(!server_.RunIpcServer(ipc_sock_path)) {
            std::cerr << server_.GetLastErrMsg() <<"\n"; 
            return false;
        }
        return true;
    }
    bool IsServerRunning(){
        return server_.IsServerRunning();
    }
    std::string GetLastErrMsg(){
        return  server_.GetLastErrMsg();
    }
  private:
    asock::ASockIpcServer server_ ; //composite usage
    static Server* this_instance_ ;
  private:
    bool OnRecvedCompleteData(asock::Context* context_ptr,
                              const char* const data_ptr, size_t len ) {
        //user specific : your whole data has arrived.
        // this is echo server
        char packet[DEFAULT_PACKET_SIZE];
        memcpy(&packet,data_ptr,len );
        packet[len] = '\0';
        std::cout << "recved [" << packet << "]\n";
        if(! server_.SendData(context_ptr, data_ptr, len) ) {
            std::cerr << GetLastErrMsg() <<"\n"; 
            exit(EXIT_FAILURE);
        }
        return true;
    }
    void OnClientConnected(asock::Context* context_ptr) {
        std::cout << "client connected : socket fd ["<< context_ptr->socket <<"]\n";
    }
    void OnClientDisconnected(asock::Context* context_ptr) {
        std::cout << "client disconnected : socket fd ["<< context_ptr->socket <<"]\n";
    }
};

Server* Server::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
    if(argc !=2) {
        std::cerr << "usage : " << argv[0] << " ipc_socket_full_path \n\n";
        exit(EXIT_FAILURE);
    }
    std::signal(SIGINT,Server::SigIntHandler);
    Server server; 
    if(!server.InitIpcServer(argv[1])) {
        exit(EXIT_FAILURE);
    }
    std::cout << "server started" << "\n";
    while( server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    exit(EXIT_SUCCESS);
}

