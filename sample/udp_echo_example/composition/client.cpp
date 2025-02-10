#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include "ASock.hpp"

// NOTE: The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class UdpEchoClient 
{
  public:
    bool IntUdpClient();
    bool SendToServer(const char* data, size_t len);
    bool IsConnected() { return udp_client_.IsConnected();}
    std::string  GetLastErrMsg(){return  udp_client_.GetLastErrMsg() ; }

  private:
    asock::ASock   udp_client_ ; //composite usage
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, size_t len); 
};

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoClient::IntUdpClient() {
    //register callbacks
    udp_client_.SetCbOnRecvedCompletePacket(std::bind(&UdpEchoClient::OnRecvedCompleteData, this,
                                            std::placeholders::_1, 
                                            std::placeholders::_2, 
                                            std::placeholders::_3));
    // In case of UDP, you need to know the maximum receivable size in advance and allocate a buffer.
    if(!udp_client_.InitUdpClient("127.0.0.1", 9990, DEFAULT_PACKET_SIZE  ) ) {
        std::cerr << udp_client_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoClient:: OnRecvedCompleteData(asock::Context* , char* data_ptr, size_t len) {
    //user specific : your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr, len );
    packet[len] = '\0';
    std::cout << "server response [" << packet << "]\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoClient:: SendToServer(const char* data, size_t len) {
    return udp_client_.SendToServer(data, len);
}

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
    UdpEchoClient client;
    client.IntUdpClient();
    std::string user_msg  {""}; 
    std::cout << "client started" << "\n";
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

