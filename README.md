# ASockLib #

### What ###

a simple, easy to use cross-platform c++ networking library.

linux, os x : tcp, udp, domain socket using epoll and kqueue.

windows : currently tcp supports using iocp(server) and wsapoll(client).

dependency : [CumBuffer](https://github.com/jeremyko/CumBuffer).


### Usage ###

#### tcp echo server ####

```cpp
//see sample directory. this is an tcp inheritance usage. 
//you can find composition usage and udp, domain socket example too.

class EchoServer : public ASock
{
  private:
    size_t  OnCalculateDataLen(asock::Context* context_ptr);
    bool    OnRecvedCompleteData(asock::Context* context_ptr, 
                                 char*  data_ptr, size_t len ) ;
};

size_t EchoServer::OnCalculateDataLen(asock::Context* context_ptr)
{
    //calculate your complete packet length here using buffer data.
    return context_ptr->recvBuffer_.GetCumulatedLen() ; //just echo 
}

bool   EchoServer::OnRecvedCompleteData(asock::Context* context_ptr, 
                                         char*           data_ptr, 
                                         size_t          len ) 
{
    //user specific : - your whole data has arrived.
    SendData(context_ptr, data_ptr, len) ){ //this is echo server
        ELOG("error! "<< GetLastErrMsg() ); 
        return false;
    }
    return true;
}

int main(int argc, char* argv[])
{
    //max client is 100000, max message length is approximately 300 bytes...
    EchoServer echoserver; 
    if(!echoserver.InitTcpServer("127.0.0.1", 9990, 100000, 300)) {
        ELOG"error! "<< echoserver.GetLastErrMsg()); 
        return -1;
    }
    while( echoserver.IsServerRunning() ) {
        sleep(1);
    }
    return 0;
}

```

#### tcp echo client ####

```cpp

#include "ASock.hpp"
class EchoClient : public ASock
{
  private:
    size_t  OnCalculateDataLen(asock::Context* context_ptr); 
    bool    OnRecvedCompleteData(asock::Context* context_ptr, 
                                 char*           data_ptr, 
                                 size_t          len); 
    void    on_disconnected_from_server() ; 
};

size_t EchoClient::OnCalculateDataLen(asock::Context* context_ptr)
{
    //calculate your complete packet length here using buffer data.
    return context_ptr->recvBuffer_.GetCumulatedLen() ; //just echo for example
}

bool EchoClient:: OnRecvedCompleteData(asock::Context* context_ptr, 
                                       char*           data_ptr, 
                                       size_t          len) 
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
    if(!client.InitTcpClient("127.0.0.1", 9990, 10, 300 ) )
    {
        ELOG("error! "<< client.GetLastErrMsg()); 
        return -1;
    }
    std::string user_msg = "hello"; 
    while( client.is_connected() ) {
        if(! client.SendToServer(user_msg.c_str(), user_msg.length()) ) {
            ELOG("error! " << client.GetLastErrMsg()); 
            return -1;
        }
        sleep(1);
    }
    return 0;
}

```

#### compile ####

    git clone https://github.com/jeremyko/ASockLib.git
    cd ASockLib
    git submodule init
    git submodule update
    mkdir build; cd build;  cmake ..
    make  or msbuild(visual studio)


