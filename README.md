# ASockLib #

### What ###

a simple, easy to use c++ TCP server, client library using epoll, kqueue and [CumBuffer](https://github.com/jeremyko/CumBuffer).


### Usage ###

####echo server####
```{.cpp}
//see sample directory

#include "AServerSocketTCP.hpp"
class EchoServer : public AServerSocketTCP
{
    public:
    private:
        size_t  GetOnePacketLength(Context* pClientContext);
        bool    OnRecvOnePacketData(Context* pClientContext, char* pOnePacket, int nPacketLen ) ;        
};

size_t EchoServer::GetOnePacketLength(Context* pClientContext)
{
    //calculate your complete packet length here using buffer data.
    return pClientContext->recvBuffer_.GetCumulatedLen() ; //just echo for example
}

bool    EchoServer::OnRecvOnePacketData(Context* pClientContext, char* pOnePacket, int nPacketLen ) 
{
    //'pOnePacket' has length of 'nPacketLen' that you returned in 'GetOnePacketLength' function. 
    if(! Send(pClientContext, pOnePacket, nPacketLen) ) //just echo for example
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

int main(int argc, char* argv[])
{
    //max client is 100000, max message length is approximately 300 bytes...
    EchoServer echoserver; 
    echoserver.SetConnInfo("127.0.0.1", 9990, 100000, 300);
    if(!echoserver.RunServer())
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< echoserver.GetLastErrMsg() <<"\n"; 
        return -1;
    }
    while( echoserver.IsServerRunnig() )
    {
        sleep(1);
    }
    std::cout << "server exit...\n";
    return 0;
}

```

####echo client####

```{.cpp}

#include "AClientSocketTCP.hpp"
class EchoClient : public AClientSocketTCP
{
    public:
    private:
        size_t  GetOnePacketLength(Context* pContext); 
        bool    OnRecvOnePacketData(Context* pContext, char* pOnePacket, int nPacketLen); 
        void    OnDisConnected() ; //callback
};

size_t EchoClient::GetOnePacketLength(Context* pContext)
{
    //calculate your complete packet length here using buffer data.
    return pContext->recvBuffer_.GetCumulatedLen() ; //just echo for example
}

bool EchoClient:: OnRecvOnePacketData(Context* pContext, char* pOnePacket, int nPacketLen) 
{
    //'pOnePacket' has length of 'nPacketLen' that you returned in 'GetOnePacketLength' function. 
    char szMsg[asocklib::DEFAULT_PACKET_SIZE]; 
    memcpy(&szMsg, pOnePacket, nPacketLen);
    szMsg[nPacketLen] = '\0';
    std::cout <<   "\n* server response ["<< szMsg <<"]\n";
    return true;
}

int main(int argc, char* argv[])
{
    EchoClient client;
    //max message length is approximately 300 bytes...
    if(!client.SetBufferCapacity(300) || !client.Connect("127.0.0.1", 9990)) 
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
        return -1;
    }

    std::string strMsg = "hello" ;
    while( client.IsConnected() )
    {
        if(! client.SendToServer(strMsg.c_str(), strMsg.length()) )
        {
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
            return -1;
        }
        sleep(1);
    }
    return 0;
}

```
