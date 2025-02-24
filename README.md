# ASockLib

## Features

**A C++11 header-only, simple and easy cross-platform socket library.**

- Supports TCP, UDP, DOMAIN SOCKET(DS).
- It performs buffering internally(TCP,DS).
- When all user data is received, the user-specified callback is called.
- No repeat send calls until all are sent. When send returns
WSAEWOULDBLOCK / EWOULDBLOCK / EAGAIN, It will be added to the queue and sent later.
- Composition or Inheritance class usage.
- Using epoll (linux), kqueue (osx) and winsock (windows).
- No additional dependencies required.

## Install and use asock in your project

- ### Option 1: Including the source code directly in your project  

  This is a header-only library, so you can just add the asock folder
  to your project include directory.
  
        cp -r asock your_include_path/.

- ### Option 2: Using CMake FetchContent  

  Add below code to your CMake file

        include(FetchContent)
        fetchcontent_declare(
            asock
            GIT_REPOSITORY https://github.com/jeremyko/ASockLib
            GIT_TAG        42c7a720f708560c0842909c328152c08b4f0079 #1.2.0
        )
        fetchcontent_makeavailable(asock)
  
- ### Option 3: Using vcpkg

  comming soon
  
- ### Option 4: Installing locally using CMake

  The test code has a [googletest](https://github.com/google/googletest) dependency.
  If you are simply installing asock, no test code compilation is required.
  The sample code has no dependencies, but is not required for asock installation.
  That's why `DJEREMYKO_ASOCK_BUILD_TESTS=OFF`
  and `-DJEREMYKO_ASOCK_BUILD_SAMPLES=OFF` are used.

        mkdir build
        cd build
        cmake .. -DJEREMYKO_ASOCK_BUILD_TESTS=OFF -DJEREMYKO_ASOCK_BUILD_SAMPLES=OFF
        sudo make install

  **Once installed with the option 2,3,4, you can use asock using cmake like this:**
  
      find_package(asock CONFIG REQUIRED)
      target_link_libraries(yours PRIVATE asock::asock)

## Sample code
The following is an tcp echo example using class inheritance. See the sample folder for all examples. 
You can find composition usage and udp, domain socket example too.

### tcp echo server

```cpp
//This is an inheritance usage.  
#include "asock/asock_tcp_server.hpp"

#define DEFAULT_PACKET_SIZE 1024
class Server : public asock::ASockTcpServer {
  private:
    bool OnRecvedCompleteData(asock::Context* context_ptr,
                              const char* const data_ptr, size_t len) override {
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
    void OnClientConnected(asock::Context* context_ptr) override {
        std::cout << "client connected : socket fd ["<< context_ptr->socket <<"]\n";
    }
    void OnClientDisconnected(asock::Context* context_ptr) override {
        std::cout << "client disconnected : socket fd ["<< context_ptr->socket <<"]\n";
    }
};

int main(int argc, char* argv[]) {
    Server Server; 
    if(!Server.RunTcpServer("127.0.0.1", 9990 )) {
        std::cerr << Server.GetLastErrMsg() <<"\n"; 
        exit(EXIT_FAILURE);
    }
    std::cout << "server started" << "\n";
    while( Server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "server exit...\n";
    exit(EXIT_SUCCESS);
}

```

#### tcp echo client

```cpp
//This is an inheritance usage.  
#include "asock/asock_tcp_client.hpp"

#define DEFAULT_PACKET_SIZE 1024
class Client : public asock::ASockTcpClient {
  private:
    bool OnRecvedCompleteData(asock::Context* ,
                              const char* const data_ptr, size_t len) override {
        //user specific : - your whole data has arrived.
        char packet[DEFAULT_PACKET_SIZE];
        memcpy(&packet,data_ptr ,len);
        packet[len] = '\0';
        std::cout << "server response [" << packet << "]\n";
        return true;
    }
    void OnDisconnectedFromServer() override {
        std::cout << "server disconnected, terminate client\n";
        client_.Disconnect();
    }
};

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
