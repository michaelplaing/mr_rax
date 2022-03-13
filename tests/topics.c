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

int topic_fun(void) {
    rax* t = raxNew();

    char* topics[] = {
        "sport/tennis/#",
        "sport/tennis/matches/italy/#",
        "sport/tennis/matches/united states/amateur/+",
        "sport/tennis/matches/#",
        "sport/tennis/matches/+/professional/+",
        "sport/tennis/matches/+/amateur/#",
        "sport/tennis/matches/italy/professional/i6",
        "sport/tennis/matches/italy/professional/i5",
        "sport/tennis/matches/england/professional/e1",
        "sport/tennis/matches/england/professional/e2",
        "sport/tennis/matches/england/professional/e3",
        "sport/tennis/matches/france/professional/f4",
        "sport/tennis/matches/france/amateur/af1",
        "sport/tennis/matches/united states/amateur/au2",
        "/foo","/foo/","bar", "//",
        NULL
    };

    long items = 0;
    while(topics[items] != NULL) items++;

    for (long i = 0; i < items; i++) {
        raxInsert(t, (unsigned char*)topics[i], strlen(topics[i]), (void*)i, NULL);
    }

    raxShow(t);

    raxIterator iter;
    raxStart(&iter, t);
    raxSeek(&iter, "^", NULL, 0);

    while(raxNext(&iter)) {
        printf("%.*s:%lu\n", (int)iter.key_len, (char*)iter.key, (uintptr_t)iter.data);
    }


    char pubtopic[] = "sport/tennis/matches/france/amateur/af1";
    char* ptopic = pubtopic;
    #define MAX_TOKENS 32
    char* tokenv[MAX_TOKENS];
    char** ptv = tokenv;
    int numtokens;

    for (numtokens = 0; numtokens < MAX_TOKENS; numtokens++, ptv++) {
        *ptv = strsep(&ptopic, "/");
        if (*ptv == NULL) break;
    }

    printf("\npubtopic: %s\n", pubtopic);
    for (int i = 0; i < numtokens; i++) printf("%s\n", tokenv[i]);

    #define MAX_TOPIC 256
    char topic[MAX_TOPIC] = "";
    for (int i = 0; i < numtokens; i++) {
        strlcat(topic, "#", MAX_TOPIC);
        printf("topic: %s\n", topic);
        void* result = raxFind(t, (uint8_t*)topic, strlen(topic));

        if (result == raxNotFound) {
            topic[strlen(topic) - 1] = '+';
            printf("topic: %s\n", topic);
            result = raxFind(t, (uint8_t*)topic, strlen(topic));
        }

        if (result == raxNotFound) {
            topic[strlen(topic) - 1] = '\0';
        }
        else {
            printf("found: %s:%lu\n", topic, (uintptr_t)result);
            break;
        }

        strlcat(topic, tokenv[i], MAX_TOPIC);
        strlcat(topic, "/", MAX_TOPIC);
    }


    // raxSeek(&iter,"=","sport/tennis/matches",0);

    //free(ptopic); // tokenv entries point into ptopic
    raxStop(&iter);
    raxFree(t);
    return 0;
}

int main(int argc, char** argv) {
    rc4srand(1234);
    return topic_fun();
}
