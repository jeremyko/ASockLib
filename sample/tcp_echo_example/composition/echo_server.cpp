#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"
#include "../../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class EchoServer 
{
  public:
    EchoServer(){this_instance_ = this; }
#if defined __APPLE__ || defined __linux__ 
    static void sigint_handler(int signo);
#endif
    bool    initialize_tcp_server();
    bool    IsServerRunning(){return tcp_server_.IsServerRunning();};
    std::string  GetLastErrMsg(){return  tcp_server_.GetLastErrMsg() ; }

  private:
    asock::ASock tcp_server_ ; //composite usage
    static  EchoServer* this_instance_ ;
  private:
    size_t  OnCalculateDataLen(asock::Context* context_ptr);
    bool    OnRecvedCompleteData(asock::Context* context_ptr, 
                                 char* data_ptr, size_t len ) ;
    void    OnClientConnected(asock::Context* context_ptr) ; 
    void    OnClientDisconnected(asock::Context* context_ptr) ; 
};

EchoServer* EchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool EchoServer::initialize_tcp_server()
{
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    tcp_server_.SetCbOnCalculatePacketLen  (std::bind(
                       &EchoServer::OnCalculateDataLen, this, _1));
    tcp_server_.SetCbOnRecvedCompletePacket(std::bind(
                       &EchoServer::OnRecvedCompleteData, this, _1,_2,_3));
    tcp_server_.SetCbOnClientConnected      (std::bind(
                       &EchoServer::OnClientConnected, this, _1));
    tcp_server_.SetCbOnClientDisconnected   (std::bind(
                       &EchoServer::OnClientDisconnected, this, _1));
    //max client is 10000, max message length is approximately 1024 bytes...
    if(!tcp_server_.InitTcpServer("127.0.0.1", 9990, 1024 /*,default=10000*/)) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< tcp_server_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}
///////////////////////////////////////////////////////////////////////////////
size_t EchoServer::OnCalculateDataLen(asock::Context* context_ptr)
{
    //user specific : calculate your complete packet length here using buffer data.
    if(context_ptr->GetBuffer()->GetCumulatedLen() < (int)CHAT_HEADER_SIZE ) {
        return asock::MORE_TO_COME ; //more to come 
    }
    ST_MY_HEADER header ;
    context_ptr->GetBuffer()->PeekData(CHAT_HEADER_SIZE, (char*)&header); 
    size_t supposed_total_len = std::atoi(header.msg_len) + CHAT_HEADER_SIZE;
    assert(supposed_total_len<=context_ptr->GetBuffer()->GetCapacity());
    if (supposed_total_len == 6) {
        std::cerr << "error !\n"; //XXX TODO just debug
    }
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool    EchoServer::OnRecvedCompleteData(asock::Context* context_ptr, 
                                         char* data_ptr, size_t len ) 
{
    //user specific : your whole data has arrived.
    // this is echo server
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    std::cout << "recved [" << packet << "]\n"; 
    
    if(! tcp_server_.SendData(context_ptr, data_ptr, len) ) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::OnClientConnected(asock::Context* context_ptr) 
{
    std::cout << "client connected : socket fd ["<< context_ptr->socket <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
void EchoServer::OnClientDisconnected(asock::Context* context_ptr) 
{
    std::cout << "client disconnected : socket fd ["<< context_ptr->socket <<"]\n";
}

///////////////////////////////////////////////////////////////////////////////
#if defined __APPLE__ || defined __linux__ 
void EchoServer::sigint_handler(int signo)
{
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< strerror(errno) <<"\n"; 
    }
    std::cout << "Stop Server! \n";
    this_instance_->tcp_server_.StopServer();
}
#endif

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
#if defined __APPLE__ || defined __linux__ 
    std::signal(SIGINT,EchoServer::sigint_handler);
#endif
    EchoServer echoserver; 
    echoserver.initialize_tcp_server();

    while( echoserver.IsServerRunning() ) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    return 0;
}

