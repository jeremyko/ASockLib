#include <cstdlib>
#include <iostream>
#include <cassert>
#include <csignal>
#include "asock/asock_tcp_server.hpp"

// The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024

///////////////////////////////////////////////////////////////////////////////
//Send To Each Other Server
// An example in which the server and the client randomly exchange data with each other.
///////////////////////////////////////////////////////////////////////////////

// server creates SERVER_SEND_THREADS_PER_CLIENT message sending threads when a client connects, 
// and each thread sends SERVER_MSG_PER_CLIENT_THREAD message.
size_t SERVER_SEND_THREADS_PER_CLIENT = 10; 
size_t SERVER_MSG_PER_CLIENT_THREAD   = 10; 
class Server {
  public:
    bool RunTcpServer() {
        this_instance_ = this;
        using std::placeholders::_1;
        using std::placeholders::_2;
        using std::placeholders::_3;
        server_.SetCbOnRecvedCompletePacket(std::bind( 
                                    &Server::OnRecvedCompleteData, this, _1,_2,_3));
        server_.SetCbOnClientConnected(std::bind( 
                                    &Server::OnClientConnected, this, _1));

        if(!server_.RunTcpServer("127.0.0.1", 9990 )) {
            std::cerr << server_.GetLastErrMsg() <<"\n";
            return false;
        }
        return true;
    }
    bool IsServerRunning(){
        return server_.IsServerRunning();
    }
    static void SigIntHandler(int signo) {
        if (signo == SIGINT) {
            std::cout << "stop server\n";
            this_instance_->server_.StopServer();
            exit(EXIT_SUCCESS);
        } else {
            exit(EXIT_FAILURE);
        }
    }
    std::string GetLastErrMsg(){
        return  server_.GetLastErrMsg();
    }
    asock::ASockTcpServer server_ ; //composite usage
  private:
    static Server* this_instance_ ;
    bool OnRecvedCompleteData(asock::Context* ctx_ptr,
                              const char* const data_ptr, size_t len ) {
        //user specific : your whole data has arrived.
 
        //char packet[DEFAULT_PACKET_SIZE];
        //memcpy(&packet, data_ptr, len);
        //packet[len] = '\0';
        //std::cout << "recved [" << packet << "]\n";
        //---------------------------------------
        //this is echo server
        if (!server_.SendData(ctx_ptr, data_ptr, len)) {
            ELOG( "error! "<< server_.GetLastErrMsg() ); 
            exit(EXIT_FAILURE);
        }
        return true;
    }
    void OnClientConnected(asock::Context* ctx_ptr) {
        //spawn new thread (server, client both sending each other)
        for (size_t j = 0; j < SERVER_SEND_THREADS_PER_CLIENT; j++) {
            std::thread send_thread(&Server::SendThread, this, ctx_ptr);
            send_thread.detach();
        }
    }

    // Send 10 times to the connected client .
    // This is not an echo response. Server sends first.
    void SendThread(asock::Context* ctx_ptr) {
        size_t cnt = 0;
        char send_msg[256];
        while(ctx_ptr->is_connected) {
            std::string data = "from server message ";
            data += std::to_string(cnt);
            memcpy(send_msg,data.c_str(),data.length());
            if(! server_.SendData(ctx_ptr,send_msg,data.length())) {
                ELOG( "error! "<< server_.GetLastErrMsg() ); 
                exit(EXIT_FAILURE);
            }
            cnt++;
            if(cnt >= SERVER_MSG_PER_CLIENT_THREAD){
                break;
            }
        }
    }
};

Server* Server::this_instance_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
    std::signal(SIGINT,Server::SigIntHandler);
    Server server;
    if(!server.RunTcpServer()){
        exit(EXIT_FAILURE);
    }
    std::cout << "server started\n";
    while( server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
#ifdef WIN32
        // std::cout << "*   client total  = " << echoserver.server_.GetCountOfClients() 
        //           << "    context cache = " << echoserver.server_.GetCountOfContextQueue() 
        //           << "\n";
#endif
    }
    std::cout << "server exit...\n";
    return 0;
}

