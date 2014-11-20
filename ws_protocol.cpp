#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <algorithm>
#include <iostream>

#include "sha1.h"
#include "base64.h"
#include "bcc_server.h"

using namespace std;

#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

string getHandshake_Response(char *key, int len)
{
    string response = string('\0'+"HTTP/1.1 101 Switching Protocols\r\n") +
        string("Upgrade: websocket\r\n") +
        string("Connection: Upgrade\r\n") +
        string("Sec-WebSocket-Accept: ")+ string(key).substr(0, len) + string("\r\n");
    return response;
}
extern "C" void handshake(int sock)
{
    int HAND_LEN = 500;
    char hand_shake[HAND_LEN];
    memset((char *) &hand_shake, '\0', HAND_LEN);
    int len = read(sock,&hand_shake,HAND_LEN-1);
    //got handshake

    string handshake_str(hand_shake);
    string clientKey = "Sec-WebSocket-Key: ";
    std::size_t found = handshake_str.find(clientKey);
    if (found != string::npos)
    {
        string key = handshake_str.substr((int)found);
        key = key.substr(clientKey.length(), key.find('\n')-1-clientKey.length());
        key.erase(remove(key.begin(), key.end(), '\n'), key.end());
        key.erase(remove(key.begin(), key.end(), '\r'), key.end());
        string guid = GUID;
        string toSha = key + guid;

        //sha
        unsigned char sha[20];
        sha1::calc(toSha.c_str(), toSha.length(), sha);
        size_t len = 0;
        char *sha64 = base64_encode(sha, 20, &len);
        string answer = getHandshake_Response((char*)sha64, len);
        free(sha64);
        answer.append("\r\n");
        write(sock, answer.c_str(), answer.length());

        //testing... ping
        string pckt = packFrame("PING");
        pckt[0] = (char)0b10001001;
        write(sock, pckt.c_str(), pckt.length());

        memset((char *) &hand_shake, '\0', HAND_LEN);
        int len2 = read(sock, &hand_shake, HAND_LEN-1);
        if(len2 > 0) printf("handshake successfull\n");
        else 
        {
            printf("handshake failed\n");
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

uint8_t* parse(const uint8_t *dataI, int length)
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

        data = (uint8_t*)malloc(len);
        for (uint64_t i = 0; i < len; i++)
            data[i] = v[pos + i] ^ key[i % 4];
    } else {
        if (size < (pos + len)) return NULL;

        data = (uint8_t*)malloc(len);
        memcpy(data, v + pos, len);
    }
    return data;
}