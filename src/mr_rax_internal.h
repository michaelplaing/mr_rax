#ifndef MR_RAX_INTERNAL_H
#define MR_RAX_INTERNAL_H

#include "mr_rax/rax.h"

int mr_get_normalized_topic(const char* pubtopic, char* topic, char* topic_key);
int mr_get_subscribe_topic(const char* subtopic, char* topic, char* share, char* topic_key);

#endif // MR_RAX_INTERNAL_H