# ASockLib 

### What 

A C++11 header-only, simple and easy cross-platform c++ socket server/client framework. 

- It performs TCP buffering internally.
- When all user data is received, the user-specified callback is called. 
- No repeat send calls until all are sent. When send returns WSAEWOULDBLOCK / EWOULDBLOCK / EAGAIN, It will be added to the queue and sent later.
- composition or inheritance usage.
- linux, os x : tcp, udp, domain socket using epoll and kqueue.
- windows : tcp, udp using winsock.


### Install 

This is a header-only library, so you can just add the asock folder to your project include directory.

### Usage 

#### tcp echo server 


```cpp
// See the sample folder for all examples.  
// This is an inheritance usage.  
// you can find composition usage and udp, domain socket example too.

#include "asock/asock_tcp_server.hpp"

#define DEFAULT_PACKET_SIZE 1024
class Server : public asock::ASockTcpServer {
  private:
    bool OnRecvedCompleteData(asock::Context* context_ptr, const char* const data_ptr, size_t len ) override;
    void OnClientConnected(asock::Context* context_ptr) override;
    void OnClientDisconnected(asock::Context* context_ptr) override; 
};

bool Server::OnRecvedCompleteData(asock::Context* context_ptr, 
                                const char* const data_ptr, size_t len ) {
    //user specific : - your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr,len );
    packet[len] = '\0';
    std::cout << "recved [" << packet << "]\n";
    
    if(! tcp_server_.SendData(context_ptr, data_ptr, len) ) {
        std::cerr << GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

void Server::OnClientConnected(asock::Context* context_ptr) {
    std::cout << "client connected : socket fd ["<< context_ptr->socket <<"]\n";
}
void Server::OnClientDisconnected(asock::Context* context_ptr) {
    std::cout << "client disconnected : socket fd ["<< context_ptr->socket <<"]\n";
}

int main(int argc, char* argv[]) {
    Server Server; 
    if(!Server.RunTcpServer("127.0.0.1", 9990 )) {
        std::cerr << Server.GetLastErrMsg() <<"\n"; 
        return 1;
    }
    std::cout << "server started" << "\n";
    while( Server.IsServerRunning() ) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    return 0;
}

```

#### tcp echo client

```cpp
#include "asock/asock_tcp_client.hpp"

#define DEFAULT_PACKET_SIZE 1024
class Client : public asock::ASockTcpClient
{
  private:
    bool OnRecvedCompleteData(asock::Context* , const char* const data_ptr, size_t len) override; 
    void OnDisconnectedFromServer() override ; 
};

bool Client:: OnRecvedCompleteData(asock::Context* , const char* const data_ptr, size_t len) {
    //user specific : - your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet,data_ptr ,len);
    packet[len] = '\0';
    std::cout << "server response [" << packet << "]\n";
    return true;
}

void Client::OnDisconnectedFromServer() {
    std::cout << "server disconnected, terminate client\n";
    client_.Disconnect();
}

int main(int argc, char* argv[]) {
    Client client;
    if(!client.InitTcpClient("127.0.0.1", 9990 ) ) {
        std::cerr << client.GetLastErrMsg() <<"\n"; 
        exit(EXIT_FAILURE);
    }
    std::string user_msg  {""}; 
    while( client.IsConnected() ) {
        std::cin.clear();
        getline(std::cin, user_msg); 
        int msg_len = user_msg.length();
        if(msg_len>0) {
            if(! client.SendToServer(user_msg.c_str(), msg_len) ) {
                std::cerr << client.GetLastErrMsg() <<"\n"; 
                exit(EXIT_FAILURE);
            }
        }
    } //while
    exit(EXIT_SUCCESS);
}
```

#### sample compile

```sh
git clone https://github.com/jeremyko/ASockLib.git
cd ASockLib
mkdir build && cd build 
cmake ..
make  # or msbuild(windows)
```

