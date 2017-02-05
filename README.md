# ASockLib #

### What ###

a simple, easy to use c++ TCP server, client library using epoll (for linux) and [CumBuffer](https://github.com/jeremyko/CumBuffer).


### Usage ###

####echo server####
```{.cpp}
//see sample directory

#include "AServerSocketTCP.hpp"

typedef struct _ST_MY_CHAT_HEADER_
{
    char szMsgLen[6];

} ST_MY_HEADER ;
#define CHAT_HEADER_SIZE sizeof(ST_MY_HEADER)

class EchoServer : public AServerSocketTCP
{
    public:
        void    SetSigAction();

    private:
        static  EchoServer* pThisInstance_ ;
        size_t  GetOnePacketLength(Context* pClientContext);
        bool    OnRecvOnePacketData(Context* pClientContext, char* pOnePacket, int nPacketLen ) ;
        void    OnClientConnected(Context* pClientContext) ; 
        void    OnClientDisConnected(Context* pClientContext) ; 
        static void SigInt(int signo);
};

size_t EchoServer::GetOnePacketLength(Context* pClientContext)
{
    //---------------------------------------------------
    //user specific : 
    //calculate your complete packet length here using buffer data.
    //---------------------------------------------------
    if(pClientContext->recvBuffer_.GetCumulatedLen() < (int)CHAT_HEADER_SIZE )
    {
        return asocklib::MORE_TO_COME ; //more to come 
    }

    ST_MY_HEADER sHeader ;
    pClientContext->recvBuffer_.PeekData(CHAT_HEADER_SIZE, (char*)&sHeader); 

    size_t nSupposedTotalLen = std::atoi(sHeader.szMsgLen) + CHAT_HEADER_SIZE;
    return nSupposedTotalLen ;
}

///////////////////////////////////////////////////////////////////////////////
bool    EchoServer::OnRecvOnePacketData(Context* pClientContext, char* pOnePacket, int nPacketLen ) 
{
    //---------------------------------------------------
    //user specific : 
    //- your whole data has arrived.
    //- 'pOnePacket' has length of 'nPacketLen' that you returned 
    //  in 'GetOnePacketLength' function. 
    //---------------------------------------------------
    char szMsg[asocklib::DEFAULT_PACKET_SIZE];
    memcpy(&szMsg, pOnePacket+CHAT_HEADER_SIZE, nPacketLen-CHAT_HEADER_SIZE);
    szMsg[nPacketLen-CHAT_HEADER_SIZE] = '\0';
    
    // this is echo server
    if(! Send(pClientContext, pOnePacket, nPacketLen) )
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    //max client is 100000, 
    //max message length is approximately 300 bytes...
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

///////////////////////////////////////////////////////////////////////////////
class EchoClient : public AClientSocketTCP
{
    public:

    private:
        size_t  GetOnePacketLength(Context* pContext); 
        bool    OnRecvOnePacketData(Context* pContext, char* pOnePacket, int nPacketLen); 
        void    OnDisConnected() ; 
};

///////////////////////////////////////////////////////////////////////////////
size_t EchoClient::GetOnePacketLength(Context* pContext)
{
    //---------------------------------------------------
    //user specific : 
    //calculate your complete packet length here using buffer data.
    //---------------------------------------------------
    if( pContext->recvBuffer_.GetCumulatedLen() < (int)CHAT_HEADER_SIZE )
    {
        return asocklib::MORE_TO_COME ; //more to come 
    }

    ST_MY_HEADER sHeader ;
    pContext->recvBuffer_.PeekData(CHAT_HEADER_SIZE, (char*)&sHeader);  

    size_t nSupposedTotalLen = std::atoi(sHeader.szMsgLen) + CHAT_HEADER_SIZE;
    return nSupposedTotalLen ;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: OnRecvOnePacketData(Context* pContext, char* pOnePacket, int nPacketLen) 
{
    //---------------------------------------------------
    //user specific : 
    //- your whole data has arrived.
    //- 'pOnePacket' has length of 'nPacketLen' that you returned 
    //  in 'GetOnePacketLength' function. 
    //---------------------------------------------------
    
    char szMsg[asocklib::DEFAULT_PACKET_SIZE]; //TODO cumbuffer size ?
    memcpy(&szMsg, pOnePacket+CHAT_HEADER_SIZE, nPacketLen-CHAT_HEADER_SIZE);
    szMsg[nPacketLen-CHAT_HEADER_SIZE] = '\0';
    
    std::cout <<   "\n* server response ["<< szMsg <<"]\n";

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void EchoClient::OnDisConnected() 
{
    //---------------------------------------------------
    //callback 
    //---------------------------------------------------
    std::cout << "* server disconnected ! \n";
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    EchoClient client;
    if(!client.SetBufferCapacity(300)) //max message length is approximately 300 bytes...
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
        return -1;
    }

    if(!client.Connect("127.0.0.1", 9990) )
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
        return -1;
    }

    std::string strMsg;
    while( client.IsConnected() )
    {
        cin.clear();
        getline(cin, strMsg); //block....
        int nMsgLen = strMsg.length();

        if(nMsgLen>0)
        {
            ST_MY_HEADER stHeader;
            snprintf(stHeader.szMsgLen, sizeof(stHeader.szMsgLen), "%d", nMsgLen );

            if(! client.SendToServer( reinterpret_cast<char*>(&stHeader), sizeof(ST_MY_HEADER)) )
            {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
                return -1;
            }
            if(! client.SendToServer(strMsg.c_str(), strMsg.length()) )
            {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
                return -1;
            }
        }
    }
    std::cout << "client exit...\n";
    return 0;
}

```
