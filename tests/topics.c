// topics.c

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mr_rax/mr_rax.h"

int topic_fun(void) {
    rax* prax = raxNew();

    char* subtopicclientv[] = {
        "$share/bom/bip/bop:21",
        "$sys/baz/bam:20",
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
        "sport/tennis/matches/ethopia/professional/e10:21;27",
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
        // "foo/bar:1;2",
        // "foo/bar/:3",
        // "$share/baz/foo/bar:4;5",
        // "+/bar:6",
        // "foo/#:7",
    };

    size_t numtopics = sizeof(subtopicclientv) / sizeof(subtopicclientv[0]);

    char* tokenv[MAX_TOKENS];
    size_t numtokens;
    char subtopic[MAX_TOPIC_LEN];
    char subtopicclient[MAX_TOPIC_LEN];

    for (int i = 0; i < numtopics; i++) {
        strcpy(subtopicclient, subtopicclientv[i]);
        char* pc = strchr(subtopicclient, ':');
        *pc = '\0';
        strlcpy(subtopic, subtopicclient, MAX_TOPIC_LEN);
        char* unparsed_clients = pc + 1;
        char* clientstr;

        while ((clientstr = strsep(&unparsed_clients, ";")) != NULL) {
            uint64_t client = strtoull(clientstr, NULL, 0);
            mr_insert_subscription(prax, subtopic, client);
        }
    }

    // raxShowHex(prax);

    char pubtopic[] = "sport/tennis/matches";
    // char pubtopic[] = "sport/tennis/matches/france/amateur/af1";
    // char pubtopic[] = "s";
    // char pubtopic[] = "/foo/";
    printf("get matching clients for '%s'\n", pubtopic);

    rax* crax = raxNew();
    mr_get_clients(prax, crax, pubtopic);
    raxIterator citer;
    raxStart(&citer, crax);
    raxSeek(&citer, "^", NULL, 0);

    while(raxNext(&citer)) {
        uint64_t client = 0;
        for (int i = 0; i < 8; i++) client += citer.key[i] << ((8 - i - 1) * 8);
        printf(" %llu", client);
    }

    puts("");

    // raxShowHex(crax);

    raxStop(&citer);
    raxFree(crax);

    char topic[MAX_TOPIC_LEN];
    mr_get_normalized_topic(pubtopic, topic);
    printf("raxSeekChildren for '%s'\n", topic);
    // printf("raxFind %s:: value: %lu\n", topic, (uintptr_t)raxFind(prax, (uint8_t*)topic, strlen(topic)));
    raxIterator piter;
    raxStart(&piter, prax);
    raxSeekChildren(&piter, (uint8_t*)topic, strlen(topic));

    while(raxNextChild(&piter)) {
        printf("Next Key: %.*s\n", (int)piter.key_len, piter.key);
    }

    raxStop(&piter);

    raxFree(prax);
    return 0;
}

int main(int argc, char** argv) {
    return topic_fun();
}
