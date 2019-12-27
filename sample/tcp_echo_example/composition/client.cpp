#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include "ASock.hpp"
#include "../../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class EchoClient 
{
  public:
    bool initialize_tcp_client();
    bool SendToServer(const char* data, size_t len);
    bool IsConnected() { return tcp_client_.IsConnected();}
    std::string  GetLastErrMsg(){return  tcp_client_.GetLastErrMsg() ; }
  private:
    asock::ASock   tcp_client_ ; //composite usage
    size_t  OnCalculateDataLen(asock::Context* context_ptr); 
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, size_t len); 
    void    OnDisconnectedFromServer() ; 
};

///////////////////////////////////////////////////////////////////////////////
bool EchoClient::initialize_tcp_client()
{
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    tcp_client_.SetCbOnCalculatePacketLen(std::bind(
                       &EchoClient::OnCalculateDataLen, this, _1));
    tcp_client_.SetCbOnRecvedCompletePacket(std::bind(
                       &EchoClient::OnRecvedCompleteData, this, _1,_2,_3));
    tcp_client_.SetCbOnDisconnectedFromServer(std::bind(
                       &EchoClient::OnDisconnectedFromServer, this));
    //connect timeout is 10 secs, max message length is approximately 1024 bytes...
    if(!tcp_client_.InitTcpClient("127.0.0.1", 9990, 10, 1024 ) ) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< tcp_client_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
size_t EchoClient::OnCalculateDataLen(asock::Context* context_ptr)
{
    //user specific : calculate your complete packet length 
    if( context_ptr->GetBuffer()->GetCumulatedLen() < (int)CHAT_HEADER_SIZE ) {
        return asock::MORE_TO_COME ; //more to come 
    }
    ST_MY_HEADER header ;
    context_ptr->GetBuffer()->PeekData(CHAT_HEADER_SIZE, (char*)&header);  
    size_t supposed_total_len = std::atoi(header.msg_len) + CHAT_HEADER_SIZE;
    assert(supposed_total_len<=context_ptr->GetBuffer()->GetCapacity());
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: OnRecvedCompleteData(asock::Context* context_ptr, 
                                       char* data_ptr, size_t len) 
{
    //user specific : your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE]; 
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    std::cout << "* server response ["<< packet <<"]\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: SendToServer(const char* data, size_t len)
{
    return tcp_client_.SendToServer(data, len);
}

///////////////////////////////////////////////////////////////////////////////
void EchoClient::OnDisconnectedFromServer() 
{
    std::cout << "* server disconnected ! \n";
    exit(1);
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    EchoClient client;
    client.initialize_tcp_client();
    std::string user_msg  {""}; 
    while( client.IsConnected() ) {
        std::cin.clear();
        getline(std::cin, user_msg); 
        size_t msg_len = user_msg.length();
        if(msg_len>0) {
            ST_MY_HEADER header;
            snprintf(header.msg_len, sizeof(header.msg_len), "%zd", msg_len );
            //you don't need to send twice like this..
            //but your whole data length should be less than 1024 bytes 
            //as you invoke InitTcpClient with max. 1024 bytes.
            if(! client.SendToServer( reinterpret_cast<char*>(&header), 
                                      sizeof(ST_MY_HEADER)) ) {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                          << client.GetLastErrMsg() <<"\n"; 
                return 1;
            }
            if(! client.SendToServer(user_msg.c_str(), user_msg.length()) ) {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                          << client.GetLastErrMsg() <<"\n"; 
                return 1;
            }
        }
    } //while
    std::cout << "client exit...\n";
    return 0;
}

