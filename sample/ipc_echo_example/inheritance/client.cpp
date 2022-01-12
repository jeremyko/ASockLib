#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include "ASock.hpp"
#include "../../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class EchoClient : public asock::ASock
{
  private:
    size_t  OnCalculateDataLen(asock::Context* context_ptr); 
    bool    OnRecvedCompleteData(asock::Context* context_ptr, 
                                 char* data_ptr, size_t len); 
    void    OnDisconnectedFromServer() ; 
};

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
    //user specific : - your whole data has arrived.
    std::string response = data_ptr + CHAT_HEADER_SIZE;
    response.replace(len- CHAT_HEADER_SIZE, 1, 1, '\0');
    std::cout<<"server response  [" << response.c_str() << "]\n";
    return true;
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
    //connect timeout is 10 secs.
    //max message length is approximately 1024 bytes...
    if(!client.InitIpcClient(argv[1], 10, 1024)) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
        return 1;
    }
    std::cout << "client started" << "\n";
    std::string user_msg  {""}; 
    while( client.IsConnected() ) {
        std::cin.clear();
        getline(std::cin, user_msg); 
        int msg_len = user_msg.length();
        if(msg_len>0) {
            ST_MY_HEADER header;
            snprintf(header.msg_len, sizeof(header.msg_len), "%d", msg_len );
            //you don't need to send twice like this..
            if(! client.SendToServer(reinterpret_cast<char*>(&header), 
                                       sizeof(ST_MY_HEADER)) ) {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! " << client.GetLastErrMsg() <<"\n"; 
                return 1;
            }
            if(! client.SendToServer(user_msg.c_str(), msg_len) ) {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! " << client.GetLastErrMsg() <<"\n"; 
                return 1;
            }
        }
    }//while
    std::cout << "client exit...\n";
    return 0;
}

