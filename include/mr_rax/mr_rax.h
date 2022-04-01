#ifndef MR_RAX_H
#define MR_RAX_H

#include "mr_rax/rax.h"

#define MAX_TOKENS 32
#define MAX_TOKEN_LEN 64
#define MAX_TOPIC_LEN 256

// invalid utf8 chars used to separate clients & shared subs from topics
static uint8_t client_mark = 0xFE;
static uint8_t shared_mark = 0xFF;

// MQTT disallowed control char used to represent a zero-length token
static uint8_t empty_tokenv[] = {0x1f, 0};

int mr_insert_subscription(rax* prax, const char* subtopic, const uint64_t client);
int mr_get_clients(rax* prax, rax* crax, char* topic);

#endif // MR_RAX_H