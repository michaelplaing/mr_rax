#ifndef MR_RAX_INTERNAL_H
#define MR_RAX_INTERNAL_H

#include "mr_rax/rax.h"

int mr_get_normalized_topic(const char* pubtopic, char* topic);
int mr_get_subscribe_topic(const char* subtopic, char* topic, char* share);

static int mr_tokenize_topic(char* topic, char** tokenv);
static int mr_insert_topic(rax* prax, char* topic, size_t len);
static int mr_insert_parent_topic_tree(rax* prax, const char* topic);
static int mr_get_topic_clients(rax* prax, rax* crax, uint8_t* key, size_t key_len);
static int mr_probe_subscriptions(rax* prax, rax* crax, char* topic, int level, char** tokenv, size_t numtokens);

#endif // MR_RAX_INTERNAL_H