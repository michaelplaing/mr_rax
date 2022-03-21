// topics.c

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>
#include <errno.h>

#include "mr_rax/rax.h"
#include "mr_rax/rax_malloc.h"
#include "rc4rand.h"

// This is a special distinct node value used to separate tokens
static void* token_delimiter = NULL; // (void*)42; // "token_delimiter";

#define MAX_TOKENS 32
#define MAX_TOKEN_LEN 64
#define MAX_TOPIC_LEN 256

enum match_result_types {
    NO_MATCH,
    PLUS_MATCH
};

static int tokenize_topic(char* topic, char** ptopicv) {
    int numtokens;

    for (numtokens = 0; numtokens < MAX_TOKENS; numtokens++, ptopicv++) {
        *ptopicv = strsep(&topic, "/");
        if (*ptopicv == NULL) break;
    }

    return numtokens;
}

static int subtree_search(raxIterator *piter, const char* topic) {
    raxIterator iter = *piter;
    size_t len = strlen(topic);
    char* tokenv[MAX_TOKENS];
    // char topic2[MAX_TOPIC_LEN];
    char topic2[len + 1];
    char topic3[len + 1];
    strlcpy(topic2, topic, MAX_TOPIC_LEN);
    strlcpy(topic3, topic, MAX_TOPIC_LEN);
    size_t numtokens = tokenize_topic(topic2, tokenv);
    printf("\nsubtree parent:: topic: '%s'; numtokens: %lu\n", topic, numtokens);

    // iterate subtree
    raxSeek(&iter, ">", (uint8_t*)topic, len);
    topic3[len - 1] = 0xff;

    char token_prev[MAX_TOKEN_LEN] = "";
    while(raxNext(&iter) && raxCompare(&iter, "<", (uint8_t*)topic3, len)) {
        char cv[iter.key_len + 1];
        snprintf(cv, iter.key_len + 1, "%s", iter.key);
        tokenize_topic(cv, tokenv);

        // get unique immediate child tokens
        if (strcmp(tokenv[numtokens], token_prev) != 0) {
            printf("child: '%s' (%.*s:%lu)\n", tokenv[numtokens], (int)iter.key_len, (char*)iter.key, (uintptr_t)iter.data);
            strlcpy(token_prev, tokenv[numtokens], MAX_TOKEN_LEN);
        }
    }

    return 0;
}

int topic_fun(void) {
    rax* prax = raxNew();

    char* topicv[] = {
        // "+/whatever",
        "sport/tennis/#",
        "sport/tennis/matches/italy/#",
        "sport/tennis/matches/united states/amateur/+",
        "sport/tennis/matches/#",
        "sport/tennis/matches/+/professional/+",
        "sport/tennis/matches/+/amateur/#",
        "sport/tennis/matches/italy/professional/i6",
        "sport/tennis/matches/italy/professional/i5",
        "sport/tennis/matches/ethiopia/professional/e4",
        "sport/tennis/matches/england/professional/e1",
        "sport/tennis/matches/england/professional/e2",
        "sport/tennis/matches/england/professional/e3",
        "sport/tennis/matches/france/professional/f4",
        "sport/tennis/matches/france/amateur/af1",
        "sport/tennis/matches/united states/amateur/au2",
        "/foo",
        "/foo/",
        "bar",
        "//"
    };

    size_t numtopics = sizeof(topicv) / sizeof(topicv[0]);

    char* tokenv[MAX_TOKENS];
    size_t numtokens;
    char topic[MAX_TOPIC_LEN];
    char topic2[MAX_TOPIC_LEN];

    for (uintptr_t i = 0; i < numtopics; i++) {
        snprintf(topic, strlen(topicv[i]) + 3, "@/%s", topicv[i]);
        raxInsert(prax, (uint8_t*)topic, strlen(topic), (void*)i, NULL);
        numtokens = tokenize_topic(topic, tokenv); // modifies topic replacing '/' with '\0'

        topic2[0] = '\0';
        for (int j = 0; j < (numtokens - 1); j++) {
            strlcat(topic2, tokenv[j], MAX_TOPIC_LEN);
            // printf("raxTryInsert: %s\n", topic2);
            if (strlen(topic2) > 2) raxTryInsert(prax, (uint8_t*)topic2, strlen(topic2), token_delimiter, NULL);
            strlcat(topic2, "/", MAX_TOPIC_LEN);
        }
    }

    raxShow(prax);

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

    // subtree
    // strlcpy(topic, "@/sport/tennis/matches", MAX_TOPIC_LEN);
    subtree_search(&iter, topic);

    printf("seek topic: %s\n", topic);
    raxSeekChildren(&iter, (uint8_t*)topic, strlen(topic));

    while(raxNextChild(&iter)) {
        printf("%.*s:%lu\n", (int)iter.key_len, (char*)iter.key, (uintptr_t)iter.data);
    }


    raxStop(&iter);
    raxFree(prax);
    return 0;
}

int main(int argc, char** argv) {
    rc4srand(1234);
    return topic_fun();
}
