g++ -pthread -Wall -std=c++11 -O2 -g -o server echo_server.cpp ../../src/AServerSocketTCP.cpp  ../../src/ASockBase.cpp
g++ -pthread -Wall -std=c++11 -O2 -g -o client client.cpp      ../../src/AClientSocketTCP.cpp  ../../src/ASockBase.cpp
