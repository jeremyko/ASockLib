#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <stdio.h>
#include <cassert>
#include <mutex> 
#include <atomic>
#include "asock/asock_tcp_client.hpp"
#include "condvar.hpp"
#include "elapsed_time.hpp"

// The buffer must be large enough to hold the entire data.
#define DEFAULT_PACKET_SIZE 1024

///////////////////////////////////////////////////////////////////////////////
//Send To Each Other Client
// An example in which the server and the client randomly exchange data with each other.
///////////////////////////////////////////////////////////////////////////////

CondVar g_all_done_cond_var;
size_t MAX_CLIENTS        = 200;
size_t THREADS_PER_CLIENT = 10;
size_t SEND_PER_THREAD    = 10;

size_t SERVER_SEND_THREADS_PER_CLIENT = 10;
size_t SERVER_MSG_PER_CLIENT_THREAD   = 10;

size_t TOTAL_EXPECTED_SERVER_MSG_CNT  =
    (MAX_CLIENTS * SERVER_SEND_THREADS_PER_CLIENT * SERVER_MSG_PER_CLIENT_THREAD);

size_t TOTAL_EXPECTED_SERVER_ECHO_CNT =
    (MAX_CLIENTS * THREADS_PER_CLIENT * SEND_PER_THREAD);
std::atomic<size_t> g_responsed_cnt{ 0 };
std::atomic<size_t> g_server_msg_cnt{ 0 };

class Client {
  public:
    bool IntTcpClient(size_t client_id) {
        using std::placeholders::_1;
        using std::placeholders::_2;
        using std::placeholders::_3;
        client_id_ = client_id ;
        client_.SetCbOnRecvedCompletePacket(std::bind( 
                                    &Client::OnRecvedCompleteData, this, _1,_2,_3));
    
        if(!client_.InitTcpClient("127.0.0.1", 9990   ) ) {
            std::cerr <<"error : "<< client_.GetLastErrMsg() << "\n"; 
            exit(EXIT_FAILURE);
        }
        return true;
    }
    void DisConnect() {
        client_.Disconnect();
    }
    bool IsConnected(){
        return client_.IsConnected();
    }
    void SendThread(size_t index) ;
    std::string GetLastErrMsg(){
        return client_.GetLastErrMsg();
    }
    size_t client_id_;
  private:
    asock::ASockTcpClient client_ ; //composite usage
    bool OnRecvedCompleteData(asock::Context* context_ptr, const char* const data_ptr ,
                              size_t len);
    std::vector<std::string> vec_sent_strings_ ;
    std::mutex  sent_chk_lock_ ; // XXX vec_sent_strings_ is used by multiple threads.
};

///////////////////////////////////////////////////////////////////////////////
void Client::SendThread(size_t index) {
    while (!IsConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1)); 
    }
    size_t sent_cnt =0;
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
        if (!client_.SendToServer(data.c_str(),data.length())) {
            DBG_ELOG("error! " << client_.GetLastErrMsg());
            exit(EXIT_FAILURE);
        }
        sent_cnt++ ;
        if(sent_cnt >= SEND_PER_THREAD){
            //LOG("client " <<index << " completes send ");
            break;
        }
    }
    //LOG("send thread exiting : " << index);
}

///////////////////////////////////////////////////////////////////////////////
bool Client:: OnRecvedCompleteData(asock::Context* , const char* const data_ptr, size_t len) {
    //user specific : your whole data has arrived.

    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr,len);
    packet[len] = '\0';
    std::string response = packet;
    //std::cout << "server response [" << response << "] len=" << len <<"\n";

    // Let's check if it matches what we sent.
    if (response.compare(0,4,"from") == 0) {
        // skip "from server message 0"
        // This is not an echo response, the data was sent by the server. -> just increment the count
        g_server_msg_cnt++;
    }else {
        bool found = false;
        std::lock_guard<std::mutex> lock(sent_chk_lock_);
        for (auto it = vec_sent_strings_.begin(); it != vec_sent_strings_.end(); ++it) {
            if (it->compare(0, len,response)==0){
                //std::cout <<"got complete data from server -------- \n";
                found = true;
                g_responsed_cnt++;
                break;
            }
        }
        if (!found) {
            std::cerr << "data abnomaly !!! --> [" << response <<"]\n";
            exit(EXIT_FAILURE);
        }
    }
    // - client : after connecting to the server, the client starts THREADS_PER_CLIENT threads, 
    //            sends a message 10 times per thread, and receives an echo response.
    // - server : creates SERVER_SEND_THREADS_PER_CLIENT message sending threads when a client connects, 
    //            and each thread sends SERVER_MSG_PER_CLIENT_THREAD message.
    if( g_server_msg_cnt == TOTAL_EXPECTED_SERVER_MSG_CNT && 
        g_responsed_cnt  == TOTAL_EXPECTED_SERVER_ECHO_CNT) {
        LOG("all done :" << "server_msg_cnt_="<< g_server_msg_cnt <<", responsed_cnt="<< g_responsed_cnt);
        g_all_done_cond_var.NotifyOne();
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
int main(int , char* []) {
    ElapsedTime elapsed;
    std::vector<std::thread>  vec_threads ;
    std::vector<Client*> vec_clients;
    std::cout << "client started\n";
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        Client* client = new (std::nothrow) Client;
        if (client == nullptr) {
            std::cerr << "alloc failed!!\n";
            exit(EXIT_FAILURE);
        }
        if(!client->IntTcpClient(i)){
            exit(EXIT_FAILURE);
        }
        vec_clients.push_back(client);
    }
    elapsed.SetStartTime();
    //spawn thread after all client starts..
    for (auto it = vec_clients.begin(); it != vec_clients.end(); ++it) {
        for (size_t j = 0; j < THREADS_PER_CLIENT; j++) {
            vec_threads.push_back(std::thread(&Client::SendThread, *it, j));
        }
    }
    for(size_t i = 0; i < vec_threads.size(); i++) {
        if (vec_threads[i].joinable()) {
            vec_threads[i].join();
        }
    }
    // All send operations are done, 
    // but later server response can be received asynchronously. 
    // --> wait all done
    g_all_done_cond_var.WaitForSignal();

    size_t elapsed_time =  elapsed.SetEndTime(MILLI_SEC_RESOLUTION) ;
    std::cout << "waiting all clients exit\n";
    for (auto it = vec_clients.begin(); it != vec_clients.end(); ++it) {
        (*it)->DisConnect();
    }
    while (! vec_clients.empty()) {
        delete vec_clients.back();
        vec_clients.pop_back();
    }
    vec_clients.clear();

	std::cout << "\n\n=====================================================================\n";
    std::cout << "total clients               = " << MAX_CLIENTS <<"\n";
    std::cout << "threads per client          = " << THREADS_PER_CLIENT << "\n";
    std::cout << "send per thread             = " << SEND_PER_THREAD << "\n";
    int elapsed_hour = (int)elapsed_time / (60 * 60 * 1000);
    int elapsed_min = (int)elapsed_time / (60 * 1000);
    int elapsed_sec = (int)elapsed_time / 1000;
    char elapsed_fmt[100];
    snprintf(elapsed_fmt,sizeof(elapsed_fmt), "%02d:%02d:%02d.%03d", elapsed_hour , 
         (int)elapsed_min -(elapsed_hour * 60) , (int)elapsed_sec -(elapsed_min * 60)  , 
         (int)elapsed_time - (1000 * elapsed_sec) );
    std::cout << "total server response count = " << g_responsed_cnt << "\n";
    std::cout << "total server sent msg count = " << g_server_msg_cnt << "\n";
    std::cout << "elapsed                     = " <<  elapsed_fmt << " / " <<elapsed_time <<"ms\n\n";

    exit(EXIT_SUCCESS);
}

