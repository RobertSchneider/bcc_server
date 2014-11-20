#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

//parse frames
//perform handshake 

extern "C" void handshake(int sock);
uint8_t* parse(const uint8_t *dataI, int length);
#endif