#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include "ASock.hpp"
#include "../../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class UdpEchoClient 
{
  public:
    bool initialize_udp_client();
    bool SendToServer(const char* data, size_t len);
    bool IsConnected() { return udp_client_.IsConnected();}
    std::string  GetLastErrMsg(){return  udp_client_.GetLastErrMsg() ; }

  private:
    asock::ASock   udp_client_ ; //composite usage
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, size_t len); 
};

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoClient::initialize_udp_client()
{
    //register callbacks
    udp_client_.SetCbOnRecvedCompletePacket(std::bind(&UdpEchoClient::OnRecvedCompleteData, this,
                                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    //max message length is approximately 1024 bytes...
    if(!udp_client_.InitUdpClient("127.0.0.1", 9990, 1024 ) ) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< udp_client_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoClient:: OnRecvedCompleteData(asock::Context* context_ptr, 
                                          char* data_ptr, size_t len) 
{
    //user specific : your whole data has arrived.
    std::string response = data_ptr + CHAT_HEADER_SIZE;
    response.replace(len- CHAT_HEADER_SIZE, 1, 1, '\0');
    std::cout<<"server response  [" << response.c_str() << "]\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoClient:: SendToServer(const char* data, size_t len)
{
    return udp_client_.SendToServer(data, len);
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    UdpEchoClient client;
    client.initialize_udp_client();
    std::string user_msg  {""}; 
    std::cout << "client started" << "\n";
    while( client.IsConnected() ) {
        std::cin.clear();
        getline(std::cin, user_msg);
        size_t msg_len = user_msg.length();
        if(msg_len>0) {
            ST_MY_HEADER header;
            snprintf(header.msg_len, sizeof(header.msg_len), "%zu", msg_len );
            char* complete_packet_data = new  char [1024] ;
            memcpy(complete_packet_data, (char*)&header,  sizeof(ST_MY_HEADER));
            memcpy(complete_packet_data+sizeof(ST_MY_HEADER), user_msg.c_str(),user_msg.length() );
            size_t total_len = sizeof(ST_MY_HEADER)+  user_msg.length();
            if(! client.SendToServer(complete_packet_data ,total_len) ) {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! " << client.GetLastErrMsg() <<"\n"; 
                delete [] complete_packet_data;
                return 1;
            }            
            delete [] complete_packet_data;
        }
    } //while
    std::cout << "client exit...\n";
    return 0;
}

