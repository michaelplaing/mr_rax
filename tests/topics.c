// topics.c

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "mr_rax/mr_rax.h"
#include "mr_rax_internal.h"

int topic_fun(void) {
    rax* tcrax = raxNew();
    rax* crax = raxNew();

    char* subtopicclientv[] = {
        // "$share/bom/bip/bop:21",
        // "$sys/baz/bam:20",
        // "$share/bom/sport/tennis/matches/#:1001;1002;1003;1022;1023",
        // "$share/bip/sport/tennis/matches/france/amateur/+:2023;2024;2004;2005;2006;2002;2003",
        // "$share/foo/$sys/baz/bam:25",
        // "sport/tennis/matches:1;2;3",
        // "sport/tennis/matches/italy/#:4",
        // "sport/tennis/matches/italy/#:5",
        // "sport/tennis/matches/united states/amateur/+:6",
        // "sport/tennis/matches/#:7;8",
        // "sport/tennis/matches/+/professional/+:3;5",
        // "sport/tennis/matches/+/amateur/#:9",
        // "sport/tennis/matches/italy/professional/i6:12;1",
        // "sport/tennis/matches/italy/professional/i5:2",
        // "sport/tennis/matches/ethiopia/professional/e4:11;7;1",
        // "sport/tennis/matches/ethopia/professional/e10:21;27",
        // "sport/tennis/matches/england/professional/e1:10",
        // "sport/tennis/matches/england/professional/e2:13;9",
        // "sport/tennis/matches/england/professional/e3:9;13",
        // "sport/tennis/matches/france/professional/f4:14",
        // "sport/tennis/matches/france/amateur/af1:6",
        // "sport/tennis/matches/france/amateur/af2:66",
        // "sport/tennis/matches/france/amateur/af1/#:99",
        // "sport/tennis/matches/france/amateur/af1/\":999",
        // "sport/tennis/matches/france/amateur/af1/foo:9999",
        // "sport/+/+/+/amateur/#:10;11;9",
        // "sport/tennis/matches/united states/amateur/au2:2;4;8;10",
        // "/foo:1;2",
        //"/f:1",
        // "/foo/:3;5",
        // "bar:2;3",
        //"//:15;16",
        // "#:100",
        // "s:1",
        // "s/#:2",
        // "foo:9;10;1;2",
        "foo/bar:1;2",
        "foo/bar/:3",
        "$share/baz/foo/bar:4;5",
        "+/bar:6",
        "foo/#:7;1",
        "$SYS/foo/#:1",
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
            mr_insert_subscription(tcrax, crax, subtopic, client);
        }
    }

    char pubtopic[] = "sport/tennis/matches";
    char pubtopic2[] = "baz/bam";
    char pubtopic3[] = "foo/bar";
    // char pubtopic[] = "s";
    // char pubtopic[] = "/foo/";

    // mr_upsert_client_topic_alias(crax, 1, pubtopic, 1);
    mr_upsert_client_topic_alias(crax, 1, pubtopic2, 8, true);
    mr_upsert_client_topic_alias(crax, 1, pubtopic3, 8, false);

    raxShowHex(tcrax);
    raxShowHex(crax);

    printf("get matching clients for '%s'\n", pubtopic);

    rax* srax = raxNew();
    mr_get_subscribed_clients(tcrax, srax, pubtopic);
    raxIterator siter;
    raxStart(&siter, srax);

    raxSeekChildren(&siter, NULL, 0);

    while(raxNextChild(&siter)) {
        uint64_t client = 0;
        for (int i = 0; i < 8; i++) client += siter.key[i] << ((8 - i - 1) * 8);
        printf(" %llu", client);
    }

    puts("");

    // raxShowHex(srax);

    raxStop(&siter);
    raxFree(srax);

    char topic[MAX_TOPIC_LEN];
    mr_get_normalized_topic(pubtopic, topic);
    printf("raxSeekChildren for '%s'\n", topic);
    // printf("raxFind %s:: value: %lu\n", topic, (uintptr_t)raxFind(tcrax, (uint8_t*)topic, strlen(topic)));
    raxIterator tciter;
    raxStart(&tciter, tcrax);
    raxSeekChildren(&tciter, (uint8_t*)topic, strlen(topic));

    while(raxNextChild(&tciter)) {
        printf("Next Key: %.*s\n", (int)tciter.key_len, tciter.key);
    }

    raxStop(&tciter);

    raxFree(tcrax);
    return 0;
}

int main(int argc, char** argv) {
    return topic_fun();
}
