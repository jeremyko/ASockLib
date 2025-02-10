#include <cstdlib>
#include <iostream>
#include <cassert>
#include <csignal>

#include "ASock.hpp"

// NOTE: The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024

///////////////////////////////////////////////////////////////////////////////
//Send To Each Other Server
// An example in which the server and the client randomly exchange data with each other.
///////////////////////////////////////////////////////////////////////////////
 
// server creates SERVER_SEND_THREADS_PER_CLIENT message sending threads when a client connects, 
// and each thread sends SERVER_MSG_PER_CLIENT_THREAD message.
size_t SERVER_SEND_THREADS_PER_CLIENT = 10; 
size_t SERVER_MSG_PER_CLIENT_THREAD   = 10; 
class STEO_Server 
{
  public:
    STEO_Server(){/*this_instance_ = this;*/ }
    bool    RunTcpServer();
    bool    IsServerRunning(){return tcp_server_.IsServerRunning();};
#if defined __APPLE__ || defined __linux__ 
    static void SigIntHandler(int signo);
#endif
    std::string  GetLastErrMsg(){return  tcp_server_.GetLastErrMsg() ; }
    asock::ASock tcp_server_ ; //composite usage
  private:
    bool    OnRecvedCompleteData(asock::Context* ctx_ptr, 
                                 char* data_ptr, size_t len ) ;
    void    OnClientConnected(asock::Context* ctx_ptr) ; 
    void    SendThread(asock::Context* ctx_ptr) ;
};

static STEO_Server* this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
bool STEO_Server::RunTcpServer() {
    this_instance_ = this; 
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    tcp_server_.SetCbOnRecvedCompletePacket(std::bind( 
                                &STEO_Server::OnRecvedCompleteData, this, _1,_2,_3));
    tcp_server_.SetCbOnClientConnected      (std::bind( 
                                &STEO_Server::OnClientConnected, this, _1));

    if(!tcp_server_.RunTcpServer("127.0.0.1", 9990 )) {
        std::cerr << tcp_server_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Send 10 times to the connected client .
// This is not an echo response. Server sends first.
void STEO_Server::SendThread(asock::Context* ctx_ptr) {
    size_t cnt = 0;
	char send_msg[256];
    while(ctx_ptr->is_connected) {
        std::string data = "from server message ";
        data += std::to_string(cnt);
        memcpy(send_msg,data.c_str(),data.length());
        if(! tcp_server_.SendData(ctx_ptr,send_msg,data.length())) {
            ELOG( "error! "<< tcp_server_.GetLastErrMsg() ); 
            return ;
        }
        cnt++;
        if(cnt >= SERVER_MSG_PER_CLIENT_THREAD){
            break;
        }
    }//while
}

///////////////////////////////////////////////////////////////////////////////
bool STEO_Server::OnRecvedCompleteData(asock::Context* ctx_ptr, 
                                         char* data_ptr, size_t len ) {
    //user specific : your whole data has arrived.
    
    //char packet[DEFAULT_PACKET_SIZE];
    //memcpy(&packet, data_ptr, len);
    //packet[len] = '\0';
    //std::cout << "recved [" << packet << "]\n";
    //---------------------------------------
    //this is echo server
    if (!tcp_server_.SendData(  ctx_ptr, data_ptr, len)) {
        ELOG( "error! "<< tcp_server_.GetLastErrMsg() ); 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void STEO_Server::OnClientConnected(asock::Context* ctx_ptr) {
    //spawn new thread (server, client both sending each other)
    for (size_t j = 0; j < SERVER_SEND_THREADS_PER_CLIENT; j++) {
        std::thread send_thread(&STEO_Server::SendThread, this, ctx_ptr);
        send_thread.detach();
    }
}


///////////////////////////////////////////////////////////////////////////////
#if defined __APPLE__ || defined __linux__ 
void STEO_Server::SigIntHandler(int signo) {
    sigset_t sigset, oldset;
    sigfillset(&sigset);
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
        std::cerr << strerror(errno) << "/"<<signo<<"\n"; 
    }
    std::cout << "Stop Server! \n";
    this_instance_->tcp_server_.StopServer();
}
#else
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
        // Handle the CTRL-C signal. 
    case CTRL_C_EVENT:
        DBG_LOG("Ctrl-C event");
        this_instance_->tcp_server_.StopServer();
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
#if defined __APPLE__ || defined __linux__ 
    std::signal(SIGINT,STEO_Server::SigIntHandler);
#else
    if (0 == SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        std::cout << "error: server exit...\n";
        return 1;
    }
#endif
    STEO_Server server; 
    if(!server.RunTcpServer()){
        return 1;
    }
    std::cout << "server started\n";
    while( server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
#ifdef WIN32
        // std::cout << "*   client total  = " << echoserver.tcp_server_.GetCountOfClients() 
        //           << "    context cache = " << echoserver.tcp_server_.GetCountOfContextQueue() 
        //           << "\n";
#endif
    }
    std::cout << "server exit...\n";
    return 0;
}

