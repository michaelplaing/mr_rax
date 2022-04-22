#ifndef MR_RAX_INTERNAL_H
#define MR_RAX_INTERNAL_H

#include "mr_rax/rax.h"

int mr_get_normalized_topic(const char* pubtopic, char* topic, char* topic_key);
int mr_get_subscribe_topic(const char* subtopic, char* topic, char* share, char* topic_key);

static int mr_tokenize_topic(char* topic, char** tokenv);
static int mr_insert_topic_tree(rax* tc_tree, const char* topic);
static int mr_trim_topic(rax* tc_tree, raxIterator* piter, char* topic_key, size_t len);
static int mr_trim_topic_tree(rax* tc_tree, raxIterator* piter, const char* topic, char* topic_key);
static int mr_remove_subscription_tc_tree(rax* tc_tree, const char* subtopic, const uint64_t client);
static int mr_remove_subscription_client_tree(rax* client_tree, const char* subtopic, const uint64_t client);
static int mr_get_topic_clients(rax* tc_tree, raxIterator* iter, rax* client_set, uint8_t* base, size_t base_len);

static int mr_probe_subscriptions(
    rax* tc_tree, raxIterator* iter, rax* client_set, const size_t max_len, char* topic, int level, char** tokenv, size_t numtokens
);

#endif // MR_RAX_INTERNAL_H