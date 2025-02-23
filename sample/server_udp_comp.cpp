#include <cstdlib>
#include <iostream>
#include <cassert>
#include <csignal>
#include "asock/asock_udp_server.hpp"

// The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class Server {
  public:
    bool RunUdpServer() {
        this_instance_ = this;
        //register callbacks
        server_.SetCbOnRecvedCompletePacket(
                        std::bind( &Server::OnRecvedCompleteData, this,
                                std::placeholders::_1, std::placeholders::_2,
                                std::placeholders::_3));
        // In case of UDP, you need to know the maximum receivable size in advance 
        // and allocate a buffer.
        if(!server_.RunUdpServer("127.0.0.1", 9990, DEFAULT_PACKET_SIZE )) {
            std::cerr << server_.GetLastErrMsg() <<"\n"; 
            return false;
        }
        return true;
    }
    bool IsServerRunning(){
        return server_.IsServerRunning();
    }
    std::string GetLastErrMsg(){
        return server_.GetLastErrMsg();
    }
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
    asock::ASockUdpServer server_ ; //composite usage
    static Server* this_instance_ ;
  private:
    bool OnRecvedCompleteData(asock::Context* context_ptr,const char* const data_ptr, size_t len) {
        //user specific : your whole data has arrived. 
        char packet[DEFAULT_PACKET_SIZE];
        memcpy(&packet, data_ptr, len);
        packet[len] = '\0';
        std::cout << "recved [" << packet << "]\n";
        //this is echo server
        if(! server_.SendData(context_ptr, data_ptr, len) ) {
            std::cerr << GetLastErrMsg() <<"\n"; 
            exit(EXIT_FAILURE);
        }
        return true;
    }
};

Server* Server::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
    std::signal(SIGINT,Server::SigIntHandler);
    Server server;
    if(!server.RunUdpServer()){
        exit(EXIT_FAILURE);
    }
    std::cout << "server started" << "\n";
    while( server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    exit(EXIT_SUCCESS);
}

