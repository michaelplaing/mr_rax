## mr_rax: functions to handle MQTT data requirements using Rax

The mr_rax public functions so far are:

- ``mr_insert_subscription()``: Insert an MQTT subscription topic (with optional wildcards) and a Client ID.

- ``mr_get_subscribed_clients()``: For a published topic return the dedup'd set of Client IDs from all matching subscriptions. ``raxSeekChildren()`` (see below) will efficiently iterate through this set which is a Rax tree with depth 1.

- ``mr_upsert_client_topic_alias()``: Insert or update a topic/alias pair for a client.

- ``mr_get_alias_by_topic()``: Get the alias for a topic and client, if any.

- ``mr_get_topic_by_alias()``: Get the topic for an alias and client, if any.

Note: MQTT shared subscriptions are fully supported.

More functions will be added to, e.g., delete subscriptions.

This project is set up to use as one of the CMake subprojects in a comprehensive MQTT project(s).

Some additions and modifications have also been made to Rax itself to support the following functions needed by the above. There is also some experimental code included to avoid repetitive scanning of node data for the next child node index, which may be particularly important for the anticipated wide spans of binary Client IDs.

- ``raxSeekChildren()``: Seek a key in order to get its immediate child keys. A key of NULL seeks the root which is useful for handling a Rax tree of depth 1.

- ``raxNextChild()``: Return the next immediate child key of the key sought above.

And for easier visualization of binary data, e.g. Client IDs:

- ``raxShowHex()``

## The unified topic/client (TC) tree

The external TC tree conforms to MQTT.

The internal TC tree is composed of:

- The root
- The hierarchy token:
    - ``@`` for the normal hierarchy
    - ``$`` for topics starting with '\$'
- For shared subscriptions, the topic is placed in the hierarchy above and the rest treated as described below.
- The topic tokens (the 0-length token is represented by ``0x1f`` which is invalid in MQTT)
- ``/`` as the separator between the above tokens

Then for normal subscription clients:
- ``0xef`` as the Client Mark (invalid UTF8)
- 8 bytes of Client ID in big endian (network) order

And for shared subscription clients:
- ``0xff`` as the Shared Mark (invalid UTF8)
- the share name
- 8 bytes of Client ID in big endian order

### Subscription examples:

If the subscription topic is ``foo/bar`` and the Client ID is ``1``, then the normalized entry in the TC tree would be:

``@/foo/bar<0xef><0x0000000000000001>``

And maybe there is another client subscribed to that same topic:

``@/foo/bar<0xef><0x0000000000000002>``

A subscription with a 0-length token looks like this - topic ``foo/bar/``; Client ID ``2``:

``@/foo/bar/<0x1f><0xef><0x0000000000000003>``

A shared subscription is like this - topic ``$share/baz/foo/bar``; Client ID ``3``:

``@/foo/bar<0xff>baz<0x0000000000000004>``

... and it's more interesting if there is more than one client sharing the same subscription:

``@/foo/bar<0xff>baz<0x0000000000000005>``

A plus wildcard can replace any token, e.g. topic ``+/bar``; Client ID ``5``:

``@/+/bar<0xef><0x0000000000000006>``

A hash wildcard can be the last token at any level, e.g. topic ``foo/#``; Client ID ``7``:

``@/foo/#<0xef><0x0000000000000007>``

And of course a client can have any number of subscriptions and vice versa, e.g. topic ``foo/#``; Client ID ``1``:

``@/foo/#<0xef><0x0000000000000001>``

And subscribe to a ``$SYS`` topic as well, e.g. topic ``$SYS/foo/#``; Client ID ``1``:

``$/$SYS/foo/#<0xef><0x0000000000000001>``

### The Rax tree implementation

Rax is a binary character based adaptive radix prefix tree. This means that common prefixes are combined and node sizes vary depending on prefix compression, node compression and the number of children. A key is a sequence of characters that can be "inserted" and/or "found". Optionally a key can have associated data. The keys are maintained in lexicographic order within the tree's hierarchy.

There is much more information in the Rax README and ``rax.h``.

### Overlaying the TC tree on Rax

Each token subtree in a normalized topic is stored as a key. The client mark subtree, shared mark subtree and share subtree are also keys. Finally, the entire topic with Client ID is a key. Hence inserting ``@/foo/bar<0xef><0x0000000000000001>`` would result in the following 5 keys:
```
@
@/foo
@/foo/bar
@/foo/bar<0xef>
@/foo/bar<0xef><0x0000000000000001>
```
Due to prefix compression, storage for the 5 keys would look like:
```
@/foo/bar<0xef><0x0000000000000001>
↑   ↑   ↑    ↑                   ↑
```

When ``@/foo/bar<0xef><0x0000000000000002>`` is also inserted, we get:
```
@/foo/bar<0xef><0x00000000000000>
↑   ↑   ↑    ↑                 \
                               |<0x01>
                               \    ↑
                                <0x02>
                                    ↑
```

The additions to Rax include ``raxShowHex()``. When the 9 subscriptions above are applied to the TC tree they result in the following ASCII art of the Rax internal structures, illustrating prefix compression, node compression and adaptive node sizes:
```
[$@]
 `-($) "/$SYS"=0x1 -> "/foo"=0x1 -> "/#"=0x1 -> [0xfe]=0x1 -> "0x0000000000000001"=0x1 -> []
 `-(@) [/]=0x8 -> [+f]
                   `-(+) "/bar"=0x1 -> [0xfe]=0x1 -> "0x0000000000000006"=0x1 -> []
                   `-(f) "oo" -> [/]=0x7 -> [#b]
                                             `-(#) [0xfe]=0x2 -> "0x00000000000000"=0x2 -> [0x0107]
                                                                                            `-(.) []
                                                                                            `-(.) []
                                             `-(b) "ar" -> [0x2ffeff]=0x5
                                                            `-(/) [0x1f] -> [0xfe]=0x1 -> "0x0000000000000003"=0x1 -> []
                                                            `-(.) "0x00000000000000"=0x2 -> [0x0102]
                                                                                             `-(.) []
                                                                                             `-(.) []
                                                            `-(.) "baz"=0x2 -> "0x00000000000000"=0x2 -> [0x0405]
                                                                                                          `-(.) []
                                                                                                          `-(.) []
```
A full explanation of the notation above is in the Rax README and ``rax.h``; a tricky part is that the first character of a key is stored in the node pointing to the key, not in the key itself.

Each key except for leaf Client IDs has an integer value associated with it which is the count of Client IDs in its subtree, e.g. the ``0x8`` associated with key ``@``. This is currently useful in randomly picking a Client ID when matching a shared subscription and in pruning the tree as subscriptions are deleted. The Rax tree itself maintains total counts of all keys and nodes.

### TC tree search strategy

This is the strategy used by ``mr_get_subscribed_clients()`` to extract a dedup'd list of subscribed Client IDs when provided with a valid publish topic (no wildcards).

The strategy is executed in 2 parts:

1) a subscribe topic (may have wildcards) is found in the TC tree that matches the publish topic; then

2) the normal and/or shared subscription Client IDs associated with the subscribe topic, if there are any, are found and added to the unique set of Client IDs.

This is repeated until all possible matches have been found and the Client IDs noted.

For part 1, the internally formatted publish topic is tokenized using '/' as the separator. For example, the external publish topic ``foo/bar`` is tokenized as: ``@``; ``foo``; ``bar``. The tokens are kept in a vector.

Then 3 searches are performed in order at each level of the TC tree except for the last which has 1 search. For example the levels are: ``@``; ``@/foo``, ``@/foo/bar`` and the search predicates are: ``@/#``, ``@/+``, ``@/foo``; ``@/foo/#``, ``@/foo/+``, ``@/foo/bar``; ``@/foo/bar/#``. The last search is necessary because ``#`` matches the level above.

1) For a found search predicate ending in ``#``, we can gather its Client IDs and continue the searches unless this is the last level, in which case we are done with this subtree.

2) If the found search predicate ends in ``+``, we take one of 2 routes:

    - if this is the next to last level, gather its Client IDs and continue the searches; otherwise

    - search the new ``+`` subtree forking from this one and then continue the searches in this subtree.

3) Finally we try the search predicate ending in an explicit token, like ``bar``:

    - if it is found and it is the next to last level, gather its Client IDs and continue the searches (to search for ``#``); else

    - if it is found but above the next to last level just continue the searches; else

    - if it is not found there are no more possible matches; we are done with this subtree.

When we have finished all subtrees, including those necessary to handle ``+`` wildcards, we are done with part 1.

For part 2 of the strategy, we proceed in 2 steps to gather Client IDs:

1) Append the Client Mark (``0xef``) to the current key and search for it. If found, iterate over its Client ID children (the Client IDs) inserting each into the result set.

2) Append the Shared Mark (``0xff``) to the current key and search for it. If found iterate over the share name children and, for each share name, get its Client ID children selecting one at random for insertion into the result set.

Running ``mr_get_subscribed_clients()`` using publish topic ``foo/bar`` against our TC tree above results in Client IDs: `` 1 2 4 6 7``. Running it more often results in `` 1 2 5 6 7`` – this is due to the share ``baz`` being shared by clients ``4`` and ``5`` whereas the other subscriptions are normal. Note also that Client ID ``1`` only appears once although it is present in 2 matching subscriptions: ``foo/bar`` and ``foo/#``.

### The Client tree

This tree contains a subscriptions inversion for each client, topic aliases for clients, and will contain other client-based information.

Topic aliases are distinct for incoming ones, which are set by the client, and outgoing ones set by the server. Hence there are 2 pairs (handling inversion) of synchronized subtrees: ``iabt`` / ``itba`` and ``oabt`` / ``otba`` for each client, providing alias-by-topic and topic-by-alias respectively for incoming (client) and outgoing (server) aliases. The topic alias leaf values are used to store the alias and the topic pointer – this usage simplifies the overwriting of aliases as allowed by MQTT.

Adding incoming topic alias ``8`` for Client ID ``1`` topic ``baz/bam`` plus outgoing alias ``8`` for Client ID ``1`` topic ``foo/bar`` then running ``raxShowHex()`` yields the following depiction of our 7 clients, their 9 subscriptions and the 2 aliases in the client tree:

```
"0x00000000000000" -> [0x01020304050607]
        `-(.) [ios]
               `-(i) [at]
                      `-(a) "bt" -> "baz/bam" -> []=0x8
                      `-(t) "ba" -> [0x08] -> []=0x100290088
               `-(o) [at]
                      `-(a) "bt" -> "foo/bar" -> []=0x8
                      `-(t) "ba" -> [0x08] -> []=0x1002900a0
               `-(s) "ubs" -> [$f]
                               `-($) "SYS/foo/#" -> []
                               `-(f) "oo/" -> [#b]
                                               `-(#) []
                                               `-(b) "ar" -> []
        `-(.) "subs" -> "foo/bar" -> []
        `-(.) "subs" -> "foo/bar/" -> []
        `-(.) "subs" -> "$share/baz/foo/bar" -> []
        `-(.) "subs" -> "$share/baz/foo/bar" -> []
        `-(.) "subs" -> "+/bar" -> []
        `-(.) "subs" -> "foo/#" -> []
```
