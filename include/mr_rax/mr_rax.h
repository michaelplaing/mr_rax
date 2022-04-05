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

int mr_get_normalized_topic(const char* pubtopic, char* topic);
int mr_get_subscribe_topic(const char* subtopic, char* topic, char* share);
int mr_insert_subscription(rax* tcrax, rax* crax, const char* subtopic, const uint64_t client);
int mr_get_subscribed_clients(rax* tcrax, rax* srax, const char* pubtopic);
int mr_upsert_client_topic_alias(rax* tcrax, const uint64_t client, const char* pubtopic, const uintptr_t alias);

#endif // MR_RAX_H