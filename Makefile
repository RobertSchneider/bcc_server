LDFLAGS += $(shell pkg-config --libs json-c)
CFLAGS += $(shell pkg-config --cflags json-c)

OBJS = bcc_server.c sha1.c base64.c ws_protocol.c

all:
	gcc $(OBJS) -o bcc_server $(CFLAGS)  $(LDFLAGS) -lbcc -pthread -std=c99 -g
