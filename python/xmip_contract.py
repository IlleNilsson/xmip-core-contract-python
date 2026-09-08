"""The Python content contract - a technology of xmip-core-contract, in Python.

ADR-0042 decision 3: a contract may be authored in any declared language over
the C ABI. An interpreter cannot export the C entrypoint itself, so the shim in
``shim/`` does: it embeds CPython in-process and forwards the contract table to
this module (owner, 2026-09-07). Nothing here touches Xmip's Rust or its C
header; the shim is the only thing that does.

What it claims: well-formedness is bytes (ADR-0042 decision 1). A Python
contract with a real standard replaces ``validate`` and nothing else.
"""

from __future__ import annotations


def validate(descriptor: str, data: bytes) -> str:
    """Judge a whole stream against the bound descriptor.

    Return the empty string when the stream holds, else the message the
    diagnostic carries. The identity contract holds everything.
    """
    return ""


def implies(descriptor: str, key: str) -> str | None:
    """What the bound contract already determines about ``key``, or None."""
    if key == "descriptor" and descriptor:
        return descriptor
    return None
