#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

//parse frames
//perform handshake 

void handshake(int sock);
uint8_t* parse(const uint8_t *dataI, int length, int *len2);
#endif