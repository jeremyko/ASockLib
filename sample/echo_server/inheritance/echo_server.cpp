#include <iostream>
#include <cassert>

#include "ASock.hpp"
#include "msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class EchoServer : public ASock
{
    public:

    private:
        size_t  on_calculate_data_len(asock::Context* context_ptr);
        bool    on_recved_complete_data(asock::Context* context_ptr, 
                                        char*           data_ptr, 
                                        int             len ) ;
        void    on_client_connected(asock::Context* context_ptr) ; 
        void    on_client_disconnected(asock::Context* context_ptr) ; 
};

///////////////////////////////////////////////////////////////////////////////
size_t EchoServer::on_calculate_data_len(asock::Context* context_ptr)
{
    //---------------------------------------------------
    //user specific : 
    //calculate your complete packet length here using buffer data.
    //---------------------------------------------------
    if(context_ptr->recv_buffer_.GetCumulatedLen() < (int)CHAT_HEADER_SIZE )
    {
        return asock::MORE_TO_COME ; //more to come 
    }

    ST_MY_HEADER header ;
    context_ptr->recv_buffer_.PeekData(CHAT_HEADER_SIZE, (char*)&header); 

    size_t supposed_total_len = std::atoi(header.msg_len) + CHAT_HEADER_SIZE;
    assert(supposed_total_len<=context_ptr->recv_buffer_.GetCapacity());
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool    EchoServer::on_recved_complete_data(asock::Context* context_ptr, 
                                            char*           data_ptr, 
                                            int             len ) 
{
    //user specific : - your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    
    // this is echo server
    if(! send_data(context_ptr, data_ptr, len) )
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< get_last_err_msg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::on_client_connected(asock::Context* context_ptr) 
{
    std::cout << "client connected : socket fd ["<< context_ptr->socket_ <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::on_client_disconnected(asock::Context* context_ptr) 
{
    std::cout << "client disconnected : socket fd ["<< context_ptr->socket_ <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    //max client is 100000, 
    //max message length is approximately 1024 bytes...
    EchoServer echoserver; 
    echoserver.init_tcp_server("127.0.0.1", 9990, 100000, 1024);

    if(!echoserver.run_server())
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< echoserver.get_last_err_msg() <<"\n"; 
        return -1;
    }

    while( echoserver.is_server_running() )
    {
        sleep(1);
    }

    std::cout << "server exit...\n";
    return 0;
}

