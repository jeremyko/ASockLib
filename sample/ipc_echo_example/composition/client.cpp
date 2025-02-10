#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include "ASock.hpp"

// NOTE: The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024
///////////////////////////////////////////////////////////////////////////////
class EchoClient 
{
  public:
    bool InitIpcClient(const char* ipc_sock_path);
    bool SendToServer(const char* data, size_t len);
    bool IsConnected() { return ipc_client_.IsConnected();}
    std::string  GetLastErrMsg(){return  ipc_client_.GetLastErrMsg() ; }
  private:
    asock::ASock   ipc_client_ ; //composite usage
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, size_t len); 
    void    OnDisconnectedFromServer() ; 
};

///////////////////////////////////////////////////////////////////////////////
bool EchoClient::InitIpcClient(const char* ipc_sock_path) {
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    ipc_client_.SetCbOnRecvedCompletePacket(std::bind( 
                            &EchoClient::OnRecvedCompleteData, this, _1,_2,_3));
    ipc_client_.SetCbOnDisconnectedFromServer(std::bind( 
                            &EchoClient::OnDisconnectedFromServer, this));
    if(!ipc_client_.InitIpcClient(ipc_sock_path) ) {
        std::cerr << ipc_client_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: OnRecvedCompleteData(asock::Context* , char* data_ptr, size_t len) {
    //user specific : your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr, len );
    packet[len] = '\0';
    std::cout << "server response [" << packet << "]\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool EchoClient:: SendToServer(const char* data, size_t len) {
    return ipc_client_.SendToServer(data, len);
}

///////////////////////////////////////////////////////////////////////////////
void EchoClient::OnDisconnectedFromServer() {
    std::cout << "* server disconnected ! \n";
    exit(1);
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
    if(argc !=2) {
        std::cerr << "usage : " << argv[0] << " ipc_socket_full_path \n\n";
        return 1;
    }
    EchoClient client;
    if(!client.InitIpcClient(argv[1])) {
        return 1;
    }
    std::cout << "client started" << "\n";
    std::string user_msg  {""}; 
    while( client.IsConnected() ) {
        std::cin.clear();
        getline(std::cin, user_msg); 
        int msg_len = user_msg.length();
        if(msg_len>0) {
            if(! client.SendToServer(user_msg.c_str(),msg_len) ) {
                std::cerr << client.GetLastErrMsg() <<"\n"; 
                return 1;
            }
        }
    }
    std::cout << "client exit...\n";
    return 0;
}

