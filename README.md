# mr_rax: functions to handle the MQTT topic/client tree using Rax underneath

The mr_rax public functions so far are:

- mr_insert_subscription: Insert an MQTT subscription topic (with optional wildcards) and a client ID

- mr_get_clients: For a published topic return the dedup'd set of clients IDs from matching subscriptions.

Note: MQTT shared subscriptions are supported.

More functions will be added to, e.g., delete subscriptions.

This project is set up to use as one of the CMake subprojects in a comprehensive MQTT project(s).

Some additions and minimal modifications have also been made to Rax itself to support the following functions needed by the above:

- raxSeekChildren: Seek a key in order to get its immediate child keys.

- raxNextChild: Return the next immediate child key of the key sought above.

And for easier visualization of binary data, e.g. client IDs:

- raxShowHex
