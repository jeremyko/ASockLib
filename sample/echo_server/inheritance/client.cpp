#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>

#include "ASock.hpp"
#include "msg_defines.h"
//#define NDEBUG

std::string g_sent_msg{""}; 

///////////////////////////////////////////////////////////////////////////////
class EchoClient : public ASock
{
    private:
        size_t  on_calculate_data_len(asock::Context* context_ptr); 
        bool    on_recved_complete_data(asock::Context* context_ptr, 
                                        char*           data_ptr, 
                                        int             len); 
        void    on_disconnected_from_server() ; 
};

///////////////////////////////////////////////////////////////////////////////
size_t EchoClient::on_calculate_data_len(asock::Context* context_ptr)
{
    //user specific : calculate your complete packet length 
    if( context_ptr->recvBuffer_.GetCumulatedLen() < (int)CHAT_HEADER_SIZE )
    {
        return asock::MORE_TO_COME ; //more to come 
    }

    ST_MY_HEADER sHeader ;
    context_ptr->recvBuffer_.PeekData(CHAT_HEADER_SIZE, (char*)&sHeader);  
    size_t supposed_total_len = std::atoi(sHeader.szMsgLen) + CHAT_HEADER_SIZE;
    assert(supposed_total_len<=context_ptr->recvBuffer_.GetCapacity());
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: on_recved_complete_data(asock::Context* context_ptr, 
                                          char*           data_ptr, 
                                          int             len) 
{
    //user specific : - your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE]; 
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    
    std::cout <<   "\n* server response ["<< packet <<"]\n";
    assert( std::string(packet) == g_sent_msg);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void EchoClient::on_disconnected_from_server() 
{
    std::cout << "* server disconnected ! \n";
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    EchoClient client;

    //connect timeout is 10 secs.
    //max message length is approximately 300 bytes...
    if(!client.init_tcp_client("127.0.0.1", 9990, 10, 300 ) )
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< client.get_last_err_msg() <<"\n"; 
        return -1;
    }

    std::string user_msg  {""}; 
    while( client.is_connected() )
    {
        std::cin.clear();
        getline(std::cin, user_msg); 
        g_sent_msg = user_msg ;

        int nMsgLen = user_msg.length();

        if(nMsgLen>0)
        {
            ST_MY_HEADER stHeader;
            snprintf(stHeader.szMsgLen, sizeof(stHeader.szMsgLen), "%d", nMsgLen );

            if(! client.send_to_server(reinterpret_cast<char*>(&stHeader), 
                                       sizeof(ST_MY_HEADER)) )
            {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                          << client.get_last_err_msg() <<"\n"; 
                return -1;
            }
            if(! client.send_to_server(user_msg.c_str(), user_msg.length()) )
            {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                          << client.get_last_err_msg() <<"\n"; 
                return -1;
            }
        }
    }
    std::cout << "client exit...\n";
    return 0;
}

