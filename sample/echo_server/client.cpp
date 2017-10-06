
#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>

#include "ASock.hpp"
#include "msg_defines.h"

//----- for debug assert !!!! ---------
#include <cassert>
//#define NDEBUG
//----- for debug assert !!!! ---------

std::string gStrMyMsg  {""}; 
std::string gStrSentMsg{""}; 

///////////////////////////////////////////////////////////////////////////////
class EchoClient : public ASock
{
    public:
        //EchoClient();

    private:
        size_t  GetOnePacketLength(asocklib::Context* pContext); 
        bool    OnRecvOnePacketData(asocklib::Context* pContext, char* pOnePacket, int nPacketLen); 
        void    OnDisConnected() ; 
};

///////////////////////////////////////////////////////////////////////////////
size_t EchoClient::GetOnePacketLength(asocklib::Context* pContext)
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

    assert(nSupposedTotalLen<=pContext->recvBuffer_.GetCapacity());
    return nSupposedTotalLen ;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: OnRecvOnePacketData(asocklib::Context* pContext, char* pOnePacket, int nPacketLen) 
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
    
    std::cout <<   "\n* server response ["<< szMsg <<"]\n";
    assert( std::string(szMsg) == gStrSentMsg);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
void EchoClient::OnDisConnected() 
{
    std::cout << "* server disconnected ! \n";
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    EchoClient client;

    //connect timeout is 10 secs.
    //max message length is approximately 300 bytes...
    if(!client.InitTcpClient("127.0.0.1", 9990, 10, 300 ) )
    {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
        return -1;
    }

    while( client.IsConnected() )
    {
        std::cin.clear();
        getline(std::cin, gStrMyMsg); //block....
        gStrSentMsg = gStrMyMsg ;

        int nMsgLen = gStrMyMsg.length();

        if(nMsgLen>0)
        {
            ST_MY_HEADER stHeader;
            snprintf(stHeader.szMsgLen, sizeof(stHeader.szMsgLen), "%d", nMsgLen );

            if(! client.SendToServer( reinterpret_cast<char*>(&stHeader), sizeof(ST_MY_HEADER)) )
            {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
                return -1;
            }
            if(! client.SendToServer(gStrMyMsg.c_str(), gStrMyMsg.length()) )
            {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< client.GetLastErrMsg() <<"\n"; 
                return -1;
            }
        }
    }
    std::cout << "client exit...\n";
    return 0;
}

