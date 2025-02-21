#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include <csignal>
#include "asock/asock_ipc_client.hpp"

// NOTE: Not implemented on Windows.
// The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class Client {
  public:
    bool InitIpcClient(const char* ipc_sock_path);
    bool SendToServer(const char* data, size_t len);
    bool IsConnected() { 
        return client_.IsConnected();
    }
    std::string GetLastErrMsg(){
        return  client_.GetLastErrMsg();
    }
    static void SigIntHandler(int signo);
  private:
    asock::ASockIpcClient client_ ; //composite usage
    static Client* this_instance_ ;
    bool OnRecvedCompleteData(asock::Context* context_ptr, const char* const data_ptr, size_t len);
    void OnDisconnectedFromServer() ;
};

Client* Client::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool Client::InitIpcClient(const char* ipc_sock_path) {
    this_instance_ = this;
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    client_.SetCbOnRecvedCompletePacket(std::bind( 
                            &Client::OnRecvedCompleteData, this, _1,_2,_3));
    client_.SetCbOnDisconnectedFromServer(std::bind( 
                            &Client::OnDisconnectedFromServer, this));
    if(!client_.InitIpcClient(ipc_sock_path) ) {
        std::cerr << client_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool Client:: OnRecvedCompleteData(asock::Context* , const char* const data_ptr, size_t len) {
    //user specific : your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr, len );
    packet[len] = '\0';
    std::cout << "server response [" << packet << "]\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool Client:: SendToServer(const char* data, size_t len) {
    return client_.SendToServer(data, len);
}

///////////////////////////////////////////////////////////////////////////////
void Client::OnDisconnectedFromServer() {
    std::cout << "server disconnected, terminate client\n";
    client_.Disconnect();
}

///////////////////////////////////////////////////////////////////////////////
void Client::SigIntHandler(int signo) {
    if (signo == SIGINT) {
        std::cout << "stop client\n";
        this_instance_->client_.Disconnect();
        exit(EXIT_SUCCESS);
    } else {
        std::cerr << strerror(errno) << "/"<<signo<<"\n"; 
        exit(EXIT_FAILURE);
    }
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
    if(argc !=2) {
        std::cerr << "usage : " << argv[0] << " ipc_socket_full_path \n\n";
        exit(EXIT_FAILURE);
    }
    std::signal(SIGINT,Client::SigIntHandler);
    Client client;
    if(!client.InitIpcClient(argv[1])) {
        exit(EXIT_FAILURE);
    }
    std::cout << "client started" << "\n";
    std::string user_msg  {""}; 
    while( client.IsConnected() ) {
        std::cin.clear();
        getline(std::cin, user_msg); 
        int msg_len = (int)user_msg.length();
        if(msg_len>0) {
            if(! client.SendToServer(user_msg.c_str(),msg_len) ) {
                std::cerr << client.GetLastErrMsg() <<"\n"; 
                exit(EXIT_FAILURE);
            }
        }
    }
    std::cout << "client exit...\n";
    exit(EXIT_SUCCESS);
}

