#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include <csignal>
#include "ASock.hpp"

// NOTE: The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024

///////////////////////////////////////////////////////////////////////////////
class Client {
  public:
    bool InitTcpClient();
    bool SendToServer(const char* data, size_t len);
    bool IsConnected() { return client_.IsConnected();}
    std::string  GetLastErrMsg(){return  client_.GetLastErrMsg() ; }
    static void SigIntHandler(int signo);
  private:
    static Client* this_instance_ ;
    asock::ASock client_ ; //composite usage
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, size_t len); 
    void    OnDisconnectedFromServer() ; 
};
Client* Client::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool Client::InitTcpClient() {
    this_instance_ = this;
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    client_.SetCbOnRecvedCompletePacket(std::bind(
                                &Client::OnRecvedCompleteData, this, _1, _2, _3));
    client_.SetCbOnDisconnectedFromServer(std::bind(
                                &Client::OnDisconnectedFromServer, this));

    if(!client_.InitTcpClient("127.0.0.1", 9990 ) ) {
        std::cerr << client_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool Client:: OnRecvedCompleteData(asock::Context* , char* data_ptr, size_t len) {
    //user specific : your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr,len );
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
    }
    else {
        std::cerr << strerror(errno) << "/"<<signo<<"\n"; 
        exit(EXIT_FAILURE);
    }
}

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
    Client client;
    std::signal(SIGINT,Client::SigIntHandler);
    if(!client.InitTcpClient()) {
        exit(EXIT_FAILURE);
    }
    std::cout << "client started" << "\n";
    std::string user_msg  {""}; 
    while( client.IsConnected() ) {
        std::cin.clear();
        getline(std::cin, user_msg); 
        size_t msg_len = user_msg.length();
        if(msg_len>0) {
            if(! client.SendToServer(user_msg.c_str(),msg_len) ) {
                std::cerr << client.GetLastErrMsg() <<"\n"; 
                exit(EXIT_FAILURE);
            }
        }
    } //while
    std::cout << "client exit...\n";
    exit(EXIT_SUCCESS);
}

