// topics.c

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mr_rax/rax.h"
#include "mr_rax/rax_malloc.h"
#include "rc4rand.h"

#define MAX_TOKENS 32
#define MAX_TOKEN_LEN 64
#define MAX_TOPIC_LEN 256

// invalid utf8 chars used to separate clients & shared subs from topics
static uint8_t client_mark = 0xFE;
static uint8_t shared_mark = 0xFF;
// static uint8_t client_mark = 'C'; // for visualization
// static uint8_t shared_mark = 'S';

enum match_result_types {
    NO_MATCH,
    PLUS_MATCH
};

int raxUnion(rax* rax_base, rax* rax_in) {
    raxIterator iter;
    raxStart(&iter, rax_in);
    if (!raxSeek(&iter, "^", NULL, 0)) return 0;

    while(raxNext(&iter)) {
        if (!raxInsert(rax_base, iter.key, iter.key_len, iter.data, NULL) && errno != 0) break;
    }

    if (errno != 0) return 0;
    raxStop(&iter);
    return 1;
}

static uint8_t empty_valuev[] = {0x1f, 0}; // MQTT disallowed control char: unit separator <US>
// static uint8_t empty_valuev[] = {'!', 0}; // for visualization

static int tokenize_topic(char* topic, char** tokenv) {
    int numtokens;

    for (numtokens = 0; numtokens < MAX_TOKENS; numtokens++, tokenv++) {
        *tokenv = strsep(&topic, "/");
        if (*tokenv == NULL) break;
        if (*tokenv[0] == '\0') *tokenv = (char*)&empty_valuev;
    }

    return numtokens;
}

static int get_normalized_topic(const char* topic_in, char* topic) {
    size_t tlen = strlen(topic_in);
    char topic2[tlen + 3];

    if (topic_in[0] == '$') {
        snprintf(topic2, tlen + 3, "$/%s", topic_in);
    }
    else {
        snprintf(topic2, tlen + 3, "@/%s", topic_in);
    }

    char* tokenv[MAX_TOKENS];
    int numtokens = tokenize_topic(topic2, tokenv);

    topic[0] = '\0';
    for (int i = 0; i < numtokens; i++) {
        strlcat(topic, tokenv[i], MAX_TOPIC_LEN);
        strcat(topic, "/");
    }

    topic[strlen(topic) - 1] = '\0';
    return 0;
}

static int get_subscribe_topic(const char* subtopic, char* topic, char* share) {
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

    get_normalized_topic(subtopic2, topic);
    return 0;
}

static int upsert_topic(rax* prax, char* topic, size_t len) {
    uintptr_t client_count; // increment
    int try = raxTryInsert(prax, (uint8_t*)topic, len, (void*)1, (void**)&client_count);
    if (!try) raxInsert(prax, (uint8_t*)topic, len, (void*)(client_count + 1), NULL);
    return 0;
}

static int upsert_parent_topic_tree(rax* prax, const char* topic) {
    size_t tlen = strlen(topic);
    char* tokenv[MAX_TOKENS];
    char topic2[tlen + 1];
    char topic3[tlen + 1];
    strlcpy(topic3, topic, tlen + 1);
    size_t numtokens = tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    topic2[0] = '\0';

    for (int i = 0; i < (numtokens - 1); i++) {
        strlcat(topic2, tokenv[i], tlen + 1);
        upsert_topic(prax, topic2, strlen(topic2));
        strlcat(topic2, "/", tlen + 1);
    }

    return 0;
}

static int insert_subscription(rax* prax, const char* subtopic, const uint64_t client) {
    size_t tlen_in = strlen(subtopic);
    char topic[tlen_in + 3];
    char share[tlen_in + 1];
    share[0] = '\0';

    // get topic and share name if any
    get_subscribe_topic(subtopic, topic, share);

    // insert/update topic & parent tree incrementing client counts
    upsert_parent_topic_tree(prax, topic);
    size_t tlen = strlen(topic);
    upsert_topic(prax, topic, tlen);

    // get the client bytes in network order (big endian)
    uint8_t clientv[8];
    for (int i = 0; i < 8; i++) clientv[i] = client >> ((7 - i) * 8) & 0xff;

    // insert client
    char topic2[tlen + 1 + 8];
    strlcpy(topic2, topic, tlen + 1);
    size_t slen = strlen(share);

    if (slen) { // shared subscription sub-hierarchy
        memcpy((void*)topic2 + tlen, &shared_mark, 1);
        upsert_topic(prax, topic2, tlen + 1);
        memcpy((void*)topic2 + tlen + 1, (void*)share, slen);
        upsert_topic(prax, topic2, tlen + 1 + slen);
        memcpy((void*)topic2 + tlen + 1 + slen, (void*)clientv, 8);
        raxInsert(prax, (uint8_t*)topic2, tlen + 1 + slen + 8, NULL, NULL); // insert the client
    }
    else { // regular subscription sub-hierarchy
        memcpy((void*)topic2 + tlen, &client_mark, 1);
        upsert_topic(prax, topic2, tlen + 1);
        memcpy((void*)topic2 + tlen + 1, (void*)clientv, 8);
        raxInsert(prax, (uint8_t*)topic2, tlen + 1 + 8, NULL, NULL); // insert the client
    }

    return 0;
}

static int get_topic_clients(rax* prax, rax* crax, uint8_t* key, size_t key_len) {
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

static int probe_subscriptions(rax* prax, rax* crax, char* topic, int level, char** tokenv, size_t numtokens) {
    char topic2[MAX_TOPIC_LEN];
    char* token = tokenv[level];

    while (level < numtokens) {
        strlcat(topic, token, MAX_TOPIC_LEN);

        snprintf(topic2, MAX_TOPIC_LEN, "%s/#", topic);
        if (raxFind(prax, (uint8_t*)topic2, strlen(topic2)) != raxNotFound) {
            get_topic_clients(prax, crax, (uint8_t*)topic2, strlen(topic2));
        }

        if (level == (numtokens - 1)) break; // only '#' is valid at this level

        snprintf(topic2, MAX_TOPIC_LEN, "%s/+", topic);
        if (raxFind(prax, (uint8_t*)topic2, strlen(topic2)) != raxNotFound) {
            if (level == (numtokens - 2)) {
                get_topic_clients(prax, crax, (uint8_t*)topic2, strlen(topic2));
            }
            else {
                char *token2v[numtokens];
                for (int i = 0; i < numtokens; i++) token2v[i] = tokenv[i];
                token2v[level + 1] = "+";
                topic2[strlen(topic2) - 1] = '\0'; // trim the '+'
                probe_subscriptions(prax, crax, topic2, level + 1, token2v, numtokens);
            }
        }

        token = tokenv[level + 1];
        snprintf(topic2, MAX_TOPIC_LEN, "%s/%s", topic, token);
        if (
            raxFind(prax, (uint8_t*)topic2, strlen(topic2)) != raxNotFound &&
            level == (numtokens - 2)
        )  get_topic_clients(prax, crax, (uint8_t*)topic2, strlen(topic2));

        strlcat(topic, "/", MAX_TOPIC_LEN);
        level++;
    }

    return 0;
}

static int get_clients(rax* prax, rax* crax, char* topic) {
    char* tokenv[MAX_TOKENS];
    char topic2[MAX_TOPIC_LEN];
    char topic3[MAX_TOPIC_LEN];
    strlcpy(topic3, topic, MAX_TOPIC_LEN);
    size_t numtokens = tokenize_topic(topic3, tokenv); // modifies topic3 and points into it from tokenv
    topic2[0] = '\0';
    int level = 0;
    probe_subscriptions(prax, crax, topic2, level, tokenv, numtokens);
    return 0;
}

int topic_fun(void) {
    rax* prax = raxNew();

    char* subtopicclientv[] = {
        // "$share/bom/bip/bop:21",
        // "$sys/baz/bam:20",
        "$share/bom/sport/tennis/matches/#:1001;1002;1003;1022;1023",
        "$share/bip/sport/tennis/matches/france/amateur/+:2023;2024;2004;2005;2006;2002;2003",
        "$share/foo/$sys/baz/bam:25",
        "sport/tennis/matches:1;2;3",
        "sport/tennis/matches/italy/#:4",
        "sport/tennis/matches/italy/#:5",
        "sport/tennis/matches/united states/amateur/+:6",
        "sport/tennis/matches/#:7;8",
        "sport/tennis/matches/+/professional/+:3;5",
        "sport/tennis/matches/+/amateur/#:9",
        "sport/tennis/matches/italy/professional/i6:12;1",
        "sport/tennis/matches/italy/professional/i5:2",
        "sport/tennis/matches/ethiopia/professional/e4:11;7;1",
        "sport/tennis/matches/england/professional/e1:10",
        "sport/tennis/matches/england/professional/e2:13;9",
        "sport/tennis/matches/england/professional/e3:9;13",
        "sport/tennis/matches/france/professional/f4:14",
        "sport/tennis/matches/france/amateur/af1:6",
        "sport/tennis/matches/france/amateur/af2:66",
        "sport/tennis/matches/france/amateur/af1/#:99",
        "sport/tennis/matches/france/amateur/af1/\":999",
        "sport/tennis/matches/france/amateur/af1/foo:9999",
        "sport/+/+/+/amateur/#:10;11;9",
        "sport/tennis/matches/united states/amateur/au2:2;4;8;10",
        // "/foo:1;2",
        //"/f:1",
        // "/foo/:3;5",
        // "bar:2;3",
        //"//:15;16",
        // "#:100",
        // "s:99",
        // "s/#:999",
        // "foo:9;10;11;12",
    };

    size_t numtopics = sizeof(subtopicclientv) / sizeof(subtopicclientv[0]);

    char* tokenv[MAX_TOKENS];
    size_t numtokens;
    char subtopic[MAX_TOPIC_LEN];
    char subtopicclient[MAX_TOPIC_LEN];
    char topic[MAX_TOPIC_LEN];
    char topic2[MAX_TOPIC_LEN];
    char topic3[MAX_TOPIC_LEN];

    for (int i = 0; i < numtopics; i++) {
        strcpy(subtopicclient, subtopicclientv[i]);
        char* pc = strchr(subtopicclient, ':');
        *pc = '\0';
        strlcpy(subtopic, subtopicclient, MAX_TOPIC_LEN);
        char *unparsed_clients = pc + 1;
        char *clientstr;

        while ((clientstr = strsep(&unparsed_clients, ";")) != NULL) {
            uint64_t client = strtoull(clientstr, NULL, 0);
            insert_subscription(prax, subtopic, client);
        }
    }

    // raxShow(prax);

    strlcpy(topic, "@/sport/tennis/matches", MAX_TOPIC_LEN);
    printf("seek topic: %s\n", topic);
    raxIterator iter;
    raxStart(&iter, prax);
    raxIterator iter2;
    raxStart(&iter2, prax);

    strlcpy(topic2, topic, MAX_TOPIC_LEN);
    size_t tlen = strlen(topic2);
    topic2[tlen] = client_mark;
    printf("regular clients:");
    raxSeekChildren(&iter, (uint8_t*)topic2, tlen + 1);

    while(raxNextChild(&iter)) {
        uint64_t client = 0;
        for (int i = 0; i < 8; i++) client += iter.key[iter.key_len - 8 + i] << ((8 - i - 1) * 8);
        printf(" %llu", client);
    }

    puts("");

    printf("shared subscriptions\n");
    strlcpy(topic2, topic, MAX_TOPIC_LEN);
    topic2[tlen] = shared_mark;
    raxSeekChildren(&iter, (uint8_t*)topic2, tlen + 1);

    while(raxNextChild(&iter)) {
        size_t slen = iter.key_len - tlen - 1;
        char share[slen];
        memcpy(share, iter.key + iter.key_len - slen, slen);
        printf("shared name: %.*s; clients:", (int)slen, share);
        raxSeekChildren(&iter2, iter.key, iter.key_len);

        while(raxNextChild(&iter2)) {
            uint64_t client = 0;
            for (int i = 0; i < 8; i++) client += iter2.key[iter2.key_len - 8 + i] << ((8 - i - 1) * 8);
            printf(" %llu", client);
        }

        puts("");
    }

    // char pubtopic[] = "sport/tennis/matches";
    char pubtopic[] = "sport/tennis/matches/france/amateur/af1";
    // char pubtopic[] = "s";
    // char pubtopic[] = "/foo/";
    printf("get matching clients for '%s'\n", pubtopic);
    get_normalized_topic(pubtopic, topic);
    rax* crax = raxNew();
    get_clients(prax, crax, topic);

    raxIterator citer;
    raxStart(&citer, crax);
    raxSeek(&citer, "^", NULL, 0);

    while(raxNext(&citer)) {
        uint64_t client = 0;
        for (int i = 0; i < 8; i++) client += citer.key[i] << ((8 - i - 1) * 8);
        printf(" %llu", client);
    }

    puts("");

    // raxShow(crax);

    raxStop(&citer);
    raxFree(crax);

/*
    rax* brax = raxNew();
    rax* nrax = raxNew();

    raxInsert(brax, (uint8_t*)"@/foobar", 8, NULL, NULL);
    raxInsert(nrax, (uint8_t*)"@/bazbam", 8, NULL, NULL);
    raxUnion(brax, nrax);
    raxFree(nrax);

    raxIterator biter;
    raxStart(&biter, brax);
    raxSeek(&biter, "^", NULL, 0);

    while(raxNext(&biter)) {
        printf("%.*s:%lu\n", (int)biter.key_len, (char*)biter.key, (uintptr_t)biter.data);
    }

    raxStop(&biter);
    raxFree(brax);

 */
/*
        raxInsert(prax, (uint8_t*)topic, strlen(topic), (void*)i, NULL);
        numtokens = tokenize_topic(topic, tokenv); // modifies topic replacing '/' with '\0'

        topic2[0] = '\0';
        for (int j = 0; j < (numtokens - 1); j++) {
            strlcat(topic2, tokenv[j], MAX_TOPIC_LEN);
            // printf("raxTryInsert: %s\n", topic2);
            if (strlen(topic2) > 2) raxTryInsert(prax, (uint8_t*)topic2, strlen(topic2), NULL, NULL);
            strlcat(topic2, "/", MAX_TOPIC_LEN);
        }
    }
*/

    // raxShow(prax);
    // raxShowHex(prax);

/*
    raxIterator iter;
    raxStart(&iter, prax);
    raxSeek(&iter, "^", NULL, 0);
    printf("\nsubscriptions:\n");

    while(raxNext(&iter)) {
        printf("%.*s:%lu\n", (int)iter.key_len, (char*)iter.key, (uintptr_t)iter.data);
    }

    char pubtopic[] = "sport/tennis/matches/france/amateur/af1";

    // exact search
    printf("\npubtopic: %s\n", pubtopic);
    snprintf(topic, strlen(pubtopic) + 3, "@/%s", pubtopic);
    void* result = raxFind(prax, (uint8_t*)topic, strlen(topic));
    if (result != raxNotFound) printf("exact match: %s:%lu\n", pubtopic, (uintptr_t)result);

    // tokenize
    strlcpy(topic2, topic, MAX_TOPIC_LEN); // topic2 provides storage for tokenv
    numtokens = tokenize_topic(topic2, tokenv); // modifies topic2 replacing '/' with '\0'
    // for (int i = 0; i < numtokens; i++) printf("token %d: %s\n", i, tokenv[i]);

    printf("\nwildcard search:\n");

    int match_result = NO_MATCH;
    strlcpy(topic, "@/", MAX_TOPIC_LEN);
    for (int i = 1; i < numtokens; i++) {
        strlcat(topic, "+", MAX_TOPIC_LEN);
        size_t len = strlen(topic);
        printf("topic: %s\n", topic);
        result = raxFind(prax, (uint8_t*)topic, len);

        if (result != raxNotFound) {
            printf("plus match: %s:%lu\n", topic, (uintptr_t)result);
            topic[len - 2] = '\0';
            match_result = PLUS_MATCH;
            break;
        }

        topic[len - 1] = '#';
        printf("topic: %s\n", topic);
        result = raxFind(prax, (uint8_t*)topic, len);

        if (result != raxNotFound) {
            printf("hash match: %s:%lu\n", topic, (uintptr_t)result);
        }

        topic[len - 1] = '\0';
        strlcat(topic, tokenv[i], MAX_TOPIC_LEN);
        strlcat(topic, "/", MAX_TOPIC_LEN);
    }

    if (match_result != PLUS_MATCH) topic[0] = '\0';
    printf("wildcard search result topic: %s\n", topic);

    printf("seek topic: %s\n", topic);
    raxSeekChildren(&iter, (uint8_t*)topic, strlen(topic));

    while(raxNextChild(&iter)) {
        printf("%.*s:%lu\n", (int)iter.key_len, (char*)iter.key, (uintptr_t)iter.data);
    }

    strlcpy(topic, "@/sport/tennis/matches/france/amateur", MAX_TOPIC_LEN);
    printf("subtree topic: %s\n", topic);
    raxSeekSubtree(&iter, (uint8_t*)topic, strlen(topic));

    while(raxNext(&iter)) {
        printf("%.*s:%lu\n", (int)iter.key_len, (char*)iter.key, (uintptr_t)iter.data);
    }
*/
    raxStop(&iter);
    raxStop(&iter2);
    raxFree(prax);
    return 0;
}

int main(int argc, char** argv) {
    rc4srand(1234);
    return topic_fun();
}
