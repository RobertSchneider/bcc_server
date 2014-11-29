#ifndef BCC_SERVER_H
#define BCC_SERVER_H

struct BCC_User
{
	char ip[40];
	int socket;
	struct BCC_User *next;
};

char *packFrame(char *msg);
void closeSocket(int socket);

//methods
void error(char *msg);

//MAIN
int listenSocketServer(int argc, char *argv[]);
void* clientMain(void *arg);

//BROADCAST
void initBroadcastSocket(void);
void broadcastMsg(char *msg);
void* listenBroadcast(void* argc);

#endif