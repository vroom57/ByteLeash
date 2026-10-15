# core

Shared, platform-independent logic used by every ByteLeash edition.

Code here must not call OS-specific APIs directly. Anything that differs between
platforms belongs in the matching directory under `platforms/`.
