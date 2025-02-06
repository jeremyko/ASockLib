#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include "ASock.hpp"

#define DEFAULT_PACKET_SIZE 1024

///////////////////////////////////////////////////////////////////////////////
class EchoClient 
{
  public:
    bool InitTcpClient();
    bool SendToServer(const char* data, size_t len);
    bool IsConnected() { return tcp_client_.IsConnected();}
    std::string  GetLastErrMsg(){return  tcp_client_.GetLastErrMsg() ; }
  private:
    asock::ASock   tcp_client_ ; //composite usage
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, size_t len); 
    void    OnDisconnectedFromServer() ; 
};

///////////////////////////////////////////////////////////////////////////////
bool EchoClient::InitTcpClient() {
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    tcp_client_.SetCbOnRecvedCompletePacket(std::bind(
                                &EchoClient::OnRecvedCompleteData, this, _1, _2, _3));
    tcp_client_.SetCbOnDisconnectedFromServer(std::bind(
                                &EchoClient::OnDisconnectedFromServer, this));

    if(!tcp_client_.InitTcpClient("127.0.0.1", 9990 ) ) {
        std::cerr << tcp_client_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: OnRecvedCompleteData(asock::Context* context_ptr, 
                                       char* data_ptr, size_t len) {
    //user specific : your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr,len );
    packet[len] = '\0';
    std::cout << "server response [" << packet << "]\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: SendToServer(const char* data, size_t len) {
    return tcp_client_.SendToServer(data, len);
}

///////////////////////////////////////////////////////////////////////////////
void EchoClient::OnDisconnectedFromServer() {
    std::cout << "* server disconnected ! \n";
    exit(1);
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
    EchoClient client;
    client.InitTcpClient(); 
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

