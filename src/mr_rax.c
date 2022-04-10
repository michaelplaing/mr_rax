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

int mr_get_normalized_topic(const char* topic_in, char* topic) {
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

int mr_get_subscribe_topic(const char* subtopic, char* topic, char* share) {
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

    mr_get_normalized_topic(subtopic2, topic);
    return 0;
}

static int mr_insert_topic(rax* tcrax, char* topic, size_t len) {
    raxTryInsert(tcrax, (uint8_t*)topic, len, NULL, NULL);
    return 0;
}

static int mr_insert_parent_topic_tree(rax* tcrax, const char* topic) {
    size_t tlen = strlen(topic);
    char* tokenv[MAX_TOKENS];
    char topic2[tlen + 1];
    char topic3[tlen + 1];
    strlcpy(topic3, topic, tlen + 1);
    size_t numtokens = mr_tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    topic2[0] = '\0';

    for (int i = 0; i < (numtokens - 1); i++) {
        strlcat(topic2, tokenv[i], tlen + 1);
        mr_insert_topic(tcrax, topic2, strlen(topic2));
        strlcat(topic2, "/", tlen + 1);
    }

    return 0;
}

int mr_insert_subscription(rax* tcrax, rax* crax, const char* subtopic, const uint64_t client) {
    size_t stlen = strlen(subtopic);
    char topic[stlen + 3];
    char share[stlen + 1];
    share[0] = '\0';

    // get topic and share name if any
    mr_get_subscribe_topic(subtopic, topic, share);

    // insert/update topic & parent tree incrementing client counts
    mr_insert_parent_topic_tree(tcrax, topic);
    size_t tlen = strlen(topic);
    mr_insert_topic(tcrax, topic, tlen);

    // get the client bytes in network order (big endian)
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;

    // insert sub/client in subscription subtree
    size_t slen = strlen(share);
    size_t tlen2 = tlen + 1 + slen;
    char topic2[tlen2 + 8];
    strlcpy(topic2, topic, tlen + 1);

    if (slen) { // shared subscription sub-hierarchy
        memcpy((void*)topic2 + tlen, &shared_mark, 1);
        mr_insert_topic(tcrax, topic2, tlen + 1);
        memcpy((void*)topic2 + tlen + 1, (void*)share, slen);
    }
    else { // regular subscription sub-hierarchy
        memcpy((void*)topic2 + tlen, &client_mark, 1);
    }

    mr_insert_topic(tcrax, topic2, tlen2);
    memcpy((void*)topic2 + tlen2, (void*)clientv, 8);
    raxInsert(tcrax, (uint8_t*)topic2, tlen2 + 8, NULL, NULL); // insert the client

    char topic3[8 + 4 + stlen];
    memcpy(topic3, clientv, 8);
    raxTryInsert(crax, (uint8_t*)topic3, 8, NULL, NULL);
    memcpy(topic3 + 8, "subs", 4);
    raxTryInsert(crax, (uint8_t*)topic3, 8 + 4, NULL, NULL);
    memcpy(topic3 + 8 + 4, subtopic, stlen);
    raxTryInsert(crax, (uint8_t*)topic3, 8 + 4 + stlen, NULL, NULL);

    return 0;
}

static int mr_trim_topic(rax* tcrax, raxIterator* piter, char* topic, size_t len) {
    raxSeekChildren(piter, (uint8_t*)topic, len);
    if (raxNextChild(piter)) return 0; // has children still
    raxRemove(tcrax, (uint8_t*)topic, len, NULL);
    return 1;
}

static int mr_trim_topic_tree(rax* tcrax, raxIterator* piter, const char* topic) {
    size_t tlen = strlen(topic);
    char topic2[tlen + 1];
    strlcpy(topic2, topic, tlen + 1);
    char* pc = topic2 + tlen;
    int count = 0;

    while(pc) {
        *pc = '\0';
        size_t len = pc - topic2;
        if (!mr_trim_topic(tcrax, piter, topic2, len)) return count; // done
        count++;
        pc = strrchr(topic2, '/');
    }

    return count;
}

int mr_remove_subscription(rax* tcrax, rax* crax, const char* subtopic, const uint64_t client) {
    size_t stlen = strlen(subtopic);
    char topic[stlen + 3];
    char share[stlen + 1];
    share[0] = '\0';

    // get topic and share name if any
    mr_get_subscribe_topic(subtopic, topic, share);
    size_t tlen = strlen(topic);

    // get the client bytes in network order (big endian)
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;

    size_t slen = strlen(share);
    size_t tlen2 = tlen + 1 + slen;
    char topic2[tlen2 + 8];
    strlcpy(topic2, topic, tlen + 1);

    if (slen) { // shared subscription sub-hierarchy
        memcpy((void*)topic2 + tlen, &shared_mark, 1);
        memcpy((void*)topic2 + tlen + 1, (void*)share, slen);
    }
    else { // regular subscription sub-hierarchy
        memcpy((void*)topic2 + tlen, &client_mark, 1);
    }

    // remove client
    memcpy((void*)topic2 + tlen2, (void*)clientv, 8);
    raxIterator iter;
    raxStart(&iter, tcrax);

    if (raxRemove(tcrax, (uint8_t*)topic2, tlen2 + 8, NULL)) { // found
        // trim hierarchy as long as there are no more child keys
        if (mr_trim_topic(tcrax, &iter, topic2, tlen2)) { // client mark or share
            int trimmed = 1;
            if (slen) trimmed = mr_trim_topic(tcrax, &iter, topic2, tlen + 1); // shared mark
            if (trimmed) mr_trim_topic_tree(tcrax, &iter, topic);
        }
    }

    // remove inversion
    raxStart(&iter, crax);
    char topic3[8 + 4 + stlen];
    memcpy(topic3, clientv, 8);
    memcpy(topic3 + 8, "subs", 4);
    memcpy(topic3 + 8 + 4, subtopic, stlen);

    if (raxRemove(crax, (uint8_t*)topic3, 8 + 4 + stlen, NULL)) { // found
        if (mr_trim_topic(crax, &iter, topic3, 8 + 4)) {
            mr_trim_topic(crax, &iter, topic3, 8);
        }
    }

    return 0;
}

int mr_remove_client_subscriptions(rax* tcrax, rax* crax, const uint64_t client) {
    rax* srax = raxNew();
    raxIterator iter;
    raxStart(&iter, crax);
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
        mr_remove_subscription(tcrax, crax, subtopic, client);
    }

    raxStop(&iter);
    raxFree(srax);
    return 0;
}

static int mr_get_topic_clients(rax* tcrax, rax* srax, uint8_t* key, size_t key_len) {
    raxIterator iter;
    raxStart(&iter, tcrax);
    raxIterator iter2;
    raxStart(&iter2, tcrax);

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

static int mr_probe_subscriptions(rax* tcrax, rax* srax, char* topic, int level, char** tokenv, size_t numtokens) {
    char topic2[MAX_TOPIC_LEN];
    char* token = tokenv[level];

    while (level < numtokens) {
        strlcat(topic, token, MAX_TOPIC_LEN);

        snprintf(topic2, MAX_TOPIC_LEN, "%s/#", topic);
        if (raxFind(tcrax, (uint8_t*)topic2, strlen(topic2)) != raxNotFound) {
            mr_get_topic_clients(tcrax, srax, (uint8_t*)topic2, strlen(topic2));
        }

        if (level == (numtokens - 1)) break; // only '#' is valid at this level

        snprintf(topic2, MAX_TOPIC_LEN, "%s/+", topic);
        if (raxFind(tcrax, (uint8_t*)topic2, strlen(topic2)) != raxNotFound) {
            if (level == (numtokens - 2)) {
                mr_get_topic_clients(tcrax, srax, (uint8_t*)topic2, strlen(topic2));
            }
            else {
                char *token2v[numtokens];
                for (int i = 0; i < numtokens; i++) token2v[i] = tokenv[i];
                token2v[level + 1] = "+";
                topic2[strlen(topic2) - 1] = '\0'; // trim the '+'
                mr_probe_subscriptions(tcrax, srax, topic2, level + 1, token2v, numtokens);
            }
        }

        token = tokenv[level + 1];
        snprintf(topic2, MAX_TOPIC_LEN, "%s/%s", topic, token);

        if (raxFind(tcrax, (uint8_t*)topic2, strlen(topic2)) != raxNotFound) {
            if (level == (numtokens - 2)) mr_get_topic_clients(tcrax, srax, (uint8_t*)topic2, strlen(topic2));
        }
        else {
            break; // no more possible matches
        }

        strlcat(topic, "/", MAX_TOPIC_LEN);
        level++;
    }

    return 0;
}

int mr_get_subscribed_clients(rax* tcrax, rax* srax, const char* pubtopic) {
    char* tokenv[MAX_TOKENS];
    char topic[MAX_TOPIC_LEN];
    char topic2[MAX_TOPIC_LEN];
    char topic3[MAX_TOPIC_LEN];
    mr_get_normalized_topic(pubtopic, topic);
    strlcpy(topic3, topic, MAX_TOPIC_LEN);
    size_t numtokens = mr_tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    topic2[0] = '\0';
    int level = 0;
    mr_probe_subscriptions(tcrax, srax, topic2, level, tokenv, numtokens);
    return 0;
}

int mr_upsert_client_topic_alias(
    rax* crax, const uint64_t client, const bool isincoming, const char* pubtopic, const uint8_t alias
) {
    size_t ptlen = strlen(pubtopic);
    char* abt = isincoming ? "iabt" : "oabt";
    char* tba = isincoming ? "itba" : "otba";
    char topicbyalias[8 + 4 + 1]; // <Client ID>"itba"<alias>
    char aliasbytopic[8 + 4 + ptlen]; // <Client ID>"iabt"<pubtopic>
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;

    // common
    memcpy(topicbyalias, clientv, 8);
    memcpy(aliasbytopic, clientv, 8);
    raxTryInsert(crax, (uint8_t*)topicbyalias, 8, NULL, NULL);
    memcpy(topicbyalias + 8, tba, 4);
    raxTryInsert(crax, (uint8_t*)topicbyalias, 8 + 4, NULL, NULL);
    memcpy(aliasbytopic + 8, abt, 4);
    raxTryInsert(crax, (uint8_t*)aliasbytopic, 8 + 4, NULL, NULL);

    // inversion pair
    memcpy(topicbyalias + 8 + 4, &alias, 1);
    memcpy(aliasbytopic + 8 + 4, pubtopic, ptlen);

    // clear topicbyalias & inversion if necessary
    char* pubtopic2;
    if (raxRemove(crax, (uint8_t*)topicbyalias, 8 + 4 + 1, (void**)&pubtopic2)) {
        size_t ptlen2 = strlen(pubtopic2);
        memcpy(aliasbytopic + 8 + 4, pubtopic2, ptlen2);
        raxRemoveScalar(crax, (uint8_t*)aliasbytopic, 8 + 4 + ptlen2, NULL);
        rax_free(pubtopic2);
        memcpy(aliasbytopic + 8 + 4, pubtopic, ptlen); // restore
    }

    // clear aliasbytopic & inversion if necessary
    uintptr_t old_bigalias;
    if (raxRemoveScalar(crax, (uint8_t*)aliasbytopic, 8 + 4 + ptlen, &old_bigalias)) {
        uint8_t old_alias = old_bigalias & 0xff;
        memcpy(topicbyalias + 8 + 4, &old_alias, 1);
        char* pubtopic3;
        raxRemove(crax, (uint8_t*)topicbyalias, 8 + 4 + 1, (void**)&pubtopic3);
        rax_free(pubtopic3);
        memcpy(topicbyalias + 8 + 4, &alias, 1); // restore
    }

    // insert inversion pair
    char* pubtopic4 = strdup(pubtopic); // free on removal
    raxInsert(crax, (uint8_t*)topicbyalias, 8 + 4 + 1, pubtopic4, NULL);
    raxInsertScalar(crax, (uint8_t*)aliasbytopic, 8 + 4 + ptlen, alias, NULL);

    return 0;
}

int mr_remove_client_topic_aliases(rax* crax, const uint64_t client) {
    uint8_t clientplusv[8 + 4];
    for (int i = 0; i < 8; i++) clientplusv[i] = client >> ((7 - i) * 8) & 0xff;
    char* prefixv[] = {"iabt", "itba", "oabt", "otba"};

    for (int i = 0; i < 4; i++) {
        memcpy(clientplusv + 8, prefixv[i], 4);
        raxFreeSubtreeWithCallback(crax, clientplusv, 8 + 4, rax_free);
    }

    return 0;
}

int mr_get_alias_by_topic(rax* crax, const uint64_t client, const bool isincoming, const char* pubtopic, uint8_t* palias) {
    size_t ptlen = strlen(pubtopic);
    char aliasbytopic[8 + 4 + ptlen]; // <Client ID>"iabt"<pubtopic>
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;
    memcpy(aliasbytopic, clientv, 8);
    char* abt = isincoming ? "iabt" : "oabt";
    memcpy(aliasbytopic + 8, abt, 4);
    memcpy(aliasbytopic + 8 + 4, pubtopic, ptlen);
    void* value = raxFind(crax, (uint8_t*)aliasbytopic, 8 + 4 + ptlen);
    *palias = value == raxNotFound ? 0 : (uintptr_t)value & 0xff; // 0 is an invalid alias
    return 0;
}

int mr_get_topic_by_alias(rax* crax, const uint64_t client, const bool isincoming, const uint8_t alias, char** ppubtopic) {
    char topicbyalias[8 + 4 + 1]; // <Client ID>"itba"<alias>
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;
    memcpy(topicbyalias, clientv, 8);
    char* tba = isincoming ? "itba" : "otba";
    memcpy(topicbyalias + 8, tba, 4);
    memcpy(topicbyalias + 8 + 4, &alias, 1);
    void* value = raxFind(crax, (uint8_t*)topicbyalias, 8 + 4 + 1);
    *ppubtopic = value == raxNotFound ? NULL : value;
    return 0;
}

int mr_remove_client_data(rax* tcrax, rax* crax, uint64_t client) {
    mr_remove_client_subscriptions(tcrax, crax, client);
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;
    raxFreeSubtreeWithCallback(crax, clientv, 8, rax_free);
    return 0;
}