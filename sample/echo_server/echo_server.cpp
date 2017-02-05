
#include <iostream>
#include <csignal>

#include "../../src/AServerSocketTCP.hpp"
#include "msg_defines.h"

//----- for debug assert !!!! ---------
#include <cassert>
//#define NDEBUG
//----- for debug assert !!!! ---------

///////////////////////////////////////////////////////////////////////////////
class EchoServer : public AServerSocketTCP
{
    public:
        EchoServer();
        void    SetSigAction();

    private:
        static  EchoServer* pThisInstance_ ;
        size_t  GetOnePacketLength(Context* pClientContext);
        bool    OnRecvOnePacketData(Context* pClientContext, char* pOnePacket, int nPacketLen ) ;
        void    OnClientConnected(Context* pClientContext) ; 
        void    OnClientDisConnected(Context* pClientContext) ; 
        static void SigInt(int signo);
};


EchoServer* EchoServer::pThisInstance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
EchoServer::EchoServer ()
{
    pThisInstance_ = this;
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::SetSigAction()
{
    std::signal(SIGINT,EchoServer::SigInt);
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::SigInt(int signo)
{
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0)
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< strerror(errno) <<"\n"; 
    }
    
    std::cout << "Stop Server! \n";
    pThisInstance_->StopServer();
}

///////////////////////////////////////////////////////////////////////////////
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
    assert(nSupposedTotalLen<=pClientContext->recvBuffer_.GetCapacity());

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
void EchoServer::OnClientConnected(Context* pClientContext) 
{
    std::cout << "client connected : socket fd ["<< pClientContext->socket_ <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::OnClientDisConnected(Context* pClientContext) 
{
    std::cout << "client disconnected : socket fd ["<< pClientContext->socket_ <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    //max client is 100000, 
    //max message length is approximately 300 bytes...
    EchoServer echoserver; 
    echoserver.SetConnInfo("127.0.0.1", 9990, 100000, 300);

    echoserver.SetSigAction();

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

