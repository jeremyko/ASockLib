#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include <csignal>
#include "asock/asock_udp_client.hpp"

// The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class Client : public asock::ASockUdpClient {
  public:
    Client(){
        this_instance_ = this;
    }
    static void SigIntHandler(int signo) {
        if (signo == SIGINT) {
            std::cout << "stop client\n";
            this_instance_->Disconnect();
            exit(EXIT_SUCCESS);
        } else {
            exit(EXIT_FAILURE);
        }
    }
  private:
    static Client* this_instance_ ;
    bool OnRecvedCompleteData(asock::Context* ,const char* const data_ptr, size_t len) override {
        //user specific : - your whole data has arrived.
        char packet[DEFAULT_PACKET_SIZE];
        memcpy(&packet, data_ptr, len );
        packet[len] = '\0';
        std::cout << "server response [" << packet << "]\n";
        return true;
    }
};
Client* Client::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
    std::signal(SIGINT,Client::SigIntHandler);
    Client client;
    // In case of UDP, you need to know the maximum receivable size in advance and allocate a buffer.
    if(!client.InitUdpClient("127.0.0.1", 9990, DEFAULT_PACKET_SIZE  ) ) {
        std::cerr << client.GetLastErrMsg() <<"\n";
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
    }//while
    std::cout << "client exit...\n";
    exit(EXIT_SUCCESS);
}

