#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"
#include "../../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
class EchoServer : public asock::ASock
{
  public:
    EchoServer(){this_instance_ = this; }
    static void sigint_handler(int signo);

  private:
    size_t  OnCalculateDataLen(asock::Context* context_ptr);
    bool    OnRecvedCompleteData(asock::Context* context_ptr, 
                                 char* data_ptr, size_t len ) ;
    void    OnClientConnected(asock::Context* context_ptr) ; 
    void    OnClientDisconnected(asock::Context* context_ptr) ; 
    static  EchoServer* this_instance_ ;
};

EchoServer* EchoServer::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
size_t EchoServer::OnCalculateDataLen(asock::Context* context_ptr)
{
    //---------------------------------------------------
    //user specific : 
    //calculate your complete packet length here using buffer data.
    //---------------------------------------------------
    if(context_ptr->recv_buffer.GetCumulatedLen() < (int)CHAT_HEADER_SIZE ) {
        return asock::MORE_TO_COME ; //more to come 
    }
    ST_MY_HEADER header ;
    context_ptr->recv_buffer.PeekData(CHAT_HEADER_SIZE, (char*)&header); 
    size_t supposed_total_len = std::atoi(header.msg_len) + CHAT_HEADER_SIZE;
    assert(supposed_total_len<=context_ptr->recv_buffer.GetCapacity());
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool    EchoServer::OnRecvedCompleteData(asock::Context* context_ptr, 
                                         char*  data_ptr, size_t len ) 
{
    //user specific : - your whole data has arrived.
    std::cout<<"recved [" << data_ptr + CHAT_HEADER_SIZE << "]\n";
    // this is echo server
    if(! SendData(context_ptr, data_ptr, len) ) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< GetLastErrMsg() <<"\n"; 
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
void EchoServer::sigint_handler(int signo)
{
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< strerror(errno) <<"\n"; 
    }
    std::cout << "Stop Server! \n";
    this_instance_->StopServer();
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    if(argc !=2) {
        std::cout << "usage : " << argv[0] << " ipc_socket_full_path \n\n";
        return 1;
    }
    std::signal(SIGINT,EchoServer::sigint_handler);
    //max client is 100000, 
    //max message length is approximately 1024 bytes...
    EchoServer echoserver; 
    if(!echoserver.InitIpcServer(argv[1])) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] error! "<< echoserver.GetLastErrMsg() <<"\n"; 
        exit(1);
    }std::cout << "server started" << "\n";
    while( echoserver.IsServerRunning() ) {
        sleep(1);
    }
    std::cout << "server exit...\n";
    return 0;
}

