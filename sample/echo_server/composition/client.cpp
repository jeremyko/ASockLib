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
class EchoClient 
{
    public:
        bool initialize_tcp_client();
        bool send_to_server(const char* data, int len);
        bool is_connected() { return tcp_client_.is_connected();}
        std::string  get_last_err_msg(){return  tcp_client_.get_last_err_msg() ; }

    private:
        ASock   tcp_client_ ; //composite usage
        size_t  on_calculate_data_len(asock::Context* context_ptr); 
        bool    on_recved_complete_data(asock::Context* context_ptr, char* data_ptr, int len); 
        void    on_disconnected_from_server() ; 
};

///////////////////////////////////////////////////////////////////////////////
bool EchoClient::initialize_tcp_client()
{
    //connect timeout is 10 secs, max message length is approximately 300 bytes...
    if(!tcp_client_.init_tcp_client("127.0.0.1", 9990, 10, 300 ) )
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< tcp_client_.get_last_err_msg() <<"\n"; 
        return -1;
    }

    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;

    tcp_client_.set_cb_on_calculate_packet_len(std::bind(
                       &EchoClient::on_calculate_data_len, this, _1));
    tcp_client_.set_cb_on_recved_complete_packet(std::bind(
                       &EchoClient::on_recved_complete_data, this, _1,_2,_3));
    tcp_client_.set_cb_on_disconnected_from_server(std::bind(
                       &EchoClient::on_disconnected_from_server, this));

    return true;
}

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
    //user specific : your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE]; 
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    
    std::cout <<   "\n* server response ["<< packet <<"]\n";
    assert( std::string(packet) == g_sent_msg);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: send_to_server(const char* data, int len)
{
    return tcp_client_.send_to_server(data, len);
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
    client.initialize_tcp_client();

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

            if(! client.send_to_server( reinterpret_cast<char*>(&stHeader), 
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

