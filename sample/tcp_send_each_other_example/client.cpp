#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include <mutex> 
#include <atomic>
#include "ASock.hpp"
#include "../msg_defines.h"

///////////////////////////////////////////////////////////////////////////////
//Send To Each Other Client
// An example in which the server and the client randomly exchange data with each other.
///////////////////////////////////////////////////////////////////////////////

class STEO_Client 
{
  public:
    bool InitializeTcpClient(size_t client_id);
    void DisConnectTcpClient();
    bool IsConnected() { return tcp_client_.IsConnected();}
    void SendThread(size_t index) ;
    void WaitForClientLoopExit();
    std::string  GetLastErrMsg(){return  tcp_client_.GetLastErrMsg() ; }
    std::atomic<int> server_msg_cnt_ ; // Check that all server messages have arrived
    size_t client_id_;
  private:
    asock::ASock   tcp_client_ ; //composite usage
    size_t  OnCalculateDataLen(asock::Context* context_ptr); 
    bool    OnRecvedCompleteData(asock::Context* context_ptr, char* data_ptr, 
                                 size_t len); 
    void    OnDisconnectedFromServer() ; 
    std::vector<std::string> vec_sent_strings_ ;
    std::mutex      sent_chk_lock_ ; // XXX vec_sent_strings_ is used by multiple threads.
};

///////////////////////////////////////////////////////////////////////////////
bool STEO_Client::InitializeTcpClient(size_t client_id)
{
    client_id_ = client_id ;
    //register callbacks
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    tcp_client_.SetCbOnCalculatePacketLen(std::bind( &STEO_Client::OnCalculateDataLen, this, _1));
    tcp_client_.SetCbOnRecvedCompletePacket(std::bind( &STEO_Client::OnRecvedCompleteData, this, _1,_2,_3));
    tcp_client_.SetCbOnDisconnectedFromServer(std::bind( &STEO_Client::OnDisconnectedFromServer, this));
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
    //LOG("Send Thread starts....... : "<< index);
    int sent_cnt =0;
    ST_MY_HEADER header;
    char send_msg[256];
    while(IsConnected()){
        std::string data = "client[";
        data += std::to_string(client_id_);
        data += "]thread index[";
        data += std::to_string(index);
        data += "](";
        data += std::to_string(sent_cnt) + std::string(")");
        {
            std::lock_guard<std::mutex> lock(sent_chk_lock_);
            vec_sent_strings_.push_back(data);
        }
        snprintf(header.msg_len, sizeof(header.msg_len), "%zu", data.length() );
        memcpy(&send_msg, &header, sizeof(header));
        memcpy(send_msg+sizeof(ST_MY_HEADER), data.c_str(), data.length());
        if (!tcp_client_.SendToServer(send_msg, 
                                      sizeof(ST_MY_HEADER) + data.length())) {
            DBG_ELOG("error! " << tcp_client_.GetLastErrMsg());
            return;
        }
        sent_cnt++ ;
        if(sent_cnt >= 10){
            //LOG("client " <<index << " completes send ");
            break;
        }
    }
    //LOG("send thread exiting : " << index);
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
    std::string response = data_ptr + CHAT_HEADER_SIZE;
    response.replace(len- CHAT_HEADER_SIZE, 1, 1, '\0');
    std::cout<<"server response  [" << response.c_str() << "]\n";

    // Let's check if it matches what we sent.
    if (response.compare(0,4,"from") == 0) {
        // skip "from server message 0"
        // This is not an echo response, the data was sent by the server. -> just increment the count
        server_msg_cnt_++;
    }else {
        bool found = false;
        std::lock_guard<std::mutex> lock(sent_chk_lock_);
        for (auto it = vec_sent_strings_.begin(); it != vec_sent_strings_.end(); ++it) {
            if (it->compare(response.c_str())==0){
                //std::cout <<"got complete data from server -------- \n";
                found = true;
                break;
            }
        }
        if (!found) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cerr << "data anomaly !!! --> [" << response <<"]\n";
            exit(1);
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void STEO_Client::OnDisconnectedFromServer() {
    std::cout << "* server disconnected ! \n";
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    size_t MAX_CLIENTS = 20;
    size_t MAX_THREADS = 5;
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
        //for (size_t j = 0; j < MAX_THREADS; j++) {
        //    vec_threads.push_back(std::thread(&STEO_Client::SendThread, client, j));
        //}
        vec_clients.push_back(client);
    }
    //spawn thread after all client starts..
    for (auto it = vec_clients.begin(); it != vec_clients.end(); ++it) {
        for (size_t j = 0; j < MAX_THREADS; j++) {
            vec_threads.push_back(std::thread(&STEO_Client::SendThread, *it, j));
        }
    }
    for(size_t i = 0; i < vec_threads.size(); i++) {
        if (vec_threads[i].joinable()) {
            vec_threads[i].join();
        }
    }
    // All send operations are done, but later server response can be received asynchronously. 
    // In order to handle this properly, it is necessary to properly determine and process 
    // the exit time. By the way, this is a simple example, so keep it simple. wait long enough. :-)
    // if you increase the total number of threads, etc., 
    // you may have to wait a bit longer to avoid synchronization errors in this example.
	std::this_thread::sleep_for(std::chrono::seconds(10));

    for (auto it = vec_clients.begin(); it != vec_clients.end(); ++it) {
        (*it)->DisConnectTcpClient();
    }
    for (auto it = vec_clients.begin(); it != vec_clients.end(); ++it) {
        (*it)->WaitForClientLoopExit();
    }

	std::cout << "\n\n=================== all clients exiting ====================\n";
    std::cout << "total clients = " << vec_clients.size() <<"\n";
    std::cout << "total threads = " << vec_threads.size() << "\n\n";
    for (auto it = vec_clients.begin(); it != vec_clients.end(); ++it) {
        std::cout << "server msg count : client -> " << (*it)->client_id_ 
            << " , count =  " << (*it)->server_msg_cnt_ << "\n";
    }
    
    while (! vec_clients.empty()) {
        delete vec_clients.back();
        vec_clients.pop_back();
    }
    vec_clients.clear();

    return 0;
}

