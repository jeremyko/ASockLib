#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include <csignal>
#include "asock/asock_ipc_client.hpp"

// The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class Client : public asock::ASockIpcClient
{
  public:
    Client(){
        this_instance_ = this;
    }
    static void SigIntHandler(int signo);
  private:
    static Client* this_instance_ ;
    bool OnRecvedCompleteData(asock::Context* context_ptr, 
                              const char* const data_ptr, size_t len) override;
    void OnDisconnectedFromServer() override;
};

Client* Client::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool Client:: OnRecvedCompleteData(asock::Context* , const char* const  data_ptr, size_t len) {
    //user specific : - your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr, len );
    packet[len] = '\0';
    std::cout << "server response [" << packet << "]\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void Client::OnDisconnectedFromServer() {
    std::cout << "server disconnected, terminate client\n";
    Disconnect();
}

///////////////////////////////////////////////////////////////////////////////
void Client::SigIntHandler(int signo) {
    if (signo == SIGINT) {
        std::cout << "stop client\n";
        this_instance_->Disconnect();
        exit(EXIT_SUCCESS);
    } else {
        std::cerr << strerror(errno) << "/"<<signo<<"\n"; 
        exit(EXIT_FAILURE);
    }
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
    if(argc !=2) {
        std::cout << "usage : " << argv[0] << " ipc_socket_full_path \n\n";
        exit(EXIT_FAILURE);
    }
    std::signal(SIGINT,Client::SigIntHandler);
    Client client;
    if(!client.InitIpcClient(argv[1] )) {
        std::cerr << client.GetLastErrMsg() <<"\n"; 
        exit(EXIT_FAILURE);
    }
    std::cout << "client started" << "\n";
    std::string user_msg  {""}; 
    while( client.IsConnected() ) {
        std::cin.clear();
        getline(std::cin, user_msg); 
        int msg_len = user_msg.length();
        if(msg_len>0) {
            if(! client.SendToServer(user_msg.c_str(),user_msg.length()) ) {
                std::cerr << client.GetLastErrMsg() <<"\n"; 
                exit(EXIT_FAILURE);
            }
        }
    }
    std::cout << "client exit...\n";
    exit(EXIT_SUCCESS);
}

