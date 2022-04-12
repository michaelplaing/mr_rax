// mr_rax.c

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mr_rax/mr_rax.h"
#include "mr_rax/rax.h"
#include "mr_rax/rax_malloc.h"
#include "rax_internal.h"
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

int mr_get_normalized_topic(const char* topic_in, char* topic, char* topic_key) {
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
    topic_key[0] = '\0';
    for (int i = 0; i < numtokens; i++) {
        strlcat(topic_key, tokenv[i], MAX_TOPIC_LEN);
        strlcat(topic, tokenv[i], MAX_TOPIC_LEN);
        strcat(topic, "/");
    }

    topic[strlen(topic) - 1] = '\0';
    return 0;
}

int mr_get_subscribe_topic(const char* subtopic, char* topic, char* share, char* topic_key) {
    size_t stlen = strlen(subtopic);
    char subtopic2[stlen + 1];

    if (strncmp("$share/", subtopic, 7)) {
        strlcpy(subtopic2, subtopic, stlen + 1);
    }
    else {
        char subtopic3[stlen + 1];
        strlcpy(subtopic3, subtopic + 7, stlen + 1);
        char* pc = strchr(subtopic3, '/');
        *pc = '\0';
        strlcpy(share, subtopic3, stlen + 1);
        strlcpy(subtopic2, pc + 1, stlen + 1);
    }

    mr_get_normalized_topic(subtopic2, topic, topic_key);
    return 0;
}

static int mr_insert_topic_tree(rax* tc_tree, const char* topic) {
    size_t tlen = strlen(topic);
    char* tokenv[MAX_TOKENS];
    char topic3[tlen + 1];
    strlcpy(topic3, topic, tlen + 1);
    size_t numtokens = mr_tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    char topic_key[tlen + 1];
    topic_key[0] = '\0';

    for (int i = 0; i < numtokens; i++) {
        strlcat(topic_key, tokenv[i], tlen + 1);
        raxTryInsert(tc_tree, (uint8_t*)topic_key, strlen(topic_key), NULL, NULL);
    }

    return 0;
}

int mr_insert_subscription(rax* tc_tree, rax* client_tree, const char* subtopic, const uint64_t client) {
    size_t stlen = strlen(subtopic);
    char topic[stlen + 3];
    char share[stlen + 1];
    share[0] = '\0';
    char topic_key[stlen + 3];
    mr_get_subscribe_topic(subtopic, topic, share, topic_key);
    size_t tlen = strlen(topic);
    size_t slen = strlen(share);
    size_t tklen = strlen(topic_key);

    mr_insert_topic_tree(tc_tree, topic);

    // insert sub/client in subscription subtree
    size_t tklen2 = tklen + 1 + slen;
    char topic_key2[tklen2 + 8];
    strlcpy(topic_key2, topic_key, tklen + 1);

    if (slen) { // shared subscription sub-hierarchy
        memcpy((void*)topic_key2 + tklen, &shared_mark, 1);
        raxTryInsert(tc_tree, (uint8_t*)topic_key2, tklen + 1, NULL, NULL);
        memcpy((void*)topic_key2 + tklen + 1, (void*)share, slen);
    }
    else { // regular subscription sub-hierarchy
        memcpy((void*)topic_key2 + tklen, &client_mark, 1);
    }

    raxTryInsert(tc_tree, (uint8_t*)topic_key2, tklen2, NULL, NULL);

    // get the client bytes in network order (big endian)
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;

    memcpy((void*)topic_key2 + tklen2, (void*)clientv, 8);
    raxInsert(tc_tree, (uint8_t*)topic_key2, tklen2 + 8, NULL, NULL); // insert the client

    // invert
    char topic3[8 + 4 + stlen];
    memcpy(topic3, clientv, 8);
    raxTryInsert(client_tree, (uint8_t*)topic3, 8, NULL, NULL);
    memcpy(topic3 + 8, "subs", 4);
    raxTryInsert(client_tree, (uint8_t*)topic3, 8 + 4, NULL, NULL);
    memcpy(topic3 + 8 + 4, subtopic, stlen);
    raxTryInsert(client_tree, (uint8_t*)topic3, 8 + 4 + stlen, NULL, NULL);

    return 0;
}

static int mr_trim_topic(rax* tc_tree, raxIterator* piter, char* topic_key, size_t len) {
    raxSeekChildren(piter, (uint8_t*)topic_key, len);
    if (raxNextChild(piter)) return 0; // has children still
    raxRemove(tc_tree, (uint8_t*)topic_key, len, NULL);
    return 1;
}

static int mr_trim_topic_tree(rax* tc_tree, raxIterator* piter, const char* topic, char* topic_key) {
    size_t tlen = strlen(topic);
    char topic3[tlen + 1];
    strlcpy(topic3, topic, tlen + 1);
    char* tokenv[MAX_TOKENS];
    int numtokens = mr_tokenize_topic(topic3, tokenv);
    int count = 0;
    size_t len = strlen(topic_key);

    for (int i = (numtokens - 1); i >= 0; i--, count++) {
        if (!mr_trim_topic(tc_tree, piter, topic_key, len)) return count;
        len -= strlen(tokenv[i]);
    }

    return count;
}

int mr_remove_subscription(rax* tc_tree, rax* client_tree, const char* subtopic, const uint64_t client) {
    size_t stlen = strlen(subtopic);
    char topic[stlen + 3];
    char share[stlen + 1];
    share[0] = '\0';
    char topic_key[stlen + 3];

    mr_get_subscribe_topic(subtopic, topic, share, topic_key);
    size_t tlen = strlen(topic);
    size_t slen = strlen(share);
    size_t tklen = strlen(topic_key);
    size_t tklen2 = tklen + 1 + slen;
    char topic_key2[tklen2 + 8];
    strlcpy(topic_key2, topic_key, tklen + 1);

    if (slen) { // shared subscription sub-hierarchy
        memcpy((void*)topic_key2 + tklen, &shared_mark, 1);
        memcpy((void*)topic_key2 + tklen + 1, (void*)share, slen);
    }
    else { // regular subscription sub-hierarchy
        memcpy((void*)topic_key2 + tklen, &client_mark, 1);
    }

    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;
    memcpy((void*)topic_key2 + tklen2, (void*)clientv, 8);

    raxIterator iter;
    raxStart(&iter, tc_tree);

    if (raxRemove(tc_tree, (uint8_t*)topic_key2, tklen2 + 8, NULL)) { // found
        // trim hierarchy as long as there are no more child keys
        if (mr_trim_topic(tc_tree, &iter, topic_key2, tklen2)) { // client mark or share
            int trimmed = 1;
            if (slen) trimmed = mr_trim_topic(tc_tree, &iter, topic_key2, tlen + 1); // shared mark
            if (trimmed) mr_trim_topic_tree(tc_tree, &iter, topic, topic_key);
        }
    }

    // remove inversion
    raxStart(&iter, client_tree);
    char topic3[8 + 4 + stlen];
    memcpy(topic3, clientv, 8);
    memcpy(topic3 + 8, "subs", 4);
    memcpy(topic3 + 8 + 4, subtopic, stlen);

    if (raxRemove(client_tree, (uint8_t*)topic3, 8 + 4 + stlen, NULL)) { // found
        if (mr_trim_topic(client_tree, &iter, topic3, 8 + 4)) {
            mr_trim_topic(client_tree, &iter, topic3, 8);
        }
    }

    raxStop(&iter);
    return 0;
}

int mr_remove_client_subscriptions(rax* tc_tree, rax* client_tree, const uint64_t client) {
    rax* srax = raxNew();
    raxIterator iter;
    raxStart(&iter, client_tree);
    uint8_t clientplusv[8 + 4];
    for (int i = 0; i < 8; i++) clientplusv[i] = client >> ((7 - i) * 8) & 0xff;
    memcpy(clientplusv + 8, "subs", 4);
    raxSeekChildren(&iter, clientplusv, 8 + 4);
    while(raxNextChild(&iter)) raxInsert(srax, iter.key, iter.key_len, NULL, NULL);
    raxStart(&iter, srax);
    raxSeekSet(&iter);

    while(raxNextInSet(&iter)) {
        size_t stlen = iter.key_len - (8 + 4);
        char subtopic[stlen + 1];
        memcpy(subtopic, iter.key + 8 + 4, stlen);
        subtopic[stlen] = '\0';
        mr_remove_subscription(tc_tree, client_tree, subtopic, client);
    }

    raxStop(&iter);
    raxFree(srax);
    return 0;
}

static int mr_get_topic_clients(rax* tc_tree, rax* srax, uint8_t* key, size_t key_len) {
    raxIterator iter;
    raxStart(&iter, tc_tree);
    raxIterator iter2;
    raxStart(&iter2, tc_tree);

    // get regular subs
    key[key_len] = client_mark;
    raxSeekChildren(&iter, key, key_len + 1);
    while(raxNextChild(&iter)) raxTryInsert(srax, iter.key + iter.key_len - 8, 8, NULL, NULL);

    // get shared subs
    key[key_len] = shared_mark;
    raxSeekChildren(&iter, key, key_len + 1);

    while(raxNextChild(&iter)) { // randomly pick one client per share
        raxSeekChildren(&iter2, iter.key, iter.key_len);
        int count;
        for (count = 0; raxNextChild(&iter2); count++);

        if (count) {
            int choice = arc4random() % count;
            raxSeekChildren(&iter2, iter.key, iter.key_len);
            for (int i = 0; i < (choice + 1) ; i++) raxNextChild(&iter2);
            raxTryInsert(srax, iter2.key + iter2.key_len - 8, 8, NULL, NULL);
        }
    }

    raxStop(&iter2);
    raxStop(&iter);
    return 0;
}

static int mr_probe_subscriptions(
    rax* tc_tree, rax* srax, const size_t max_len, char* topic_key, int level, char** tokenv, size_t numtokens
) {
    char topic_key2[max_len];
    char* token = tokenv[level];

    while (level < numtokens) {
        strlcat(topic_key, token, max_len);

        snprintf(topic_key2, max_len, "%s#", topic_key);
        if (raxFind(tc_tree, (uint8_t*)topic_key2, strlen(topic_key2)) != raxNotFound) {
            mr_get_topic_clients(tc_tree, srax, (uint8_t*)topic_key2, strlen(topic_key2));
        }

        if (level == (numtokens - 1)) break; // only '#' is valid at this level

        snprintf(topic_key2, max_len, "%s+", topic_key);
        if (raxFind(tc_tree, (uint8_t*)topic_key2, strlen(topic_key2)) != raxNotFound) {
            if (level == (numtokens - 2)) {
                mr_get_topic_clients(tc_tree, srax, (uint8_t*)topic_key2, strlen(topic_key2));
            }
            else {
                char *token2v[numtokens];
                for (int i = 0; i < numtokens; i++) token2v[i] = tokenv[i];
                token2v[level + 1] = "+";
                topic_key2[strlen(topic_key2) - 1] = '\0'; // trim the '+'
                mr_probe_subscriptions(tc_tree, srax, max_len, topic_key2, level + 1, token2v, numtokens);
            }
        }

        token = tokenv[level + 1];
        snprintf(topic_key2, max_len, "%s%s", topic_key, token);
        if (raxFind(tc_tree, (uint8_t*)topic_key2, strlen(topic_key2)) != raxNotFound) {
            if (level == (numtokens - 2)) mr_get_topic_clients(tc_tree, srax, (uint8_t*)topic_key2, strlen(topic_key2));
        }
        else {
            break; // no more possible matches
        }

        level++;
    }

    return 0;
}

int mr_get_subscribed_clients(rax* tc_tree, rax* srax, const char* pubtopic) {
    char* tokenv[MAX_TOKENS];
    size_t ptlen = strlen(pubtopic);
    char topic[ptlen + 3];
    char topic_key[ptlen + 3];
    mr_get_normalized_topic(pubtopic, topic, topic_key);
    size_t tlen = strlen(topic);
    char topic3[tlen + 1];
    strlcpy(topic3, topic, tlen + 1);
    size_t numtokens = mr_tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    topic_key[0] = '\0';
    int level = 0;
    mr_probe_subscriptions(tc_tree, srax, tlen + 1, topic_key, level, tokenv, numtokens);
    return 0;
}

int mr_upsert_client_topic_alias(
    rax* client_tree, const uint64_t client, const bool isclient, const char* pubtopic, const uint8_t alias
) {
    size_t ptlen = strlen(pubtopic);
    char* source = isclient ? "client" : "server";
    char topicbyalias[8 + 16 + 1];       // <Client ID>"aliasesclienttba"<alias>
    char aliasbytopic[8 + 16 + ptlen];   // <Client ID>"aliasesclientatb"<pubtopic>
    for (int i = 0; i < 8; i++) topicbyalias[i] = client >> ((7 - i) * 8) & 0xff;

    // common: 7 + 6 + 3 = 16
    raxTryInsert(client_tree, (uint8_t*)topicbyalias, 8, NULL, NULL);
    memcpy(topicbyalias + 8, "aliases", 7);
    raxTryInsert(client_tree, (uint8_t*)topicbyalias, 8 + 7, NULL, NULL);
    memcpy(topicbyalias + 8 + 7, source, 6);
    raxTryInsert(client_tree, (uint8_t*)topicbyalias, 8 + 7 + 6, NULL, NULL);
    memcpy(aliasbytopic, topicbyalias, 8 + 7 + 6);

    memcpy(topicbyalias + 8 + 7 + 6, "tba", 3);
    raxTryInsert(client_tree, (uint8_t*)topicbyalias, 8 + 16, NULL, NULL);
    memcpy(aliasbytopic + 8 + 7 + 6, "abt", 3);
    raxTryInsert(client_tree, (uint8_t*)aliasbytopic, 8 + 16, NULL, NULL);

    // inversion pair
    memcpy(topicbyalias + 8 + 16, &alias, 1);
    memcpy(aliasbytopic + 8 + 16, pubtopic, ptlen);

    // clear topicbyalias & inversion if necessary
    char* pubtopic2;
    if (raxRemove(client_tree, (uint8_t*)topicbyalias, 8 + 16 + 1, (void**)&pubtopic2)) {
        size_t ptlen2 = strlen(pubtopic2);
        memcpy(aliasbytopic + 8 + 16, pubtopic2, ptlen2);
        raxRemoveScalar(client_tree, (uint8_t*)aliasbytopic, 8 + 16 + ptlen2, NULL);
        rax_free(pubtopic2);
        memcpy(aliasbytopic + 8 + 16, pubtopic, ptlen); // restore
    }

    // clear aliasbytopic & inversion if necessary
    uintptr_t old_bigalias;
    if (raxRemoveScalar(client_tree, (uint8_t*)aliasbytopic, 8 + 16 + ptlen, &old_bigalias)) {
        uint8_t old_alias = old_bigalias & 0xff;
        memcpy(topicbyalias + 8 + 16, &old_alias, 1);
        char* pubtopic3;
        raxRemove(client_tree, (uint8_t*)topicbyalias, 8 + 16 + 1, (void**)&pubtopic3);
        rax_free(pubtopic3);
        memcpy(topicbyalias + 8 + 16, &alias, 1); // restore
    }

    // insert inversion pair
    char* pubtopic4 = strdup(pubtopic); // free on removal
    raxInsert(client_tree, (uint8_t*)topicbyalias, 8 + 16 + 1, pubtopic4, NULL);
    raxInsertScalar(client_tree, (uint8_t*)aliasbytopic, 8 + 16 + ptlen, alias, NULL);

    return 0;
}

int mr_remove_client_topic_aliases(rax* client_tree, const uint64_t client) {
    uint8_t clientplusv[8 + 7];
    for (int i = 0; i < 8; i++) clientplusv[i] = client >> ((7 - i) * 8) & 0xff;
    memcpy(clientplusv + 8, "aliases", 7);
    raxFreeSubtreeWithCallback(client_tree, clientplusv, 8 + 7, rax_free);
    return 0;
}

int mr_get_alias_by_topic(rax* client_tree, const uint64_t client, const bool isclient, const char* pubtopic, uint8_t* palias) {
    size_t ptlen = strlen(pubtopic);
    char aliasbytopic[8 + 16 + ptlen + 1]; // <Client ID>"aliasesclientabt"<pubtopic>\0
    for (int i = 0; i < 8; i++) aliasbytopic[i] = client >> ((7 - i) * 8) & 0xff;
    char* source = isclient ? "client" : "server";
    snprintf(aliasbytopic + 8, 16 + ptlen + 1, "aliases%sabt%.*s", source, (int)ptlen, pubtopic);
    void* value = raxFind(client_tree, (uint8_t*)aliasbytopic, 8 + 16 + ptlen);
    *palias = value == raxNotFound ? 0 : (uintptr_t)value & 0xff; // 0 is an invalid alias
    return 0;
}

int mr_get_topic_by_alias(rax* client_tree, const uint64_t client, const bool isclient, const uint8_t alias, char** ppubtopic) {
    char topicbyalias[8 + 16 + 1]; // <Client ID>"aliasesclienttba"<alias>\0
    for (int i = 0; i < 8; i++) topicbyalias[i] = client >> ((7 - i) * 8) & 0xff;
    char* source = isclient ? "client" : "server";
    snprintf(topicbyalias + 8, 16 + 1, "aliases%stba", source);
    topicbyalias[8 + 16] = alias;
    void* value = raxFind(client_tree, (uint8_t*)topicbyalias, 8 + 16 + 1);
    *ppubtopic = value == raxNotFound ? NULL : value;
    return 0;
}

int mr_remove_client_data(rax* tc_tree, rax* client_tree, uint64_t client) {
    mr_remove_client_subscriptions(tc_tree, client_tree, client);
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;
    raxFreeSubtreeWithCallback(client_tree, clientv, 8, rax_free);
    return 0;
}
