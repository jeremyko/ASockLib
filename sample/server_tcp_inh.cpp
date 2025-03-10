#include <cstdlib>
#include <iostream>
#include <cassert>
#include <csignal>
#include "asock/asock_tcp_server.hpp"

// The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class Server : public asock::ASockTcpServer {
  public:
    Server(){
        this_instance_ = this; 
    }
    static void SigIntHandler(int signo) {
        if (signo == SIGINT) {
            std::cout << "stop server! \n";
            this_instance_->StopServer();
            exit(EXIT_SUCCESS);
        } else {
            exit(EXIT_FAILURE);
        }
    }
  private:
    static Server* this_instance_ ;
    bool OnRecvedCompleteData(asock::Context* context_ptr, const char* const data_ptr, size_t len ) override  {
        //user specific : - your whole data has arrived.
        char packet[DEFAULT_PACKET_SIZE];
        memcpy(&packet,data_ptr,len);
        packet[len] = '\0';
        std::cout << "recved [" << packet << "]\n";
        // this is echo server
        if(! SendData(context_ptr, data_ptr, len) ) {
            std::cerr << GetLastErrMsg() <<"\n"; 
            exit(EXIT_FAILURE);
        }
        return true;
    }
    void OnClientConnected(asock::Context* context_ptr) override {
        std::cout << "client connected : socket fd ["<< context_ptr->socket <<"]\n";
    }
    void OnClientDisconnected(asock::Context* context_ptr) override {
        std::cout << "client disconnected : socket fd ["<< context_ptr->socket <<"]\n";
    }
};
Server* Server::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
    std::signal(SIGINT,Server::SigIntHandler);
    Server server;
    if(!server.RunTcpServer("127.0.0.1", 9990 )) {
        std::cerr << server.GetLastErrMsg() <<"\n"; 
        exit(EXIT_FAILURE);
    }
    std::cout << "server started" << "\n";
    while( server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    exit(EXIT_SUCCESS);
}

