#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"
#include "../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
//Send To Each Other Server
///////////////////////////////////////////////////////////////////////////////
class STEO_Server 
{
  public:
    STEO_Server(){/*this_instance_ = this;*/ }
    bool    InitializeTcpServer();
    bool    IsServerRunning(){return tcp_server_.IsServerRunning();};
#if defined __APPLE__ || defined __linux__ 
    static void SigIntHandler(int signo);
#endif
    std::string  GetLastErrMsg(){return  tcp_server_.GetLastErrMsg() ; }
    asock::ASock tcp_server_ ; //composite usage
  private:
    size_t  OnCalculateDataLen(asock::Context* ctx_ptr);
    bool    OnRecvedCompleteData(asock::Context* ctx_ptr, 
                                 char* data_ptr, size_t len ) ;
    void    OnClientConnected(asock::Context* ctx_ptr) ; 
    void    OnClientDisconnected(asock::Context* ctx_ptr) ; 
    void    SendThread(asock::Context* ctx_ptr) ;
};

static STEO_Server* this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool STEO_Server::InitializeTcpServer()
{
    this_instance_ = this; 
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    tcp_server_.SetCbOnCalculatePacketLen  (std::bind(
                       &STEO_Server::OnCalculateDataLen, this, _1));
    tcp_server_.SetCbOnRecvedCompletePacket(std::bind(
                       &STEO_Server::OnRecvedCompleteData, this, _1,_2,_3));
    tcp_server_.SetCbOnClientConnected      (std::bind(
                       &STEO_Server::OnClientConnected, this, _1));
    tcp_server_.SetCbOnClientDisconnected   (std::bind(
                       &STEO_Server::OnClientDisconnected, this, _1));
    //max client is 10000, max message length is approximately 1024 bytes...
    if(!tcp_server_.InitTcpServer("127.0.0.1", 9990, 1024 /*,default=10000*/)) {
        std::cerr <<"["<< __func__ <<"-"<<__LINE__ 
                  <<"] error! "<< tcp_server_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void STEO_Server::SendThread(asock::Context* ctx_ptr) 
{
    LOG("Send Thread starts.......");
    size_t cnt = 0;
    while(ctx_ptr->is_connected) {
        std::string data = "server sending this....";
        data += std::to_string(cnt);
        ST_MY_HEADER header;
        snprintf(header.msg_len, sizeof(header.msg_len), "%zu", data.length());
        //---------------------------------------- send one buffer
        /*
        char send_msg[256];
        memcpy(&send_msg, &header, sizeof(header));
        memcpy(send_msg+sizeof(ST_MY_HEADER), data.c_str(), data.length());
        //DBG_LOG( "send msg ["<< send_msg <<"], len=" 
        //         << sizeof(ST_MY_HEADER) + data.length());
        if(! tcp_server_.SendData(  ctx_ptr, send_msg, 
                                    sizeof(ST_MY_HEADER)+data.length())) {
            //DBG_ELOG( "error! "<< tcp_server_.GetLastErrMsg() ); 
            return ;
        }
        */
        //---------------------------------------- send 2 times
        if(! tcp_server_.SendData(ctx_ptr, reinterpret_cast<char*>(&header), 
                    sizeof(ST_MY_HEADER)) ) {
            std::cerr <<"error! "<< tcp_server_.GetLastErrMsg() <<"\n";
            return ;
        }
        if(! tcp_server_.SendData(ctx_ptr, data.c_str(), data.length()) ) {
            std::cerr <<  "error! "<< tcp_server_.GetLastErrMsg() << "\n";
            return ;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        //std::this_thread::sleep_for(std::chrono::milliseconds(30));
        cnt++;
    }
}

///////////////////////////////////////////////////////////////////////////////
size_t STEO_Server::OnCalculateDataLen(asock::Context* ctx_ptr)
{
    //user specific : calculate your complete packet length here using buffer data.
    if(ctx_ptr->GetBuffer()->GetCumulatedLen() < (int)CHAT_HEADER_SIZE ) {
        return asock::MORE_TO_COME ; //more to come 
    }
    ST_MY_HEADER header ;
    ctx_ptr->GetBuffer()->PeekData(CHAT_HEADER_SIZE, (char*)&header); 
    int supposed_total_len = std::atoi(header.msg_len) + CHAT_HEADER_SIZE;
    assert(supposed_total_len<=ctx_ptr->GetBuffer()->GetCapacity());
    //std::cout << "calculated len=" << supposed_total_len << "\n";
    //    <<", capacity="<< ctx_ptr->GetBuffer()->GetCapacity() <<"\n"; 
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool STEO_Server::OnRecvedCompleteData(asock::Context* ctx_ptr, 
                                         char* data_ptr, size_t len ) 
{
    //user specific : your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    std::cout<<"recved [" << packet << "]\n"; 
    //---------------------------------------
    //this is echo server
    std::string data = "server echo:";
    data += std::string(packet);
    ST_MY_HEADER header;
    snprintf(header.msg_len, sizeof(header.msg_len), "%zu", data.length());
    char send_msg[256];
    memcpy(&send_msg, &header, sizeof(header));
    memcpy(send_msg + sizeof(ST_MY_HEADER), data.c_str(), data.length());
    if (!tcp_server_.SendData(  ctx_ptr, send_msg, 
                                sizeof(ST_MY_HEADER) + data.length())) {
        std::cerr <<  "error! "<< tcp_server_.GetLastErrMsg() ; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void STEO_Server::OnClientConnected(asock::Context* ctx_ptr) 
{
    std::cout <<"client connected : socket fd ["<< ctx_ptr->socket <<"]";
    //spawn new thread (server, client both sending each other)
    std::thread send_thread (&STEO_Server::SendThread,this, ctx_ptr);
    send_thread.detach();
}

///////////////////////////////////////////////////////////////////////////////
void STEO_Server::OnClientDisconnected(asock::Context* ctx_ptr) 
{
    std::cout << "client disconnected \n";
}

///////////////////////////////////////////////////////////////////////////////
#if defined __APPLE__ || defined __linux__ 
void STEO_Server::SigIntHandler(int signo)
{
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr << LOG_WHERE <<" error! "<< strerror(errno) <<"\n"; 
    }
    std::cout << "Stop Server! \n";
    this_instance_->tcp_server_.StopServer();
}
#else
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType) {
        // Handle the CTRL-C signal. 
    case CTRL_C_EVENT:
        LOG("Ctrl-C event");
        this_instance_->tcp_server_.StopServer();
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
#if defined __APPLE__ || defined __linux__ 
    std::signal(SIGINT,STEO_Server::SigIntHandler);
#else
    if (0 == SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        std::cout << "error: server exit...\n";
        return 1;
    }
#endif
    STEO_Server echoserver; 
    echoserver.InitializeTcpServer();
    std::cout << "server started\n";
    while( echoserver.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    return 0;
}

