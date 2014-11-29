#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <string.h>

#include <bcc/bcc.h>
#include "sha1.h"
#include "base64.h"
#include "ws_protocol.h"
#include "bcc_server.h"

#define CLIENTPORT 48300
#define BROADPORT 6666
#define MAXBUFLEN 5120
#define MAXCLIENTS 30

//globals
const char *broadcastaddress = "192.168.1.255";
int broadcastSockID;
struct BCC_User *users;
int numClientSockets = 0;
struct sockaddr_in broadcastAddr;

//CODE

void error(char *msg)
{
    perror(&msg[0]);
    //exit(1);
}
void logStr(char *str)
{
    //no log because of daemon
    //printf("%s\n", str);
}

int main(int argc, char *argv[])
{
    if(argc >= 2) broadcastaddress = argv[1];
    
    //daemon
    pid_t pid, sid;
    pid = fork();
    if (pid != 0) {
        exit(EXIT_FAILURE);
    }
    sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }

    //listening broadcasts in new thread
    pthread_t pth;
    pthread_create(&pth,NULL, listenBroadcast, NULL);
    
    //init socket to send broadcasts
    initBroadcastSocket();
    //listen for incomming connections
    listenSocketServer(argc, argv);
    return 1;
}

char *packFrame(char *msg)
{
    int len = 1+strlen(msg)+1+4;
    int pos = 2;
    if(strlen(msg) >= 126 && strlen(msg) <= 65535) {len += 3; pos += 2;}

    char *frame = malloc(len+1);
    frame[0] = (char)129;

    if(strlen(msg) < 125) 
    {
        frame[1] = (int)strlen(msg);
    }
    else if(strlen(msg) >= 126 && strlen(msg) <= 65535)
    {
        frame[1] = (char)126;
        frame[2] = (char)((strlen(msg) >> 8) & 255);
        frame[3] = (char)(strlen(msg) & 255);
    }
    strcpy(frame+pos, msg);

    frame[len-3] = '\r';
    frame[len-2] = '\n';
    frame[len-1] = '\0';
    return frame;
}
void writeFrame(int sock, char *s)
{
    char *s2;
    s2 = packFrame(s);
    write(sock, s2, strlen(s2));
    free(s2);
}

struct BCC_User* getUserWithSocket(int socket)
{
    struct BCC_User *user = users;
    while (user != NULL) 
    {
        if(user->socket == socket) return user;
        user = user->next;
    }
}
struct BCC_User* getUserWithNext(struct BCC_User *u)
{
    struct BCC_User *user = users;
    while (user != NULL) 
    {
        if(user->next == u) return user;
        user = user->next;
    }
}

void closeSocket(int socket)
{
    //remove socket

    struct BCC_User *user = getUserWithSocket(socket);
    if(user == NULL) return;
    struct BCC_User *parent = getUserWithNext(user);
    if(parent != NULL)
    {
        if(user->next != NULL) parent->next = user->next;
        else parent->next = NULL;
    }else 
    {
        if(user->next != NULL)  users = users->next;
        else users = NULL;
    }
    close(socket);
    logStr("socket closed");
    free(user);
    numClientSockets--;
}

void replace(char* c, int len, char cfrom, char cto)
{
    for (int i = 0; i < len; ++i)
    {
        if(c[i] == cfrom) c[i] = cto;
    }
}

//MAIN
void* clientMain(void *arg)
{
    char buffer[MAXBUFLEN];
    int n;
    int socket = *(int*)arg;

    struct BCC_User *user = getUserWithSocket(socket);
    while(1)
    {
        memset((char *) &buffer, '\0', MAXBUFLEN);
        n = read(socket,&buffer,MAXBUFLEN-1);
        if(n == 0 || (int)(buffer[0] & 0x0F) == 0)
        {
            closeSocket(socket);
            break;
        }
        int opcode = (int)(buffer[0] & 0x0F);
        //text
        if(opcode == 1)
        {
            int len = 0;
            uint8_t *outBuffer = parse((const uint8_t*)buffer, n, &len);
            if(outBuffer == NULL) 
            {
                logStr("ERROR reading / parsing frame failed");
                continue;
            };
            if (n < 0) logStr("ERROR reading socket failed");
            replace((char*)outBuffer, len, '\\', ' ');
            replace((char*)outBuffer, len, '<', ' ');
            replace((char*)outBuffer, len, '>', ' ');

            json_object *json = json_tokener_parse((char*)outBuffer);
            char *nick;
            char *msg;
            json_object_object_foreach(json, key, val)
            {
                const char *v = json_object_get_string(val);
                if (strcmp(key, "nick") == 0) {
                    nick = malloc(strlen(v)+1);
                    strcpy(nick, v);
                    nick[strlen(v)] = '\0';
                } else if (strcmp(key, "msg") == 0) {
                    msg = malloc(strlen(v)+1);
                    strcpy(msg, v);
                    msg[strlen(v)] = '\0';
                }
            }
            json_object_put (json);
            char *ip = user->ip;
            struct BccMessage *bccMsg = createMessage(nick, ip, msg);
            if(bccMsg == NULL) {logStr("ERROR parsing msg"); continue;}

            logStr("encoding...");
            char *toSend = encodeMessage(bccMsg);
            broadcastMsg(toSend);
            free(toSend);

            free(nick);
            free(msg);
            free(bccMsg->msg);
            free(bccMsg);
            free(outBuffer);
        }else if(opcode == 10)
        {
            //pong!
            logStr("PONG! received");
        }else if(opcode == 9)
        {
            //ping!
            logStr("PING! received");
            char *pckt = packFrame("PING");
            pckt[0] = (char)0b10001001;
            write(socket, pckt, strlen(pckt));
            free(pckt);
        }else if(opcode == 8)
        {
            //close
            write(socket, buffer, (size_t)n);
            closeSocket(socket);
            break;
        }else 
        {
            //wtf is thaat?
        }
    }
    return NULL;
}

int listenSocketServer(int argc, char *argv[])
{
    int serverSock, clientSock;
    struct sockaddr_in serv_addr, cli_addr;
    int n;

    //init socket
    serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) logStr("ERROR socket creation failed");
    memset((char *) &serv_addr, '\0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(CLIENTPORT);

    //bind and listen
    int reuse = -1;
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) logStr("ERROR port reusing failed");
    if (bind(serverSock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)  logStr("ERROR socket binding failed");
    listen(serverSock,5);
    socklen_t clilen = sizeof(cli_addr);

    //accepting new clients
    while(1)
    {
        clientSock = accept(serverSock, (struct sockaddr *) &cli_addr, &clilen);
        if(numClientSockets >= MAXCLIENTS) continue;
        if (clientSock < 0)  
        {
            logStr("ERROR accepting new socket failed");
            continue;
        }
        logStr("connected");

        //handshake
        handshake(clientSock);

        struct BCC_User *user = malloc(sizeof(struct BCC_User));
        if(user == NULL)
        {
            logStr("error allocating user\n");
            continue;
        }
        if(users == NULL) users = user;
        else 
        {
            struct BCC_User *last = getUserWithNext(NULL);
            last->next = user;
        }
        
        user->socket = clientSock;
        strcpy(user->ip, inet_ntoa(cli_addr.sin_addr));
        user->ip[39] = '\0';
        user->next = NULL;

        numClientSockets++;
        //new thread for every client
        pthread_t pth;
        pthread_create(&pth,NULL, clientMain, &clientSock);
    }
    
    logStr("closing server\n");
    return 0; 
}

//BROADCAST
void initBroadcastSocket(void)
{
    //create socket to send to broadcastadd
    broadcastSockID = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (broadcastSockID < 0) logStr("ERROR broadcastsocket creation failed");

    int broadcastEnable=1;
    if(setsockopt(broadcastSockID, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable))) 
        logStr("ERROR broadcastsocket creation failed");

    memset(&broadcastAddr, 0, sizeof broadcastAddr);
    broadcastAddr.sin_family = AF_INET;
    inet_pton(AF_INET, &broadcastaddress[0], &broadcastAddr.sin_addr);
    broadcastAddr.sin_port = htons(BROADPORT);
}

void broadcastMsg(char *msg)
{
    //send broadcast msg
    int ret = sendto(broadcastSockID, msg, strlen(msg), 0, (struct sockaddr*)&broadcastAddr, sizeof broadcastAddr);
    if (ret < 0) logStr("ERROR sending broadcast");
}

void* listenBroadcast(void* argc)
{
    //main loop for listening to broadcasts
    int sockfd;
    struct sockaddr_in my_addr;
    struct sockaddr_in their_addr;
    socklen_t addr_len;
    int numbytes;
    char buffer[MAXBUFLEN+1];

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) logStr("ERROR creating broadcast socket");

    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(BROADPORT);
    memset(&(my_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) < 0) 
        logStr("ERROR binding broadcast socket");

    addr_len = sizeof(struct sockaddr);
    while(1)
    {
        memset((char *) &buffer, '\0', MAXBUFLEN);
        if ((numbytes = recvfrom(sockfd, buffer, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) < 0) 
            logStr("ERROR receiving broadcast");
        //recreate packet from buffer
        //inform every client
        struct BccMessage *bccmsg = malloc(sizeof(struct BccMessage));
        int ret = parseMessage(buffer, bccmsg);
        if(ret != 0)
        {
            logStr("broadcast invalid\n");
            continue;
        }
        //jsonmsg
        json_object *json = json_object_new_object();
        json_object_object_add(json, "nick", json_object_new_string(bccmsg->nick));
        json_object_object_add(json, "ip", json_object_new_string(bccmsg->ipAddr));
        json_object_object_add(json, "msg", json_object_new_string(bccmsg->msg));
    
        const char *s = json_object_to_json_string(json);
        char toSend[strlen(s)+1];
        strcpy(toSend, s);
        toSend[strlen(s)] = '\0';

        struct BCC_User *user = users;
        while (user != NULL) 
        {
            int error;
            socklen_t len = sizeof(error);
            if(getsockopt(user->socket, SOL_SOCKET, SO_ERROR, &error, &len) == 0) writeFrame(user->socket, toSend);
            user = user->next;
        }
        json_object_put (json);

        free(bccmsg->msg);
        free(bccmsg);
    }
    return NULL;
}
