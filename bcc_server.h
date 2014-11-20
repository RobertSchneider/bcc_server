#ifndef BCC_SERVER_H
#define BCC_SERVER_H

#include <iostream>

using namespace std;
string packFrame(string msg);
void closeSocket(int socket);

//methods
void error(string msg);

//MAIN
int listenSocketServer(int argc, char *argv[]);
void* clientMain(void *arg);

//BROADCAST
void initBroadcastSocket(void);
void broadcastMsg(string msg);
void* listenBroadcast(void* argc);

#endif