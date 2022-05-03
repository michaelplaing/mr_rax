// topics.c

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "mr_rax/mr_rax.h"
#include "mr_rax_internal.h"

int topic_fun(void) {
    raxSetDebugMsg(0);
    rax* tc_tree = raxNew();
    rax* client_tree = raxNew();

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
        // "$share/bam/foo/bar:9;10;11",
        "$share/bazzle/foo/bar:6",
        "+/bar:7",
        "foo/#:8;1",
        "$SYS/foo/#:1",
        "酒/吧:8",
        // "A:9",
        // "B:9",
        // "C:9",
        // "C/V:9",
        // "C/W:9",
        // "C/Y:9",
        // "C/Z:9",
        // "D:9",
        // "E:9",
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
            mr_insert_subscription(tc_tree, client_tree, subtopic, client);
        }
    }

    // char pubtopic[] = "sport/tennis/matches";
    char pubtopic2[] = "baz/bam";
    char pubtopic3[] = "foo/baz";
    // char pubtopic[] = "s";
    char pubtopic[] = "foo/bar";

    char subtopic2[] = "$SYS/foo/#";int client2 = 1;
    //mr_remove_subscription(tc_tree, client_tree, subtopic2, client2);
    // char subtopic3[] = "foo/#";int client3 = 1;
    // mr_remove_subscription(tc_tree, client_tree, subtopic3, client3);
    // char subtopic4[] = "foo/bar";int client4 = 1;
    // mr_remove_subscription(tc_tree, client_tree, subtopic4, client4);
    //char subtopic2[] = "foo/bar/"; int client2 = 3;
    //mr_remove_subscription(tc_tree, client_tree, subtopic2, client2);
    //mr_remove_client_subscriptions(tc_tree, client_tree, client2);

    // mr_upsert_client_topic_alias(client_tree, 1, true, pubtopic, 1);
    // mr_upsert_client_topic_alias(client_tree, 1, true, pubtopic2, 2);
    // mr_upsert_client_topic_alias(client_tree, 1, true, pubtopic, 2);
    // uint8_t alias;
    // int rc = mr_get_alias_by_topic(client_tree, 1, true, pubtopic, &alias);
    // printf("mr_get_alias_by_topic:: rc: %d; alias: %u\n", rc, alias);
    // char aliastopic[MAX_TOPIC_LEN];
    // rc = mr_get_topic_by_alias(client_tree, 1, true, 2, aliastopic);
    // printf("mr_get_topic_by_alias:: rc: %d; aliastopic: %s\n", rc, aliastopic);
    mr_upsert_client_topic_alias(client_tree, 1, true, pubtopic2, 8);
    mr_upsert_client_topic_alias(client_tree, 1, false, pubtopic, 8);
    // mr_upsert_client_topic_alias(client_tree, 1, true, pubtopic2, 9);
    //uint8_t alias;
    //int rc = mr_get_alias_by_topic(client_tree, 1, false, pubtopic, &alias);
    //printf("mr_get_alias_by_topic:: rc: %d; alias: %u\n", rc, alias);
    // char aliastopic[MAX_TOPIC_LEN];
    // rc = mr_get_topic_by_alias(client_tree, 1, false, 3, aliastopic);
    // printf("mr_get_topic_by_alias:: rc: %d; aliastopic: %s\n", rc, aliastopic);
    //mr_remove_client_topic_aliases(client_tree, 1);
    //mr_remove_client_data(tc_tree, client_tree, 1);

    // raxShowHex(tc_tree);
    raxShowHex(client_tree);
    // printf("raxShowHex\n");
    // raxIterator titer;
    // raxStart(&titer, tc_tree);
    // uint8_t val[4] = {'@','C','A'};
    // raxSeek(&titer, "<", val, 3);
    // raxNext(&titer);
    // printf("Found:: Key: %.*s; len: %zu\n", (int)titer.key_len, titer.key, titer.key_len);
    // raxStop(&titer);

    printf("get matching clients for '%s'\n", pubtopic);

    rax* client_set = raxNew();
    mr_get_subscribed_clients(tc_tree, client_set, pubtopic);
    raxIterator siter;
    raxStart(&siter, client_set);

    raxSeekSet(&siter);

    while(raxNextInSet(&siter)) {
        uint64_t client = 0;
        for (int i = 0; i < 8; i++) client += siter.key[i] << ((8 - i - 1) * 8);
        printf(" %llu", client);
    }

    puts("");

    // raxShowHex(client_set);

    // raxStop(&siter);
    // raxFree(client_set);

    // char topic[MAX_TOPIC_LEN];
    // mr_get_normalized_topic(pubtopic, topic);
    // printf("raxSeekChildren for '%s'\n", topic);
    // printf("raxFind %s:: value: %lu\n", topic, (uintptr_t)raxFind(tc_tree, (uint8_t*)topic, strlen(topic)));
    // raxIterator tciter;
    // raxStart(&tciter, tc_tree);
    // raxSeekChildren(&tciter, (uint8_t*)topic, strlen(topic));

    // while(raxNextChild(&tciter)) {
    //     printf("Next Key: %.*s\n", (int)tciter.key_len, tciter.key);
    // }

    // raxStop(&tciter);

    raxFree(client_tree);
    raxFree(tc_tree);
    return 0;
}

int main(int argc, char** argv) {
    return topic_fun();
}
