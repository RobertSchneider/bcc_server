#include <stdio.h>
#include <arpa/inet.h>
#include <vector>
#include <algorithm>

#include <bcc/bcc.h>
#include "sha1.h"
#include "base64.h"
#include "ws_protocol.h"
#include "bcc_server.h"

using namespace std;

#define CLIENTPORT 48300
#define BROADPORT 6666
#define MAXBUFLEN 51200
#define MAXCLIENTS 10

//globals
string broadcastaddress = "192.168.0.255";
int broadcastSockID;
std::vector<int> clientSockets;
int numClientSockets = 0;
struct sockaddr_in broadcastAddr;

//CODE

void error(string msg)
{
    perror(&msg[0]);
    //exit(1);
}

int main(int argc, char *argv[])
{
    broadcastaddress = argv[1];
    
    //listening broadcasts in new thread
    pthread_t pth;
    pthread_create(&pth,NULL, listenBroadcast, NULL);
    
    //init socket to send broadcasts
    initBroadcastSocket();
    //listen for incomming connections
    listenSocketServer(argc, argv);
    return 1;
}


string packFrame(string msg)
{
    string frame = "";
    frame.push_back((char)129);
    if(msg.length() < 125) 
    {
        frame.push_back((char)msg.length());
    }
    else if(msg.length() >= 126 && msg.length() <= 65535)
    {
        frame.push_back((char)126);
        frame.push_back((char)(msg.length() >> 8) & 255);
        frame.push_back((char)(msg.length()) & 255);
    }
    frame.append(msg);
    return frame;
}
void writeFrame(int sock, string s)
{
    string s2 = packFrame(s);
    write(sock, s2.c_str(), s2.length());
}

void closeSocket(int socket)
{
    error("socket closed");
    //remove socket
    close(socket);
    std::vector<int>::iterator position = std::find(clientSockets.begin(), clientSockets.end(), socket);
    if (position != clientSockets.end()) clientSockets.erase(position);
    numClientSockets--;
}

//MAIN
void* clientMain(void *arg)
{
    char buffer[MAXBUFLEN];
    int n;
    int socket = *(int*)arg;
    while(1)
    {
        memset((char *) &buffer, '\0', MAXBUFLEN);
        n = read(socket,&buffer,MAXBUFLEN-1);

        if(n == 0 || (int)(buffer[0] & 0x0F) == 8 || (int)(buffer[0] & 0x0F) == 0)
        {
            closeSocket(socket);
            break;
        }
        if((int)(buffer[0] & 0x0F) == 1)
        {
            uint8_t *outBuffer = parse((const uint8_t*)buffer, n);
            if(outBuffer == NULL) 
            {
                printf("nothing to read\n");
                continue;
            };
            if (n < 0) error("ERROR reading from socket");
            string nick;
            string ip;
            string msg;
            json_object *json = json_tokener_parse((char*)outBuffer);
            json_object_object_foreach(json, key, val)
            {
                const char *c = json_object_get_string(val);
                if (strcmp(key, "nick") == 0) {
                    nick = string(json_object_get_string(val));
                } else if (strcmp(key, "ip") == 0) {
                    ip = string(json_object_get_string(val));
                } else if (strcmp(key, "msg") == 0) {
                    msg = string(json_object_get_string(val));
                }
            }

            //lookup ip
            /*sockaddr_in cli_addr;
            socklen_t len = sizeof(cli_addr);
            getsockname(socket, (sockaddr*)&cli_addr, &len);
            ip = inet_ntoa(cli_addr.sin_addr);*/

            printf("nick:%s ip:%s msg:%s\n", nick.c_str(), ip.c_str(), msg.c_str());
            BccMessage bccmsg(nick.c_str(), ip.c_str(), msg.c_str());
            broadcastMsg(bccmsg.encodeMessage());

            free(outBuffer);
        }else if((int)(buffer[0] & 0x0F) == 10)
        {
            //pong!
            printf("PONG! client alive...\n");
        }else 
        {
            //wtf is thaat?
            printf("opcode not handled %d\n", (int)(buffer[0] & 0x0F));
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
    if (serverSock < 0) error("ERROR socket creation failed");
    memset((char *) &serv_addr, '\0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(CLIENTPORT);

    //bind and listen
    int reuse = -1;
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) error("ERROR port reusing failed");
    if (bind(serverSock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)  error("ERROR socket binding failed");
    listen(serverSock,5);
    socklen_t clilen = sizeof(cli_addr);

    //accepting new clients
    while(1)
    {
        clientSock = accept(serverSock, (struct sockaddr *) &cli_addr, &clilen);
        if(numClientSockets >= MAXCLIENTS) continue;
        if (clientSock < 0)  
        {
            error("ERROR accepting new socket failed");
            continue;
        }
        printf("new socket connected %d %s\n", clientSock, inet_ntoa(cli_addr.sin_addr));

        //handshake
        handshake(clientSock);
        
        clientSockets.push_back(clientSock);
        numClientSockets++;
        //new thread for every client
        pthread_t pth;
        pthread_create(&pth,NULL, clientMain, &clientSock);
    }
    
    printf("closing server\n");
    return 0; 
}

//BROADCAST
void initBroadcastSocket(void)
{
    //create socket to send to broadcastadd
    broadcastSockID = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (broadcastSockID < 0) error("ERROR broadcastsocket creation failed");

    int broadcastEnable=1;
    if(setsockopt(broadcastSockID, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable))) 
        error("ERROR broadcastsocket creation failed");

    memset(&broadcastAddr, 0, sizeof broadcastAddr);
    broadcastAddr.sin_family = AF_INET;
    inet_pton(AF_INET, &broadcastaddress[0], &broadcastAddr.sin_addr);
    broadcastAddr.sin_port = htons(BROADPORT);
}

void broadcastMsg(string msg)
{
    //send broadcast msg
    int ret = sendto(broadcastSockID, &msg[0], strlen(&msg[0]), 0, (struct sockaddr*)&broadcastAddr, sizeof broadcastAddr);
    if (ret < 0) error("ERROR sending broadcast");
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

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) error("ERROR creating broadcast socket");

    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(BROADPORT);
    memset(&(my_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) < 0) 
        error("ERROR binding broadcast socket");

    addr_len = sizeof(struct sockaddr);
    while(1)
    {
        if ((numbytes = recvfrom(sockfd, buffer, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) < 0) 
            error("ERROR receiving broadcast");
        printf("broadcast to %d\n", (int)clientSockets.size());
        //recreate packet from buffer
        //inform every client
        std:string str(buffer);
        BccMessage msg(str);

        //jsonmsg
        json_object *json = json_object_new_object();
        json_object_object_add(json, "nick", json_object_new_string(msg.getNick()));
        json_object_object_add(json, "ip", json_object_new_string(msg.getIp()));
        json_object_object_add(json, "msg", json_object_new_string(msg.getMsg().c_str()));
    
        string toSend = json_object_to_json_string(json);
        for(int i = 0; i < clientSockets.size(); i++)
        {
            int cSock = clientSockets[i];
            int error;
            socklen_t len = sizeof(error); 
            if(getsockopt(cSock, SOL_SOCKET, SO_ERROR, &error, &len) == 0) writeFrame(cSock, toSend.append("\r\n"));
        }
    }
    return NULL;
}