## **mr_rax**: Service MQTT data requirements using Rax

The **mr_rax** public functions are:

- ``mr_insert_subscription()``: Insert an MQTT subscription topic (with optional wildcards) and a Client ID.

- ``mr_remove_subscription()``: Remove an MQTT subscription topic for a Client ID trimming the tree as needed.

- ``mr_remove_client_subscriptions()``: Remove all subscriptions for a client.

- ``mr_get_subscribed_clients()``: For a published topic return the dedup'd sorted set of Client IDs from all matching subscriptions. MQTT shared subscriptions are fully supported.

- ``mr_upsert_client_topic_alias()``: Insert or update a topic/alias pair for a client.

- ``mr_remove_client_topic_aliases()``: Remove all aliases for a client.

- ``mr_get_alias_by_topic()``: Get the alias for a topic and client, if any.

- ``mr_get_topic_by_alias()``: Get the topic for an alias and client, if any.

- ``mr_remove_client_data()``: Remove all subscriptions and other data for a client.

This project is set up for use as one of the CMake subprojects in a comprehensive MQTT project(s).

## The Topic Tree

The external Topic Tree conforms to MQTT.

The internal Topic Tree is composed of:

- The root;
- The hierarchy token:
    - ``@`` for the normal hierarchy; and
    - ``$`` for topics starting with '\$';
- For shared subscriptions, the topic is placed in the hierarchy above and the rest treated as described below:
- The topic tokens (the 0-length token is represented by ``0x1f`` which is invalid in MQTT).

There is no separator between the tokens.

Then for normal subscription clients:
- ``0xff`` as the Client Mark (invalid UTF-8); and
- The VBI-encoded Client ID.

Note: Variable Byte Integer (VBI) is an encoding of 7 bits per byte with a continuation bit in the high order spot. The bytes of the encoded integer are in big endian (network) order. This project uses 64 bit integer client IDs; hence a maximum of 10 bytes  are sufficient for encoding.

And for shared subscription clients:
- ``0xef`` as the Shared Mark (invalid UTF-8);
- the share name;
- ``0xff`` as the Client Mark; and
- 8 bytes of Client ID in big endian order.

### Subscription examples:

If the subscription topic is ``foo/bar`` and the Client ID is ``1``, then the normalized entry in the Topic Tree would be:

``@foobar<0xff><0x01>``

And maybe there is another client subscribed to that same topic:

``@foobar<0xff><0x02>``

A subscription with a 0-length token looks like this - topic ``foo/bar/``; Client ID ``3``:

``@foobar<0x1f><0xff><0x03>``

A shared subscription is like this - topic ``$share/baz/foo/bar``; Client ID ``4``:

``@foobar<0xef>baz<0xff><0x04>``

... and it's more interesting if there is more than one client sharing the same subscription:

``@foobar<0xef>baz<0xff><0x05>``

Shared subscriptions can form a complex subhierarchy, e.g. - topic ``$share/bazzle/foo/bar``; Client ID ``6``:

``@foobar<0xef>bazzle<0xff><0x06>``

A plus wildcard can replace any token, e.g. topic ``+/bar``; Client ID ``7``:

``@+bar<0xff><0x07>``

A hash wildcard can be the last token at any level, e.g. topic ``foo/#``; Client ID ``8``:

``@foo#<0xff><0x08>``

Of course a client can have any number of subscriptions and vice versa, e.g. topic ``foo/#``; Client ID ``1``:

``@foo#<0xff><0x01>``

And subscribe to a ``$SYS`` topic as well, e.g. topic ``$SYS/foo/#``; Client ID ``1``:

``$$SYSfoo#<0xff><0x01>``

Valid UTF-8 is fully supported, e.g. topic ``酒/吧``; Client ID ``8``:

``@酒吧<0xff><0x08>``

Note: ``/`` is a valid token separator for all UTF-8 character strings as it cannot be confused with any valid adjacent UTF-8 byte.

### The Rax tree implementation

Rax is a binary character based adaptive radix prefix tree. This means that common prefixes are combined and node sizes vary depending on prefix compression, node compression and the number of children (radix), which can be 0 to 256 (hence adaptive). A key is a sequence of bytes that can be "inserted" and/or "found". Optionally a key can have associated data: a pointer or a scalar – this capability is not currently used in the `mr_rax` project. The keys are maintained in lexicographic order within the tree's hierarchy.

There is much more information in the Rax README and ``rax.c``.

Some additions and modifications have been made to Rax including the following added functions needed by the ``mr_*()`` functions above. There are enhancements to avoid repetitive scanning of node data for the next child node index, which are particularly important for scanning wide spans of binary Client IDs.

- ``raxSeekChildren()``: Seek a key in order to get its immediate child keys.

- ``raxNextChild()``: Return the next immediate child key of the key sought above.

- ``raxSeekSubtree()``: Seek a key in order to get it and its subtree keys using ``raxNext()``.

To further speed up finds and traverses in a Rax tree, especially when handling related keys, the following additions make
use of state to proceed in their tasks differentially from the previous state.

- ``raxFindRelative()``: Find the value of a key relative to the previous key.

- ``raxSeekRelative()``: Seek a key relative to the previous key.

- ``raxSeekChildrenRelative()``: Seek a key relative to the previous key in order to get its immediate child keys.

- ``raxSeekSubtreeRelative()``: Seek a key relative to the previous key in order to get it and its subtree keys using ``raxNext()``.

For easier visualization of binary data, e.g. Client IDs and timestamps:

- ``raxShowHex()``

These functions remove all the keys and nodes in a subtree.

- ``raxFreeSubtree()``

- ``raxFreeSubtreeWithCallback()``

### Overlaying the Topic Tree on Rax

Each token subtree in a normalized topic is stored as a key. The client mark subtree, shared mark subtree and share subtree are also keys. Finally, the entire topic with Client ID is a key. Hence inserting ``@foobar<0xff><0x0000000000000001>`` would result in the following 5 keys:
```
@
↑
@foo
   ↑
@foobar
      ↑
@foobar<0xff>
           ↑
@foobar<0xff><0x01>
                 ↑
```
Due to prefix compression, storage for the 5 keys would look like:
```
@foobar<0xff><0x01>
↑  ↑  ↑    ↑     ↑
```

When ``@foobar<0xff><0x02>`` is also inserted, we get:
```
@foobar<0xff>
↑  ↑  ↑    ↑\
            |<0x01>
            \    ↑
             <0x02>
                 ↑
```

The additions to Rax include ``raxShowHex()``. When the 11 subscriptions above are applied to the Topic Tree they result in the following ASCII art of the Rax internal structures, illustrating prefix compression, node compression and adaptive node sizes:
```
[$@]
 `-($) "$SYS" -> "foo" -> [#] -> [0xff] -> [0x01] -> []
 `-(@) [0x2b66e9]
        `-(+) "bar" -> [0xff] -> [0x07] -> []
        `-(f) "oo" -> [#b]
                       `-(#) [0xff] -> [0x0108]
                                        `-(.) []
                                        `-(.) []
                       `-(b) "ar" -> [0x1ffeff]
                                      `-(.) [0xff] -> [0x03] -> []
                                      `-(.) "baz" -> [0x7aff]
                                                      `-(z) "le" -> [0xff] -> [0x06] -> []
                                                      `-(.) [0x0405]
                                                             `-(.) []
                                                             `-(.) []
                                      `-(.) [0x0102]
                                             `-(.) []
                                             `-(.) []
        `-(.) "0x8592" -> "0xe590a7" -> [0xff] -> [0x08] -> []
```
A full explanation of the notation above is in the Rax README and ``rax.c``; a tricky part is that edge bytes pointing to nodes are not stored in the nodes themselves.

### The Topic Tree search strategy

This is the strategy used by ``mr_get_subscribed_clients()`` to extract a dedup'd list of subscribed Client IDs when provided with a valid publish topic (no wildcards).

The strategy is repeatedly executed in 2 phases until done:

1) a subscribe topic (may have wildcards) is found in the Topic Tree that matches the publish topic; then

2) the normal and/or shared subscription Client IDs associated with the subscribe topic, if there are any, are found and added to the unique set of Client IDs.

This is repeated until all possible matches have been found and the Client IDs noted.

For phase 1, the internally formatted publish topic is tokenized using '/' as the separator. For example, the external publish topic ``foo/bar`` is tokenized as: ``@``; ``foo``; ``bar``.

Then 3 searches are performed in order at each level of the Topic Tree except for the last which has 1 search. For the example the levels are: ``@``; ``@foo``, ``@foobar`` and the search predicates are: ``@#``, ``@+``, ``@foo``; ``@foo#``, ``@foo+``, ``@foobar``; ``@foobar#``. The last search is necessary because ``#`` matches the level above.

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

1) Append the Client Mark (``0xff``) to the current key and search for the key. If found, iterate over its Client ID children inserting each Client ID into the result set.

2) Append the Shared Mark (``0xef``) to the current key and search for it. If found iterate over the share name children, e.g. ``baz``, and, for each share name, append the Client Mark, get its Client ID children, then select one at random and insert it into the result set.

Running ``mr_get_subscribed_clients()`` using publish topic ``foo/bar`` against our Topic Tree above results in Client IDs: `` 1 2 4 6 7 8``. Repeatedly running it will result in `` 1 2 5 6 7 8`` about half the time – this is due to the share ``baz`` being shared by clients ``4`` and ``5`` whereas share ``bazzle`` has a single client and the other subscriptions are normal.

Note also that Client ID ``1`` only appears once although it is present in 2 matching subscriptions: ``foo/bar`` and ``foo/#``; also Client ID ``3`` is not present since subscription topic ``foo/bar/`` does not match the publish topic.

The combination of compression, which shortens the key path, with relative key searches yields an average search key depth descending toward 1. Hence, although there are many searches in the strategy, performance is near linear, dependent upon the number of tokens in the publish topic and the number of ``+`` wildcards found in the Topic Tree, which have a multiplicative effect.

### The Client tree

This tree contains a subscriptions inversion for each client, topic aliases for clients, and will contain other client-based information.

The client tree uses the external format for both subscribe and publish topics.

Topic aliases are in 2 distinct sets: ones set by the client and those set by the server. Hence there are 2 pairs (handling inversion) of synchronized subtrees for each client, providing alias-by-topic and topic-by-alias for client and server aliases.

Adding incoming topic alias ``8`` for Client ID ``1`` topic ``baz/bam`` plus outgoing alias ``8`` for Client ID ``1`` topic ``foo/bar`` then running ``raxShowHex()`` yields the following depiction of our 8 clients, their 11 subscriptions and the 2 aliases in the client tree:

```
"0x00000000000000" -> [0x0102030405060708]
        `-(.) [as]
               `-(a) "liases" -> [cs]
                                  `-(c) "lient" -> [at]
                                                    `-(a) "bt" -> "baz/bam" -> [0x08] -> []
                                                    `-(t) "ba" -> [0x08] -> "baz/bam" -> []
                                  `-(s) "erver" -> [at]
                                                    `-(a) "bt" -> "foo/bar" -> [0x08] -> []
                                                    `-(t) "ba" -> [0x08] -> "foo/bar" -> []
               `-(s) "ubs" -> [$f]
                               `-($) "SYS/foo/#" -> []
                               `-(f) "oo/" -> [#b]
                                               `-(#) []
                                               `-(b) "ar" -> []
        `-(.) "subs" -> "foo/bar" -> []
        `-(.) "subs" -> "foo/bar/" -> []
        `-(.) "subs" -> "$share/baz/foo/bar" -> []
        `-(.) "subs" -> "$share/baz/foo/bar" -> []
        `-(.) "subs" -> "$share/bazzle/foo/bar" -> []
        `-(.) "subs" -> "+/bar" -> []
        `-(.) "subs" -> [0x66e9]
                         `-(f) "oo/#" -> []
                         `-(.) "0x85922fe590a7" -> []
```