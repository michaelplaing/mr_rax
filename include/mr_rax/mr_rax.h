#ifndef MR_RAX_H
#define MR_RAX_H

#include <stdbool.h>
#include "mr_rax/rax.h"

#define MAX_TOKENS 32
#define MAX_TOKEN_LEN 64
#define MAX_TOPIC_LEN 256

// invalid utf8 chars used to separate clients & shared subs from topics
static uint8_t shared_mark = 0xfe;
static uint8_t client_mark = 0xff;
static uint8_t client_suffix = 0x00;

// MQTT disallowed control char used to represent a zero-length token
static char empty_tokenv[] = {0x1f, 0};

int mr_next_client(raxIterator* piter, uint64_t* pu64);

int mr_insert_subscription(rax* topic_tree, rax* client_tree, const char* subtopic, const uint64_t client);
int mr_remove_subscription(rax* topic_tree, rax* client_tree, const char* subtopic, const uint64_t client);
int mr_remove_client_subscriptions(rax* topic_tree, rax* client_tree, const uint64_t client);
int mr_get_subscribed_clients(rax* topic_tree, rax* client_set, const char* pubtopic);

int mr_upsert_client_topic_alias(
    rax* client_tree, const uint64_t client, const bool isincoming, const char* pubtopic, const uint8_t alias
);

int mr_remove_client_topic_aliases(rax* client_tree, const uint64_t client);
int mr_get_alias_by_topic(rax* client_tree, const uint64_t client, const bool isincoming, const char* pubtopic, uint8_t* palias);
int mr_get_topic_by_alias(rax* client_tree, const uint64_t client, const bool isincoming, const uint8_t alias, char* pubtopic);
int mr_remove_client_data(rax* topic_tree, rax* client_tree, uint64_t client);

int mr_make_BEVBVBI(uint64_t u64, uint8_t *u8v, size_t u8vlen, int numbits);
int mr_extract_BEVBVBI(uint8_t *u8v, size_t u8vlen, int numbits, uint64_t *pu64);

#endif // MR_RAX_H