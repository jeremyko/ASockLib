#include <iostream>
#include <string>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include <conio.h >
#include "ASock.hpp"
#include "../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
//Send To Each Other Client
class STEO_Client 
{
  public:
    bool InitializeTcpClient(size_t client_id);
    void DisConnectTcpClient();
    bool IsConnected() { return tcp_client_.IsConnected();}
    void SendThread(size_t index) ;
    void WaitForClientLoopExit();
    std::string  GetLastErrMsg(){return  tcp_client_.GetLastErrMsg() ; }
  private:
    size_t client_id_;
    asock::ASock   tcp_client_ ; //composite usage
    size_t  OnCalculateDataLen(asock::Context* context_ptr); 
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, 
                                 size_t len); 
    void    OnDisconnectedFromServer() ; 
};

///////////////////////////////////////////////////////////////////////////////
bool STEO_Client::InitializeTcpClient(size_t client_id)
{
    client_id_ = client_id ;
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    tcp_client_.SetCbOnCalculatePacketLen(std::bind(
                       &STEO_Client::OnCalculateDataLen, this, _1));
    tcp_client_.SetCbOnRecvedCompletePacket(std::bind(
                       &STEO_Client::OnRecvedCompleteData, this, _1,_2,_3));
    tcp_client_.SetCbOnDisconnectedFromServer(std::bind(
                       &STEO_Client::OnDisconnectedFromServer, this));
    //connect timeout is 3 secs, max message length is approximately 1024 bytes...
    if(!tcp_client_.InitTcpClient("127.0.0.1", 9990, 3, 1024 ) ) {
        ELOG("error : "<< tcp_client_.GetLastErrMsg() ); 
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void STEO_Client::DisConnectTcpClient() {
    tcp_client_.Disconnect();
}
///////////////////////////////////////////////////////////////////////////////
void STEO_Client::WaitForClientLoopExit() {
    tcp_client_.WaitForClientLoopExit();
}
///////////////////////////////////////////////////////////////////////////////
void STEO_Client::SendThread(size_t index) 
{
    while (true) {
        if (IsConnected()) {
            break;
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(1)); 
            continue;
        }
    }
    //DBG_LOG("Send Thread starts....... : "<< index);
    int sent_cnt =0;
    ST_MY_HEADER header;
    //char send_msg[256];
    while(IsConnected()){
        std::string data = "client [";
        data += std::to_string(client_id_);
        data += "] thread index [";
        data += std::to_string(index);
        data += "] ---- sending this(";
        data += std::to_string(sent_cnt) + std::string(")");
        snprintf(header.msg_len, sizeof(header.msg_len), "%zu", data.length() );
        /*
        //---------------------------------------- send one buffer
        memcpy(&send_msg, &header, sizeof(header));
        memcpy(send_msg+sizeof(ST_MY_HEADER), data.c_str(), data.length());
        if (!tcp_client_.SendToServer(send_msg, 
                                      sizeof(ST_MY_HEADER) + data.length())) {
            DBG_ELOG("error! " << tcp_client_.GetLastErrMsg());
            return;
        }
        */
        //---------------------------------------- send 2 times
        if (!tcp_client_.SendToServer(reinterpret_cast<char*>(&header),
            sizeof(ST_MY_HEADER))) {
            std::cerr <<"error! " << tcp_client_.GetLastErrMsg()<<"\n";
            return;
        }
        if (!tcp_client_.SendToServer(data.c_str(), data.length())) {
            std::cerr <<"error! " << tcp_client_.GetLastErrMsg()<< "\n";
            return;
        }
        
        sent_cnt++ ;
        if(sent_cnt >= 5){
            LOG("client " <<index << " completes send ");
            break;
        }
        //std::this_thread::sleep_for(std::chrono::seconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    LOG("send thread exiting : " << index);
}

///////////////////////////////////////////////////////////////////////////////
size_t STEO_Client::OnCalculateDataLen(asock::Context* context_ptr)
{
    //user specific : calculate your complete packet length 
    if( context_ptr->GetBuffer()->GetCumulatedLen() < (int)CHAT_HEADER_SIZE ) {
        return asock::MORE_TO_COME ; //more to come 
    }
    ST_MY_HEADER header ;
    context_ptr->GetBuffer()->PeekData(CHAT_HEADER_SIZE, (char*)&header);  
    size_t supposed_total_len = std::atoi(header.msg_len) + CHAT_HEADER_SIZE;
    return supposed_total_len ;
}

///////////////////////////////////////////////////////////////////////////////
bool STEO_Client:: OnRecvedCompleteData(asock::Context* context_ptr, 
                                       char* data_ptr, size_t len) 
{
    //user specific : your whole data has arrived.
    char packet[asock::DEFAULT_PACKET_SIZE]; 
    memcpy(&packet, data_ptr+CHAT_HEADER_SIZE, len-CHAT_HEADER_SIZE);
    packet[len-CHAT_HEADER_SIZE] = '\0';
    std::cout << "*** ["<< packet <<"]"<<", client_id =" << client_id_ << "\n";
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void STEO_Client::OnDisconnectedFromServer() {
    std::cout << "* server disconnected ! \n";
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    size_t MAX_CLIENTS = 100;
    size_t MAX_THREADS = 3;
    std::vector<std::thread>  vec_threads ;
    std::vector<STEO_Client*> vec_clients;
    std::cout << "client started\n";
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        STEO_Client* client = new (std::nothrow) STEO_Client;
        if (client == nullptr) {
            std::cerr << "alloc failed!!\n";
            exit(1);
        }
        if(!client->InitializeTcpClient(i)){
            continue;
        }
        //spawn threads along with creating clients
        /*
        for (size_t j = 0; j < MAX_THREADS; j++) {
            vec_threads.push_back(std::thread(&STEO_Client::SendThread, client, j));
        }
        */
        vec_clients.push_back(client);
    }
    //spawn thread after all client starts..
    for (auto it = vec_clients.begin(); it != vec_clients.end(); ++it) {
        for (size_t j = 0; j < MAX_THREADS; j++) {
            vec_threads.push_back(std::thread(&STEO_Client::SendThread, *it, j));
        }
    }
    
    std::cout <<"total clients = " << vec_clients.size();
    std::cout << "total threads = " << vec_threads.size();
    for(size_t i = 0; i < vec_threads.size(); i++) {
        if (vec_threads[i].joinable()) {
            vec_threads[i].join();
        }
    }
    for (auto it = vec_clients.begin(); it != vec_clients.end(); ++it) {
        (*it)->DisConnectTcpClient();
    }
    for (auto it = vec_clients.begin(); it != vec_clients.end(); ++it) {
        (*it)->WaitForClientLoopExit();
    }
    std::cout << "==== all clients exiting : "<<vec_clients.size()<<"\n";
    while (! vec_clients.empty()) {
        delete vec_clients.back();
        vec_clients.pop_back();
    }
    vec_clients.clear();
    std::cout << '\n' << "Press enter to exit...";
    char c = getchar() ;
    return 0;
}

