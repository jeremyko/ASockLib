# ASockLib #

### What ###

a simple, easy to use c++ TCP server, client library using epoll, kqueue and [CumBuffer](https://github.com/jeremyko/CumBuffer).


### Usage ###

#### echo server ####

```{.cpp}
//see sample directory. this is an inheritance usage. 
//but you can also find an composition usage.

class EchoServer : public ASock
{
    private:
        size_t  on_calculate_data_len(asock::Context* context_ptr);
        bool    on_recved_complete_data(asock::Context* context_ptr, 
                                        char*  data_ptr, int len ) ;
};

size_t EchoServer::on_calculate_data_len(asock::Context* context_ptr)
{
    //calculate your complete packet length here using buffer data.
    return context_ptr->recvBuffer_.GetCumulatedLen() ; //just echo 
}

bool    EchoServer::on_recved_complete_data(asock::Context* context_ptr, 
                                            char*           data_ptr, 
                                            int             len ) 
{
    //user specific : - your whole data has arrived.
    send_data(context_ptr, data_ptr, len) ) //this is echo server
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< get_last_err_msg() <<"\n"; 
        return false;
    }
    return true;
}

int main(int argc, char* argv[])
{
    //max client is 100000, max message length is approximately 300 bytes...
    EchoServer echoserver; 
    echoserver.init_tcp_server("127.0.0.1", 9990, 100000, 300);

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

    return 0;
}

```

#### echo client ####

```{.cpp}

#include "ASock.hpp"
class EchoClient : public ASock
{
    private:
        size_t  on_calculate_data_len(asock::Context* context_ptr); 
        bool    on_recved_complete_data(asock::Context* context_ptr, 
                                        char*           data_ptr, 
                                        int             len); 
        void    on_disconnected_from_server() ; 
};

size_t EchoClient::on_calculate_data_len(asock::Context* context_ptr)
{
    //calculate your complete packet length here using buffer data.
    return context_ptr->recvBuffer_.GetCumulatedLen() ; //just echo for example
}

bool EchoClient:: on_recved_complete_data(asock::Context* context_ptr, 
                                          char*           data_ptr, 
                                          int             len) 
{
    //user specific : - your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE]; 
    memcpy(&packet, data_ptr, len);
    packet[lenE] = '\0';
    
    std::cout <<   "\n* server response ["<< packet <<"]\n";
    return true;
}

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

    std::string user_msg = "hello"; 
    while( client.is_connected() )
    {
        if(! client.send_to_server(user_msg.c_str(), user_msg.length()) )
        {
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "
                << client.get_last_err_msg() <<"\n"; 
            return -1;
        }
        sleep(1);
    }
    return 0;
}

```

#### compile ####

    cd project_folder
    
    mkdir build
    
    cd build/
    
    cmake -DCMAKE_BUILD_TYPE=RELEASE ../
    
    make
