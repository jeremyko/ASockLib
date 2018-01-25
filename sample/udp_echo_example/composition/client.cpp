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
        bool send_to_server(const char* data, int len);
        bool is_connected() { return udp_client_.is_connected();}
        std::string  get_last_err_msg(){return  udp_client_.get_last_err_msg() ; }

    private:
        ASock   udp_client_ ; //composite usage
        bool    on_recved_complete_data(Context* context_ptr, 
                                        char* data_ptr, int len); 
};

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoClient::initialize_udp_client()
{
    //register callbacks
    udp_client_.set_cb_on_recved_complete_packet(std::bind(
                &UdpEchoClient::on_recved_complete_data, this, 
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3));

    //max message length is approximately 1024 bytes...
    if(!udp_client_.init_udp_client("127.0.0.1", 9990, 1024 ) )
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< udp_client_.get_last_err_msg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoClient:: on_recved_complete_data(Context* context_ptr, 
                                             char* data_ptr, int len) 
{
    //user specific : your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE]; 
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    
    std::cout << "* server response ["<< packet <<"]\n";

    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool UdpEchoClient:: send_to_server(const char* data, int len)
{
    return udp_client_.send_to_server(data, len);
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    UdpEchoClient client;
    client.initialize_udp_client();

    std::string user_msg  {""}; 
    while( client.is_connected() )
    {
        std::cin.clear();
        getline(std::cin, user_msg);
        int msg_len = user_msg.length();

        if(msg_len>0)
        {
            ST_MY_HEADER header;
            snprintf(header.msg_len, sizeof(header.msg_len), "%d", msg_len );

            char* complete_packet_data = new  char [1024] ;
            memcpy(complete_packet_data, (char*)&header,  sizeof(ST_MY_HEADER));
            memcpy(complete_packet_data+sizeof(ST_MY_HEADER), user_msg.c_str(),user_msg.length() );

            int total_len = sizeof(ST_MY_HEADER)+  user_msg.length();
            if(! client.send_to_server(complete_packet_data ,total_len) )
            {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                          << client.get_last_err_msg() <<"\n"; 
                delete [] complete_packet_data;
                return 1;
            }
            delete [] complete_packet_data;
        }
    }
    std::cout << "client exit...\n";
    return 0;
}

