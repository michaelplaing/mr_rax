// mr_rax.c

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mr_rax/mr_rax.h"
#include "mr_rax/rax.h"
#include "mr_rax_internal.h"

static int mr_tokenize_topic(char* topic, char** tokenv) {
    int numtokens;

    for (numtokens = 0; numtokens < MAX_TOKENS; numtokens++, tokenv++) {
        *tokenv = strsep(&topic, "/");
        if (*tokenv == NULL) break;
        if (*tokenv[0] == '\0') *tokenv = (char*)&empty_tokenv;
    }

    return numtokens;
}

static int mr_get_normalized_topic(const char* topic_in, char* topic) {
    size_t tlen = strlen(topic_in);
    char topic2[tlen + 3];

    if (topic_in[0] == '$') {
        snprintf(topic2, tlen + 3, "$/%s", topic_in);
    }
    else {
        snprintf(topic2, tlen + 3, "@/%s", topic_in);
    }

    char* tokenv[MAX_TOKENS];
    int numtokens = mr_tokenize_topic(topic2, tokenv);

    topic[0] = '\0';
    for (int i = 0; i < numtokens; i++) {
        strlcat(topic, tokenv[i], MAX_TOPIC_LEN);
        strcat(topic, "/");
    }

    topic[strlen(topic) - 1] = '\0';
    return 0;
}

static int mr_get_subscribe_topic(const char* subtopic, char* topic, char* share) {
    size_t tlen_in = strlen(subtopic);
    char subtopic2[tlen_in + 1];

    if (strncmp("$share/", subtopic, 7)) {
        strlcpy(subtopic2, subtopic, tlen_in + 1);
    }
    else {
        char subtopic3[tlen_in + 1];
        strlcpy(subtopic3, subtopic + 7, tlen_in + 1);
        char* pc = strchr(subtopic3, '/');
        *pc = '\0';
        strlcpy(share, subtopic3, tlen_in + 1);
        strlcpy(subtopic2, pc + 1, tlen_in + 1);
    }

    mr_get_normalized_topic(subtopic2, topic);
    return 0;
}

static int mr_upsert_topic(rax* prax, char* topic, size_t len) {
    uintptr_t client_count; // increment
    int try = raxTryInsert(prax, (uint8_t*)topic, len, (void*)1, (void**)&client_count);
    if (!try) raxInsert(prax, (uint8_t*)topic, len, (void*)(client_count + 1), NULL);
    return 0;
}

static int mr_upsert_parent_topic_tree(rax* prax, const char* topic) {
    size_t tlen = strlen(topic);
    char* tokenv[MAX_TOKENS];
    char topic2[tlen + 1];
    char topic3[tlen + 1];
    strlcpy(topic3, topic, tlen + 1);
    size_t numtokens = mr_tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    topic2[0] = '\0';

    for (int i = 0; i < (numtokens - 1); i++) {
        strlcat(topic2, tokenv[i], tlen + 1);
        mr_upsert_topic(prax, topic2, strlen(topic2));
        strlcat(topic2, "/", tlen + 1);
    }

    return 0;
}

int mr_insert_subscription(rax* prax, const char* subtopic, const uint64_t client) {
    size_t tlen_in = strlen(subtopic);
    char topic[tlen_in + 3];
    char share[tlen_in + 1];
    share[0] = '\0';

    // get topic and share name if any
    mr_get_subscribe_topic(subtopic, topic, share);

    // insert/update topic & parent tree incrementing client counts
    mr_upsert_parent_topic_tree(prax, topic);
    size_t tlen = strlen(topic);
    mr_upsert_topic(prax, topic, tlen);

    // get the client bytes in network order (big endian)
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;

    // insert client
    char topic2[tlen + 1 + 8];
    strlcpy(topic2, topic, tlen + 1);
    size_t slen = strlen(share);

    if (slen) { // shared subscription sub-hierarchy
        memcpy((void*)topic2 + tlen, &shared_mark, 1);
        mr_upsert_topic(prax, topic2, tlen + 1);
        memcpy((void*)topic2 + tlen + 1, (void*)share, slen);
        mr_upsert_topic(prax, topic2, tlen + 1 + slen);
        memcpy((void*)topic2 + tlen + 1 + slen, (void*)clientv, 8);
        raxInsert(prax, (uint8_t*)topic2, tlen + 1 + slen + 8, NULL, NULL); // insert the client
    }
    else { // regular subscription sub-hierarchy
        memcpy((void*)topic2 + tlen, &client_mark, 1);
        mr_upsert_topic(prax, topic2, tlen + 1);
        memcpy((void*)topic2 + tlen + 1, (void*)clientv, 8);
        raxInsert(prax, (uint8_t*)topic2, tlen + 1 + 8, NULL, NULL); // insert the client
    }

    return 0;
}

static int mr_get_topic_clients(rax* prax, rax* crax, uint8_t* key, size_t key_len) {
    raxIterator iter;
    raxStart(&iter, prax);
    raxIterator iter2;
    raxStart(&iter2, prax);

    // get regular subs
    key[key_len] = client_mark;
    raxSeekChildren(&iter, key, key_len + 1);
    while(raxNextChild(&iter)) raxTryInsert(crax, iter.key + iter.key_len - 8, 8, NULL, NULL);

    // get shared subs
    key[key_len] = shared_mark;
    raxSeekChildren(&iter, key, key_len + 1);

    while(raxNextChild(&iter)) { // randomly pick one client per share
        int choice = arc4random() % (uintptr_t)(iter.data); // iter.data is the client count
        raxSeekChildren(&iter2, iter.key, iter.key_len);
        for (int i = 0; raxNextChild(&iter2) && i < choice; i++);
        raxTryInsert(crax, iter2.key + iter2.key_len - 8, 8, NULL, NULL);
    }

    raxStop(&iter2);
    raxStop(&iter);
    return 0;
}

static int mr_probe_subscriptions(rax* prax, rax* crax, char* topic, int level, char** tokenv, size_t numtokens) {
    char topic2[MAX_TOPIC_LEN];
    char* token = tokenv[level];

    while (level < numtokens) {
        strlcat(topic, token, MAX_TOPIC_LEN);

        snprintf(topic2, MAX_TOPIC_LEN, "%s/#", topic);
        if (raxFind(prax, (uint8_t*)topic2, strlen(topic2)) != raxNotFound) {
            mr_get_topic_clients(prax, crax, (uint8_t*)topic2, strlen(topic2));
        }

        if (level == (numtokens - 1)) break; // only '#' is valid at this level

        snprintf(topic2, MAX_TOPIC_LEN, "%s/+", topic);
        if (raxFind(prax, (uint8_t*)topic2, strlen(topic2)) != raxNotFound) {
            if (level == (numtokens - 2)) {
                mr_get_topic_clients(prax, crax, (uint8_t*)topic2, strlen(topic2));
            }
            else {
                char *token2v[numtokens];
                for (int i = 0; i < numtokens; i++) token2v[i] = tokenv[i];
                token2v[level + 1] = "+";
                topic2[strlen(topic2) - 1] = '\0'; // trim the '+'
                mr_probe_subscriptions(prax, crax, topic2, level + 1, token2v, numtokens);
            }
        }

        token = tokenv[level + 1];
        snprintf(topic2, MAX_TOPIC_LEN, "%s/%s", topic, token);
        if (
            raxFind(prax, (uint8_t*)topic2, strlen(topic2)) != raxNotFound &&
            level == (numtokens - 2)
        )  mr_get_topic_clients(prax, crax, (uint8_t*)topic2, strlen(topic2));

        strlcat(topic, "/", MAX_TOPIC_LEN);
        level++;
    }

    return 0;
}

int mr_get_clients(rax* prax, rax* crax, char* pubtopic) {
    char* tokenv[MAX_TOKENS];
    char topic[MAX_TOPIC_LEN];
    char topic2[MAX_TOPIC_LEN];
    char topic3[MAX_TOPIC_LEN];
    mr_get_normalized_topic(pubtopic, topic);
    strlcpy(topic3, topic, MAX_TOPIC_LEN);
    size_t numtokens = mr_tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    topic2[0] = '\0';
    int level = 0;
    mr_probe_subscriptions(prax, crax, topic2, level, tokenv, numtokens);
    return 0;
}