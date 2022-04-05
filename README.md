## mr_rax: functions to handle MQTT data requirements using Rax

The mr_rax public functions so far are:

- mr_insert_subscription: Insert an MQTT subscription topic (with optional wildcards) and a Client ID

- mr_get_clients: For a published topic return the dedup'd set of Client IDs from all matching subscriptions. raxSeekChildren (see below) will efficiently iterate through this set which is a Rax tree with depth 1.

Note: MQTT shared subscriptions are fully supported.

More functions will be added to, e.g., delete subscriptions.

This project is set up to use as one of the CMake subprojects in a comprehensive MQTT project(s).

Some additions and modifications have also been made to Rax itself to support the following functions needed by the above. There is also some experimental code included to avoid repetitive scanning of node data for the next child node index, which may be particularly important for the anticipated wide spans of binary Client IDs.

- raxSeekChildren: Seek a key in order to get its immediate child keys. A key of NULL seeks the root which is useful for handling a rax tree of depth 1.

- raxNextChild: Return the next immediate child key of the key sought above.

And for easier visualization of binary data, e.g. Client IDs:

- raxShowHex

## The unified topic/client (TC) tree

The external TC tree conforms to MQTT.

The internal TC tree is composed of:

- The root
- The hierarchy token:
    - ``@`` for the normal hierarchy
    - ``$`` for topics starting with '\$'
- For shared subscriptions, the topic is placed in the hierarchy above and the rest treated as described below.
- the topic tokens (the 0-length token is represented by ``0x1f`` which is invalid in MQTT)
- ``/`` as the separator between the above tokens

Then for normal subscription clients:
- ``0xef`` as the client marker (invalid UTF8)
- 8 bytes of Client ID in big endian (network) order

And for shared subscription clients:
- ``0xff`` as the shared marker (invalid UTF8)
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

``@/foo/#<0xef><0x0000000000000001>``

### The Rax tree implementation

Rax is a binary character based adaptive radix prefix tree. This means that common prefixes are combined and node sizes vary depending on prefix compression, node compression and the number of children. A key is a sequence of characters that can be "inserted" and/or "found". Optionally a key can have associated data. The keys are maintained in lexicographic order within the tree's hierarchy.

There is much more information in the Rax README and rax.h.

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
A full explanation of the notation above is in the Rax README and rax.h; a tricky part is that the first character of a key is stored in the node pointing to the key, not in the key itself.

Each key except for leaf Client IDs has an integer value associated with it which is the count of Client IDs in its subtree, e.g. the ``0x8`` associated with key ``@``. This is currently useful in randomly picking a Client ID when matching a shared subscription and in pruning the tree as subscriptions are deleted. The Rax tree itself maintains total counts of all keys and nodes.

### The Client tree

This tree contains a subscriptions inversion for each client, topic aliases for clients, and will contain other client-based information.

Topic aliases are in 2 synchronized subtrees: ``abt`` and ``tba`` for each client, providing alias-by-topic and topic-by-alias respectively. The topic alias leaf values are used to store the alias and the topic pointer – this usage simplifies the overwriting of aliases as allowed by MQTT.

Adding topic alias ``8`` for Client ID ``1`` topic ``foo/bar`` and running ``raxShowHex()`` yields the following depiction of our 7 clients, their 9 subscriptions and the alias in the client tree (the value ``0x102da8088`` is the pointer to ``foo/bar``):

```
"0x00000000000000" -> [0x01020304050607]
        `-(.) [ast]
               `-(a) "bt" -> "foo/bar" -> []=0x8
               `-(s) "ubs" -> [$f]
                               `-($) "SYS/foo/#" -> []
                               `-(f) "oo/" -> [#b]
                                               `-(#) []
                                               `-(b) "ar" -> []
               `-(t) "ba" -> [0x08] -> []=0x102da8088
        `-(.) "subs" -> "foo/bar" -> []
        `-(.) "subs" -> "foo/bar/" -> []
        `-(.) "subs" -> "$share/baz/foo/bar" -> []
        `-(.) "subs" -> "$share/baz/foo/bar" -> []
        `-(.) "subs" -> "+/bar" -> []
        `-(.) "subs" -> "foo/#" -> []
```