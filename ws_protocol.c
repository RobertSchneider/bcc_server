#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdbool.h>

#include "sha1.h"
#include "base64.h"
#include "bcc_server.h"

#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

char* getHandshake_Response(char *key, int len)
{
    const char *resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
    char *response = malloc(strlen(resp)+len+4+1);
    strncpy(response, resp, strlen(resp));
    strncpy(response+strlen(resp), key, len);
    strcpy(response+strlen(resp)+len, "\r\n\r\n");
    response[strlen(resp)+len+4] = '\0';
    return response;
}
void handshake(int sock)
{
    int HAND_LEN = 500;
    char hand_shake[HAND_LEN];
    memset((char *) &hand_shake, '\0', HAND_LEN);
    int len = read(sock,&hand_shake,HAND_LEN-1);
    //got handshake6666

    const char *clientKey = "Sec-WebSocket-Key: ";
    char *found = strstr(hand_shake, clientKey); 
    if (found != NULL)
    {
        char key[24+strlen(GUID)+1];
        memcpy(key, hand_shake+(found-hand_shake)+strlen(clientKey), 24);
        strcpy(key+24, GUID);
        key[24+strlen(GUID)] = '\0';

        //sha
        unsigned char sha[20];
        calc(key, strlen(key), sha);
        size_t len = 0;
        char *sha64 = base64_encode(sha, 20, &len);
        char *answer = getHandshake_Response((char*)sha64, len);
        free(sha64);
        write(sock, answer, strlen(answer));

        free(answer);

        //testing... ping
        char *pckt = packFrame("PING");
        pckt[0] = (char)0b10001001;
        write(sock, pckt, strlen(pckt));
        free(pckt);

        memset((char *) &hand_shake, '\0', HAND_LEN);
        int len2 = read(sock, &hand_shake, HAND_LEN-1);
        if(len2 > 0) {}
        else 
        {
            closeSocket(sock);
        }
    }else 
    {
        //invalid handshake
    }
}

//thanks to florob
//decode frames
static uint64_t read_be_uint_n(const uint8_t buf[], size_t nbytes)
{
    uint64_t val = 0;
    for (size_t i = 0; i < nbytes; i++) {
        val *= 0x100;
        val += buf[i];
    }
    return val;
}

uint8_t* parse(const uint8_t *dataI, int length, int *len2)
{
    const uint8_t *v = dataI;
    size_t size = (size_t)length;

    if (size < 2) return NULL;

    const uint8_t flags = v[0];
    const bool FIN = flags & 0x80;
    const bool RSV1 = flags & 0x40;
    const bool RSV2 = flags & 0x20;
    const bool RSV3 = flags & 0x10;
    const uint8_t op = flags & 0x0F;

    uint64_t len = v[1];
    const bool MASK = len & 0x80;
    len &= 0x7F;

    uint64_t pos = 2;

    switch (len) {
    case 126:
        len = read_be_uint_n(v + pos, 2);
        pos += 2;
        break;
    case 127:
        len = read_be_uint_n(v + pos, 8);
        pos += 8;
        break;
    }

    uint8_t *data = NULL;

    if (MASK) {
        if (size < (pos + len + 4)) return NULL;

        uint8_t key[4];
        memcpy(key, v + pos, sizeof(key));
        pos += 4;

        data = (uint8_t*)malloc(len+1);
        for (uint64_t i = 0; i < len; i++)
            data[i] = v[pos + i] ^ key[i % 4];
        data[len] = '\0';
    } else {
        if (size < (pos + len)) return NULL;

        data = (uint8_t*)malloc(len+1);
        memcpy(data, v + pos, len); 
        data[len] = '\0';
    }
    *len2 = len;
    return data;
}
