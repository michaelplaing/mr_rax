## **mr_rax**: Service MQTT data requirements using Rax

The **mr_rax** public functions are:

- ``mr_insert_subscription()``: Insert an MQTT subscription topic (with optional wildcards) and a Client ID.

- ``mr_remove_subscription()``: Remove an MQTT subscription topic for a Client ID trimming the tree as needed.

- ``mr_remove_client_subscriptions()``: Remove all subscriptions for a client.

- ``mr_get_subscribed_clients()``: For a published topic return the dedup'd sorted set of Client IDs from all matching subscriptions. ``raxSeekSet()/raxNextInSet()`` (see below) will efficiently iterate through this set. MQTT shared subscriptions are fully supported.

- ``mr_upsert_client_topic_alias()``: Insert or update a topic/alias pair for a client.

- ``mr_remove_client_topic_aliases()``: Remove all aliases for a client.

- ``mr_get_alias_by_topic()``: Get the alias for a topic and client, if any.

- ``mr_get_topic_by_alias()``: Get the topic for an alias and client, if any.

- ``mr_remove_client_data()``: Remove all subscriptions and other data for a client.

This project is set up for use as one of the CMake subprojects in a comprehensive MQTT project(s).

## The unified topic/client (TC) tree

The external TC tree conforms to MQTT.

The internal TC tree is composed of:

- The root;
- The hierarchy token:
    - ``@`` for the normal hierarchy; and
    - ``$`` for topics starting with '\$';
- For shared subscriptions, the topic is placed in the hierarchy above and the rest treated as described below:
- The topic tokens (the 0-length token is represented by ``0x1f`` which is invalid in MQTT).

There is no separator between the tokens.

Then for normal subscription clients:
- ``0xef`` as the Client Mark (invalid UTF8); and
- 8 bytes of Client ID in big endian (network) order.

And for shared subscription clients:
- ``0xff`` as the Shared Mark (invalid UTF8);
- the share name; and
- 8 bytes of Client ID in big endian order.

### Subscription examples:

If the subscription topic is ``foo/bar`` and the Client ID is ``1``, then the normalized entry in the TC tree would be:

``@foobar<0xef><0x0000000000000001>``

And maybe there is another client subscribed to that same topic:

``@foobar<0xef><0x0000000000000002>``

A subscription with a 0-length token looks like this - topic ``foo/bar/``; Client ID ``2``:

``@foobar<0x1f><0xef><0x0000000000000003>``

A shared subscription is like this - topic ``$share/baz/foo/bar``; Client ID ``3``:

``@foobar<0xff>baz<0x0000000000000004>``

... and it's more interesting if there is more than one client sharing the same subscription:

``@foobar<0xff>baz<0x0000000000000005>``

A plus wildcard can replace any token, e.g. topic ``+/bar``; Client ID ``5``:

``@+bar<0xef><0x0000000000000006>``

A hash wildcard can be the last token at any level, e.g. topic ``foo/#``; Client ID ``7``:

``@foo#<0xef><0x0000000000000007>``

And of course a client can have any number of subscriptions and vice versa, e.g. topic ``foo/#``; Client ID ``1``:

``@foo#<0xef><0x0000000000000001>``

And subscribe to a ``$SYS`` topic as well, e.g. topic ``$SYS/foo/#``; Client ID ``1``:

``$$SYSfoo#<0xef><0x0000000000000001>``

### The Rax tree implementation

Rax is a binary character based adaptive radix prefix tree. This means that common prefixes are combined and node sizes vary depending on prefix compression, node compression and the number of children (radix), which can be 0 to 256 (hence adaptive). A key is a sequence of characters that can be "inserted" and/or "found". Optionally a key can have associated data: a pointer or a scalar. The keys are maintained in lexicographic order within the tree's hierarchy.

There is much more information in the Rax README and ``rax.c``.

Some additions and modifications have been made to Rax including the following added functions needed by the ``mr_*()`` functions above. There is some experimental code to avoid repetitive scanning of node data for the next child node index, which is particularly important for scanning wide spans of binary Client IDs.

- ``raxSeekChildren()``: Seek a key in order to get its immediate child keys.

- ``raxNextChild()``: Return the next immediate child key of the key sought above.

- ``raxSeekSet()``:  Seek the beginning of a sorted set (a Rax tree with key depth 1), e.g. a set of Client IDs.

- ``raxNextInSet()``: Return the next key in the set sought above.

And for easier visualization of binary data, e.g. Client IDs:

- ``raxShowHex()``

The following functions set the added ``isscalar`` flag for a key to indicate that the associated value field is scalar data and not an allocated pointer - hence the value should not be freed.

- ``raxInsertScalar()``

- ``raxTryInsertScalar()``

This function removes a key and sets a pointer to the old scalar data.

- ``raxRemoveScalar()``

This one removes a key then sets a pointer to the old value and a pointer to the ``isscalar`` flag of the old value.

- ``raxRemoveWithFlag()``

This function frees the Rax tree and any allocated pointers associated with its keys; i.e. it does not try to free scalar data.

- ``raxFreeWithScalar()``

These functions remove all the keys in a subtree.

- ``raxFreeSubtree()``

- ``raxFreeSubtreeWithCallback()``

### Overlaying the TC tree on Rax

Each token subtree in a normalized topic is stored as a key. The client mark subtree, shared mark subtree and share subtree are also keys. Finally, the entire topic with Client ID is a key. Hence inserting ``@foobar<0xef><0x0000000000000001>`` would result in the following 5 keys:
```
@
↑
@foo
   ↑
@foobar
      ↑
@foobar<0xef>
           ↑
@foobar<0xef><0x0000000000000001>
                               ↑
```
Due to prefix compression, storage for the 5 keys would look like:
```
@foobar<0xef><0x0000000000000001>
↑  ↑  ↑    ↑                   ↑
```

When ``@foobar<0xef><0x0000000000000002>`` is also inserted, we get:
```
@foobar<0xef><0x00000000000000>
↑  ↑  ↑    ↑                 \
                               |<0x01>
                               \    ↑
                                <0x02>
                                    ↑
```

The additions to Rax include ``raxShowHex()``. When the 9 subscriptions above are applied to the TC tree they result in the following ASCII art of the Rax internal structures, illustrating prefix compression, node compression and adaptive node sizes:
```
[$@]
 `-($) "$SYS" -> "foo" -> [#] -> [0xfe] -> "0x0000000000000001" -> []
 `-(@) [+f]
        `-(+) "bar" -> [0xfe] -> "0x0000000000000006" -> []
        `-(f) "oo" -> [#b]
                       `-(#) [0xfe] -> "0x00000000000000" -> [0x0107]
                                                              `-(.) []
                                                              `-(.) []
                       `-(b) "ar" -> [0x1ffeff]
                                      `-(.) [0xfe] -> "0x0000000000000003" -> []
                                      `-(.) "0x00000000000000" -> [0x0102]
                                                                   `-(.) []
                                                                   `-(.) []
                                      `-(.) "baz" -> "0x00000000000000" -> [0x0405]
                                                                            `-(.) []
                                                                            `-(.) []
```
A full explanation of the notation above is in the Rax README and ``rax.c``; a tricky part is that the first character of a key is stored in the node pointing to the key, not in the key itself.

### The TC tree search strategy

This is the strategy used by ``mr_get_subscribed_clients()`` to extract a dedup'd list of subscribed Client IDs when provided with a valid publish topic (no wildcards).

The strategy is repeatedly executed in 2 phases until done:

1) a subscribe topic (may have wildcards) is found in the TC tree that matches the publish topic; then

2) the normal and/or shared subscription Client IDs associated with the subscribe topic, if there are any, are found and added to the unique set of Client IDs.

This is repeated until all possible matches have been found and the Client IDs noted.

For phase 1, the internally formatted publish topic is tokenized using '/' as the separator. For example, the external publish topic ``foo/bar`` is tokenized as: ``@``; ``foo``; ``bar``.

Then 3 searches are performed in order at each level of the TC tree except for the last which has 1 search. For the example the levels are: ``@``; ``@foo``, ``@foobar`` and the search predicates are: ``@#``, ``@+``, ``@foo``; ``@foo#``, ``@foo+``, ``@foobar``; ``@foobar#``. The last search is necessary because ``#`` matches the level above.

1) For a found search predicate ending in ``#``, we gather its Client IDs (phase 2) and continue the searches unless this is the last level, in which case we are done with this subtree.

2) If the found search predicate ends in ``+``, we take one of 2 routes:

    - if this is the next to last level, gather its Client IDs and continue the searches; otherwise

    - search the new ``+`` subtree forking from this one and then continue the searches in this subtree.

3) Finally we try the search predicate ending in an explicit token, like ``bar``:

    - if it is found and it is the next to last level, gather its Client IDs and continue the searches (to search for ``#``); else

    - if it is found but above the next to last level just continue the searches; else

    - if it is not found then there are no more possible matches; we are done with this subtree.

When we have finished all subtrees, including those necessary to handle ``+`` wildcards, we are done.

Phase 2 of the strategy, gathering Client IDs for a search predicate, proceeds in 2 steps:

1) Append the Client Mark (``0xef``) to the current key and search for the key. If found, iterate over its Client ID children inserting each Client ID into the result set.

2) Append the Shared Mark (``0xff``) to the current key and search for it. If found iterate over the share name children, e.g. ``baz``, and, for each share name, get its Client ID children, select one at random and insert it into the result set.

Running ``mr_get_subscribed_clients()`` using publish topic ``foo/bar`` against our TC tree above results in Client IDs: `` 1 2 4 6 7``. Repeatedly running it will result in `` 1 2 5 6 7`` about half the time – this is due to the share ``baz`` being shared by clients ``4`` and ``5`` whereas the other subscriptions are normal.

Note also that Client ID ``1`` only appears once although it is present in 2 matching subscriptions: ``foo/bar`` and ``foo/#``; also Client ID ``3`` is not present since subscription topic ``foo/bar/`` does not match the publish topic.

### The Client tree

This tree contains a subscriptions inversion for each client, topic aliases for clients, and will contain other client-based information.

The client tree uses the external format for both subscribe and publish topics.

Topic aliases are in 2 distinct sets: ones set by the client and those set by the server. Hence there are 2 pairs (handling inversion) of synchronized subtrees for each client, providing alias-by-topic and topic-by-alias for client and server aliases.

The topic alias leaf values are used to store the alias scalar and the topic pointer, since we do not need to search on them.

Adding incoming topic alias ``8`` for Client ID ``1`` topic ``baz/bam`` plus outgoing alias ``8`` for Client ID ``1`` topic ``foo/bar`` then running ``raxShowHex()`` yields the following depiction of our 7 clients, their 9 subscriptions and the 2 aliases in the client tree – the short hex values after the ``=`` in the leaf nodes are aliases and the longer ones are pointers to topic strings:

```
"0x00000000000000" -> [0x01020304050607]
        `-(.) [as]
               `-(a) "liases" -> [cs]
                                  `-(c) "lient" -> [at]
                                                    `-(a) "bt" -> "baz/bam" -> []=0x8
                                                    `-(t) "ba" -> [0x08] -> []=0x104844080
                                  `-(s) "erver" -> [at]
                                                    `-(a) "bt" -> "foo/bar" -> []=0x8
                                                    `-(t) "ba" -> [0x08] -> []=0x1048440a0
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
        `-(.) "subs" -> "foo/#" -> []```
