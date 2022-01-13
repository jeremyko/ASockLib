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
    bool initialize_ipc_client(const char* ipc_sock_path);
    bool SendToServer(const char* data, size_t len);
    bool IsConnected() { return ipc_client_.IsConnected();}
    std::string  GetLastErrMsg(){return  ipc_client_.GetLastErrMsg() ; }
  private:
    asock::ASock   ipc_client_ ; //composite usage
    size_t  OnCalculateDataLen(asock::Context* context_ptr); 
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, size_t len); 
    void    OnDisconnectedFromServer() ; 
};

///////////////////////////////////////////////////////////////////////////////
bool EchoClient::initialize_ipc_client(const char* ipc_sock_path)
{
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    ipc_client_.SetCbOnCalculatePacketLen(std::bind( &EchoClient::OnCalculateDataLen, this, _1));
    ipc_client_.SetCbOnRecvedCompletePacket(std::bind( &EchoClient::OnRecvedCompleteData, this, _1,_2,_3));
    ipc_client_.SetCbOnDisconnectedFromServer(std::bind( &EchoClient::OnDisconnectedFromServer, this));
    //connect timeout is 10 secs, max message length is approximately 1024 bytes...
    if(!ipc_client_.InitIpcClient(ipc_sock_path) ) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< ipc_client_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
size_t EchoClient::OnCalculateDataLen(asock::Context* context_ptr)
{
    //user specific : calculate your complete packet length 
    if( context_ptr->recv_buffer.GetCumulatedLen() < (int)CHAT_HEADER_SIZE ) {
        return asock::MORE_TO_COME ; //more to come 
    }
    ST_MY_HEADER header ;
    context_ptr->recv_buffer.PeekData(CHAT_HEADER_SIZE, (char*)&header);  
    size_t supposed_total_len = std::atoi(header.msg_len) + CHAT_HEADER_SIZE;
    assert(supposed_total_len<=context_ptr->recv_buffer.GetCapacity());
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: OnRecvedCompleteData(asock::Context* context_ptr, 
                                       char* data_ptr, size_t len) 
{
    //user specific : your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr + CHAT_HEADER_SIZE, len - CHAT_HEADER_SIZE);
    packet[len - CHAT_HEADER_SIZE] = '\0';
    std::cout << "server response [" << packet << "]\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: SendToServer(const char* data, size_t len)
{
    return ipc_client_.SendToServer(data, len);
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
    if(argc !=2) {
        std::cout << "usage : " << argv[0] << " ipc_socket_full_path \n\n";
        return 1;
    }
    EchoClient client;
    client.initialize_ipc_client(argv[1]);
    std::cout << "client started" << "\n";
    std::string user_msg  {""}; 
    while( client.IsConnected() ) {
        std::cin.clear();
        getline(std::cin, user_msg); 
        int msg_len = user_msg.length();
        if(msg_len>0) {
            ST_MY_HEADER header;
            snprintf(header.msg_len, sizeof(header.msg_len), "%d", msg_len );
            char* complete_packet_data = new  char [1024] ;
            memcpy(complete_packet_data, (char*)&header,  sizeof(ST_MY_HEADER));
            memcpy(complete_packet_data+sizeof(ST_MY_HEADER), user_msg.c_str(),user_msg.length() );
            if(! client.SendToServer(complete_packet_data ,sizeof(ST_MY_HEADER)+  user_msg.length()) ) {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! " << client.GetLastErrMsg() <<"\n"; 
                delete [] complete_packet_data;
                return 1;
            }
            delete [] complete_packet_data;
        }
    }
    std::cout << "client exit...\n";
    return 0;
}

