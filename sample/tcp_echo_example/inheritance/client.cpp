#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include "ASock.hpp"

// NOTE: The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class EchoClient : public asock::ASock
{
  private:
    bool    OnRecvedCompleteData(asock::Context* context_ptr, 
                                 char* data_ptr, size_t len) override; 
    void    OnDisconnectedFromServer() override ; 
};


///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: OnRecvedCompleteData(asock::Context* , char* data_ptr, size_t len) {
    //user specific : - your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr ,len);
    packet[len] = '\0';
    std::cout << "server response [" << packet << "]\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void EchoClient::OnDisconnectedFromServer() {
    std::cout << "* server disconnected ! \n";
    exit(1);
}

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
    EchoClient client;
    if(!client.InitTcpClient("127.0.0.1", 9990  ) ) {
        std::cerr << client.GetLastErrMsg() <<"\n"; 
        return 1;
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
                return 1;
            }
        }
    } //while
    std::cout << "client exit...\n";
    return 0;
}

