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

static int mr_makebytecount_VBI(uint64_t u64) {
    int i = 0;
    do {
        u64 = u64 >> 7;
        i++;
    } while (u64);

    return i; // number of bytes in result
}

static int mr_make_VBI(uint64_t u64, uint8_t *u8v0) { // u8v0 >= 10 bytes
    uint8_t *pu8 = u8v0;
    int i = 0;
    do {
        *pu8 = u64 & 0x7F;
        u64 = u64 >> 7;
        if (u64) *pu8 |= 0x80;
        i++;
        pu8++;
    } while (u64);

    return i; // number of bytes in result
}

static int mr_extractbytecount_VBI(uint8_t *u8v0) { // u8v0 >= 10 bytes
    uint8_t *pu8 = u8v0;
    int i;
    for (i = 0; i < 10; pu8++, i++) if (!(*pu8 & 0x80)) break;

    if (i == 10) return 0; // overflow: byte[9] has a continuation bit
    else return i + 1; // number of bytes consumed
}

static int mr_extract_VBI(uint8_t *u8v0, uint64_t *pu64) { // u8v0 >= 10 bytes
    uint8_t *pu8 = u8v0;
    uint64_t u64 = 0;
    int i;

    for (i = 0; i < 10; pu8++, i++){
        u64 += (*pu8 & 0x7F) << (7 * i);
        if (!(*pu8 & 0x80)) break;
    }

    if (i == 10) { // overflow: byte[9] has a continuation bit
        return 0;
    }
    else {
        *pu64 = u64;
        return i + 1; // number of bytes consumed
    }
}

int mr_next_vbi(raxIterator* piter, uint64_t* pu64) {
    if (!raxNext(piter)) return 0;
    mr_extract_VBI(piter->key, pu64);
    return 1;
}

static int mr_tokenize_topic(char* topic, char** tokenv) {
    int numtokens;

    for (numtokens = 0; numtokens < MAX_TOKENS; numtokens++, tokenv++) {
        *tokenv = strsep(&topic, "/");
        if (*tokenv == NULL) break;
        if (*tokenv[0] == '\0') *tokenv = empty_tokenv;
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
    if (topic_key) topic_key[0] = '\0';
    for (int i = 0; i < numtokens; i++) {
        if (topic_key) strlcat(topic_key, tokenv[i], MAX_TOPIC_LEN);
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

static int mr_insert_topic_tree(rax* topic_tree, const char* topic) {
    size_t tlen = strlen(topic);
    char* tokenv[MAX_TOKENS];
    char topic3[tlen + 1];
    strlcpy(topic3, topic, tlen + 1);
    size_t numtokens = mr_tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    char topic_key[tlen + 1];
    topic_key[0] = '\0';

    for (int i = 0; i < numtokens; i++) {
        strlcat(topic_key, tokenv[i], tlen + 1);
        raxTryInsert(topic_tree, (uint8_t*)topic_key, strlen(topic_key), NULL, NULL);
    }

    return 0;
}

int mr_insert_subscription(rax* topic_tree, rax* client_tree, const char* subtopic, const uint64_t client) {
    size_t stlen = strlen(subtopic);
    char topic[stlen + 3];
    char share[stlen + 1];
    share[0] = '\0';
    char topic_key[stlen + 3];
    mr_get_subscribe_topic(subtopic, topic, share, topic_key);
    size_t tlen = strlen(topic);
    size_t slen = strlen(share);
    size_t tklen = strlen(topic_key);
    size_t clen = mr_makebytecount_VBI(client);

    mr_insert_topic_tree(topic_tree, topic);

    // insert sub/client in subscription subtree
    size_t tklen2 = tklen + 1 + slen + (slen ? 1 : 0);
    uint8_t topic_key2[tklen2 + clen];
    memcpy(topic_key2, topic_key, tklen);

    if (slen) { // shared subscription sub-hierarchy
        memcpy(topic_key2 + tklen, &shared_mark, 1);
        raxTryInsert(topic_tree, topic_key2, tklen + 1, NULL, NULL);
        memcpy(topic_key2 + tklen + 1, (void*)share, slen);
        raxTryInsert(topic_tree, topic_key2, tklen + 1 + slen, NULL, NULL);
        topic_key2[tklen + 1 + slen] = client_mark;
        raxTryInsert(topic_tree, topic_key2, tklen2, NULL, NULL);
    }
    else { // regular subscription sub-hierarchy
        memcpy(topic_key2 + tklen, &client_mark, 1);
        raxTryInsert(topic_tree, topic_key2, tklen + 1, NULL, NULL);
    }

    // get the client bytes in network order (big endian) as a Variable Byte Integer (VBI)
    uint8_t clientv[10]; // 64 bits @ 7 bits per byte => 10 bytes max needed
    mr_make_VBI(client, clientv);
    memcpy(topic_key2 + tklen2, clientv, clen);
    raxInsert(topic_tree, topic_key2, tklen2 + clen, NULL, NULL); // insert the client

    // invert
    uint8_t topic3[clen + 4 + stlen];
    memcpy(topic3, clientv, clen);
    raxTryInsert(client_tree, topic3, clen, NULL, NULL);
    memcpy(topic3 + clen, "subs", 4);
    raxTryInsert(client_tree, topic3, clen + 4, NULL, NULL);
    memcpy(topic3 + clen + 4, subtopic, stlen);
    raxTryInsert(client_tree, topic3, clen + 4 + stlen, NULL, NULL);

    return 0;
}

static int mr_trim_topic(rax* topic_tree, raxIterator* piter, uint8_t* topic_key, size_t len) {
    raxSeekChildren(piter, topic_key, len);
    if (raxNextChild(piter)) return 0; // has children still
    raxRemove(topic_tree, topic_key, len, NULL);
    return 1;
}

static int mr_trim_topic_tree(rax* topic_tree, raxIterator* piter, const char* topic, char* topic_key) {
    size_t tlen = strlen(topic);
    char topic3[tlen + 1];
    strlcpy(topic3, topic, tlen + 1);
    char* tokenv[MAX_TOKENS];
    int numtokens = mr_tokenize_topic(topic3, tokenv);
    int count = 0;
    size_t len = strlen(topic_key);

    for (int i = (numtokens - 1); i >= 0; i--, count++) {
        if (!mr_trim_topic(topic_tree, piter, (uint8_t*)topic_key, len)) return count;
        len -= strlen(tokenv[i]);
    }

    return count;
}

static int mr_remove_subscription_tc_tree(rax* topic_tree, const char* subtopic, const uint8_t* clientv, const size_t clen) {
    size_t stlen = strlen(subtopic);
    char topic[stlen + 3];
    char share[stlen + 1];
    share[0] = '\0';
    char topic_key[stlen + 3];

    mr_get_subscribe_topic(subtopic, topic, share, topic_key);
    size_t tlen = strlen(topic);
    size_t slen = strlen(share);
    size_t tklen = strlen(topic_key);
    size_t tklen2 = tklen + 1 + slen + (slen ? 1 : 0);
    uint8_t topic_key2[tklen2 + clen];
    memcpy(topic_key2, topic_key, tklen);

    if (slen) { // shared subscription sub-hierarchy
        memcpy(topic_key2 + tklen, &shared_mark, 1);
        memcpy(topic_key2 + tklen + 1, (void*)share, slen);
        memcpy(topic_key2 + tklen + 1 + slen, &client_mark, 1);
    }
    else { // regular subscription sub-hierarchy
        memcpy(topic_key2 + tklen, &client_mark, 1);
    }

    memcpy(topic_key2 + tklen2, clientv, clen);

    if (raxRemove(topic_tree, topic_key2, tklen2 + clen, NULL)) { // regular or share client
        raxIterator iter;
        raxStart(&iter, topic_tree);

        // trim hierarchy as long as there are no more child keys
        if (mr_trim_topic(topic_tree, &iter, topic_key2, tklen2)) { // regular or share client mark
            int trimmed = 1;

            if (slen) {
                trimmed = mr_trim_topic(topic_tree, &iter, topic_key2, tlen + 1 + slen); // share
                if (trimmed) trimmed = mr_trim_topic(topic_tree, &iter, topic_key2, tlen + 1); // shared mark
            }

            if (trimmed) mr_trim_topic_tree(topic_tree, &iter, topic, topic_key); // topic
        }

        raxStop(&iter);
    }

    return 0;
}

static int mr_remove_subscription_client_tree(rax* client_tree, const char* subtopic, const uint8_t* clientv, size_t clen) {
    size_t stlen = strlen(subtopic);
    raxIterator iter;
    raxStart(&iter, client_tree);
    uint8_t inversion[clen + 4 + stlen];
    memcpy(inversion, clientv, clen);
    memcpy(inversion + clen, "subs", 4);
    memcpy(inversion + clen + 4, subtopic, stlen);

    if (raxRemove(client_tree, (uint8_t*)inversion, clen + 4 + stlen, NULL)) { // found
        if (mr_trim_topic(client_tree, &iter, inversion, clen + 4)) {
            mr_trim_topic(client_tree, &iter, inversion, clen);
        }
    }

    raxStop(&iter);
    return 0;
}

int mr_remove_subscription(rax* topic_tree, rax* client_tree, const char* subtopic, const uint64_t client) {
    // get the client bytes in network order (big endian) as a Variable Byte Integer (VBI)
    uint8_t clientv[10]; // 64 bits @ 7 bits per byte => 10 bytes max needed
    size_t clen = mr_make_VBI(client, clientv);
    mr_remove_subscription_tc_tree(topic_tree, subtopic, clientv, clen);
    mr_remove_subscription_client_tree(client_tree, subtopic, clientv, clen);
    return 0;
}

int mr_remove_client_subscriptions(rax* topic_tree, rax* client_tree, const uint64_t client) {
    rax* srax = raxNew();
    raxIterator iter;
    raxStart(&iter, client_tree);

    // get the client bytes in network order (big endian) as a Variable Byte Integer (VBI)
    uint8_t clientv[10]; // 64 bits @ 7 bits per byte => 10 bytes max needed
    size_t clen = mr_make_VBI(client, clientv);

    uint8_t inversion[clen + 4];
    memcpy(inversion, clientv, clen);
    memcpy(inversion + clen, "subs", 4);
    raxSeekChildren(&iter, inversion, clen + 4);
    while(raxNextChild(&iter)) raxInsert(srax, iter.key + clen + 4, iter.key_len - (clen + 4), NULL, NULL);
    raxStart(&iter, srax);
    raxSeek(&iter, "^", NULL, 0);

    while(raxNext(&iter)) {
        char subtopic[iter.key_len + 1];
        memcpy(subtopic, iter.key, iter.key_len);
        subtopic[iter.key_len] = '\0';
        mr_remove_subscription_tc_tree(topic_tree, subtopic, clientv, clen);
    }

    raxFree(srax);
    raxFreeSubtree(client_tree, inversion, clen + 4);
    raxStart(&iter, client_tree);
    mr_trim_topic(client_tree, &iter, inversion, clen);
    raxStop(&iter);
    return 0;
}

static int mr_get_topic_clients(raxIterator* iter, rax* srax, uint8_t* key, size_t key_len) {
    // printf("mr_get_topic_clients:: key: '%.*s'; key_len: %zu\n", (int)key_len, key, key_len);

    // get regular subs
    key[key_len] = client_mark;
    raxSeekSubtreeRelative(iter, key, key_len + 1);

    if (raxNext(iter)) { // subtree exists? Skip 1st key if it does
        while(raxNext(iter)) {
            // printf("mr_get_topic_clients (regular):: iter->key: '%.*s'\n", (int)iter->key_len, iter->key);
            size_t clen = iter->key_len - (key_len + 1);
            raxTryInsert(srax, iter->key + key_len + 1, clen, NULL, NULL);
        }
    }

    // get shared subs
    key[key_len] = shared_mark;

    raxSeekSubtreeRelative(iter, key, key_len + 1);

    if (raxNext(iter)) { // same
        raxIterator* piter2 = raxIteratorDup(iter);

        while(raxNext(iter)) { // randomly pick one client per share
            // printf("mr_get_topic_clients (shared):: iter->key: '%.*s'\n", (int)iter->key_len, iter->key);
            if (!memchr(iter->key, 0xfe, iter->key_len) || iter->key[iter->key_len - 1] != 0xff) continue; // only keys w/client marks
            // printf("mr_get_topic_clients (shared & selected):: iter->key: '%.*s'\n", (int)iter->key_len, iter->key);
            raxSeekSubtreeRelative(piter2, iter->key, iter->key_len);

            if (raxNext(piter2)) { // same - should succeed tho
                size_t count = 0;
                while (raxNext(piter2)) {
                    // printf("mr_get_topic_clients (counted):: piter2->key: '%.*s'\n", (int)piter2->key_len, piter2->key);
                    count++;
                }

                if (count) {
                    int choice = arc4random() % count;
                    raxSeekSubtreeRelative(piter2, iter->key, iter->key_len);

                    if (raxNext(piter2)) { // same
                        for (int i = 0; i < (choice + 1); i++) raxNext(piter2);
                        size_t clen = piter2->key_len - iter->key_len;
                        raxTryInsert(srax, piter2->key + iter->key_len, clen, NULL, NULL);
                    }
                }
            }
        }

        raxStop(piter2);
    }

    return 0;
}

static int mr_probe_subscriptions(
    raxIterator* iter, rax* srax, const size_t max_len, char* topic, int level, char** tokenv, size_t numtokens
) {
    char test_key[max_len];
    char* token;

    while (level < numtokens) {
        snprintf(test_key, max_len, "%s#", topic);
        if (raxFindRelative(iter, (uint8_t*)test_key, strlen(test_key)) != raxNotFound) {
            mr_get_topic_clients(iter, srax, (uint8_t*)test_key, strlen(test_key));
        }

        if (level == (numtokens - 1)) break; // only '#' is valid at this level

        snprintf(test_key, max_len, "%s+", topic);
        if (raxFindRelative(iter, (uint8_t*)test_key, strlen(test_key)) != raxNotFound) {
            if (level == (numtokens - 2)) {
                mr_get_topic_clients(iter, srax, (uint8_t*)test_key, strlen(test_key));
            }
            else {
                char *token2v[numtokens];
                for (int i = 0; i < numtokens; i++) token2v[i] = tokenv[i];
                token2v[level + 1] = "+";
                mr_probe_subscriptions(iter, srax, max_len, test_key, level + 1, token2v, numtokens);
            }
        }

        token = tokenv[level + 1];
        strlcat(topic, token, max_len);
        strlcpy(test_key, topic, max_len);
        if (raxFindRelative(iter, (uint8_t*)test_key, strlen(test_key)) != raxNotFound) {
            if (level == (numtokens - 2)) {
                mr_get_topic_clients(iter, srax, (uint8_t*)test_key, strlen(topic));
            }
        }
        else {
            break; // no more possible matches
        }

        level++;
    }

    return 0;
}

int mr_get_subscribed_clients(rax* topic_tree, rax* srax, const char* pubtopic) {
    char* tokenv[MAX_TOKENS];
    size_t ptlen = strlen(pubtopic);
    char topic[ptlen + 3];
    mr_get_normalized_topic(pubtopic, topic, NULL);
    size_t tlen = strlen(topic);
    char topic3[tlen + 1];
    strlcpy(topic3, topic, tlen + 1);
    size_t numtokens = mr_tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    raxIterator iter;
    raxStart(&iter, topic_tree);
    strlcpy(topic, tokenv[0], tlen + 1);

    if (raxFindRelative(&iter, (uint8_t*)topic, strlen(topic)) != raxNotFound) {
        mr_probe_subscriptions(&iter, srax, tlen + 1, topic, 0, tokenv, numtokens);
    }

    raxStop(&iter);
    return 0;
}

int mr_upsert_client_topic_alias(
    rax* client_tree, const uint64_t client, const bool isclient, const char* pubtopic, const uint8_t alias
) {
    size_t ptlen = strlen(pubtopic);
    char* source = isclient ? "client" : "server";
    char topicbyalias[24 + 1 + ptlen]; // <Client ID>"aliasesclienttba"<alias><pubtopic>
    char aliasbytopic[24 + ptlen + 1]; // <Client ID>"aliasesclientabt"<pubtopic><alias>
    for (int i = 0; i < 8; i++) topicbyalias[i] = client >> ((7 - i) * 8) & 0xff;

    // common: 7 + 6 + 3 = 16
    raxTryInsert(client_tree, (uint8_t*)topicbyalias, 8, NULL, NULL);
    memcpy(topicbyalias + 8, "aliases", 7);
    raxTryInsert(client_tree, (uint8_t*)topicbyalias, 8 + 7, NULL, NULL);
    memcpy(topicbyalias + 8 + 7, source, 6);
    raxTryInsert(client_tree, (uint8_t*)topicbyalias, 8 + 7 + 6, NULL, NULL);
    memcpy(aliasbytopic, topicbyalias, 8 + 7 + 6);

    memcpy(topicbyalias + 8 + 7 + 6, "tba", 3);
    raxTryInsert(client_tree, (uint8_t*)topicbyalias, 24, NULL, NULL);
    memcpy(aliasbytopic + 8 + 7 + 6, "abt", 3);
    raxTryInsert(client_tree, (uint8_t*)aliasbytopic, 24, NULL, NULL);

    memcpy(topicbyalias + 24, &alias, 1);
    raxTryInsert(client_tree, (uint8_t*)topicbyalias, 24 + 1, NULL, NULL);
    memcpy(aliasbytopic + 24, pubtopic, ptlen);
    raxTryInsert(client_tree, (uint8_t*)aliasbytopic, 24 + ptlen, NULL, NULL);

    // inversion pair
    memcpy(topicbyalias + 24 + 1, pubtopic, ptlen);
    memcpy(aliasbytopic + 24 + ptlen, &alias, 1);

    raxIterator iter;
    raxStart(&iter, client_tree);

    if (raxSeekChildren(&iter, (uint8_t*)topicbyalias, 24 + 1) && (raxNextChild(&iter))) {
        size_t ptlen2 = iter.key_len - (24 + 1);
        char aliasbytopic2[24 + ptlen2]; // <Client ID>"aliasesclientabt"<pubtopic>
        memcpy(aliasbytopic2, aliasbytopic, 24);
        memcpy(aliasbytopic2 + 24, iter.key + 24 + 1, ptlen2);
        raxFreeSubtree(client_tree, (uint8_t*)aliasbytopic2, 24 + ptlen2);
        raxRemove(client_tree, iter.key, iter.key_len, NULL);
    }

    if (raxSeekChildren(&iter, (uint8_t*)aliasbytopic, 24 + ptlen) && raxNextChild(&iter)) {
        char topicbyalias2[24 + 1]; // <Client ID>"aliasesclienttba"<alias>
        memcpy(topicbyalias2, topicbyalias, 24);
        memcpy(topicbyalias2 + 24, iter.key + 24 + ptlen, 1);
        raxFreeSubtree(client_tree, (uint8_t*)topicbyalias2, 24 + 1);
        raxRemove(client_tree, iter.key, iter.key_len, NULL);
    }

    raxStop(&iter);

    // insert inversion pair
    raxInsert(client_tree, (uint8_t*)topicbyalias, 24 + 1 + ptlen, NULL, NULL);
    raxInsert(client_tree, (uint8_t*)aliasbytopic, 24 + ptlen + 1, NULL, NULL);
    return 0;
}

int mr_remove_client_topic_aliases(rax* client_tree, const uint64_t client) {
    uint8_t aliases[8 + 7];
    for (int i = 0; i < 8; i++) aliases[i] = client >> ((7 - i) * 8) & 0xff;
    memcpy(aliases + 8, "aliases", 7);
    raxFreeSubtreeWithCallback(client_tree, aliases, 8 + 7, rax_free);
    return 0;
}

int mr_get_alias_by_topic(rax* client_tree, const uint64_t client, const bool isclient, const char* pubtopic, uint8_t* palias) {
    size_t ptlen = strlen(pubtopic);
    char aliasbytopic[24 + ptlen + 1]; // <Client ID>"aliasesclientabt"<pubtopic>\0
    for (int i = 0; i < 8; i++) aliasbytopic[i] = client >> ((7 - i) * 8) & 0xff;
    char* source = isclient ? "client" : "server";
    snprintf(aliasbytopic + 8, 16 + ptlen + 1, "aliases%sabt%.*s", source, (int)ptlen, pubtopic);
    raxIterator iter;
    raxStart(&iter, client_tree);

    if (raxSeekChildren(&iter, (uint8_t*)aliasbytopic, 24 + ptlen) && raxNextChild(&iter)) {
        *palias = iter.key[iter.key_len - 1];
    }
    else {
        *palias = 0;
    }

    raxStop(&iter);
    return 0;
}

int mr_get_topic_by_alias(rax* client_tree, const uint64_t client, const bool isclient, const uint8_t alias, char* pubtopic) {
    char topicbyalias[24 + 1]; // <Client ID>"aliasesclienttba"<alias>\0
    for (int i = 0; i < 8; i++) topicbyalias[i] = client >> ((7 - i) * 8) & 0xff;
    char* source = isclient ? "client" : "server";
    snprintf(topicbyalias + 8, 16 + 1, "aliases%stba", source);
    topicbyalias[24] = alias;
    raxIterator iter;
    raxStart(&iter, client_tree);

    if (raxSeekChildren(&iter, (uint8_t*)topicbyalias, 24 + 1) && raxNextChild(&iter)) {
        size_t ptlen = iter.key_len - (24 + 1);
        memcpy(pubtopic, iter.key + 24 + 1, ptlen);
        pubtopic[ptlen] = '\0';
    }
    else {
        pubtopic = NULL;
    }

    raxStop(&iter);
    return 0;
}

int mr_remove_client_data(rax* topic_tree, rax* client_tree, uint64_t client) {
    mr_remove_client_subscriptions(topic_tree, client_tree, client);
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;
    raxFreeSubtreeWithCallback(client_tree, clientv, 8, rax_free);
    return 0;
}
