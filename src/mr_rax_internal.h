#ifndef MR_RAX_H
#define MR_RAX_H

// #include <stdio.h>
// #include <stdint.h>
// #include <stdlib.h>
// #include <string.h>
// #include <errno.h>

// #include "mr_rax/mr_rax.h"
#include "mr_rax/rax.h"

static int tokenize_topic(char* topic, char** tokenv);
static int get_normalized_topic(const char* topic_in, char* topic);
static int get_subscribe_topic(const char* subtopic, char* topic, char* share);
static int upsert_topic(rax* prax, char* topic, size_t len);
static int upsert_parent_topic_tree(rax* prax, const char* topic);
static int get_topic_clients(rax* prax, rax* crax, uint8_t* key, size_t key_len);
static int probe_subscriptions(rax* prax, rax* crax, char* topic, int level, char** tokenv, size_t numtokens);

#endif // MR_RAX_H