# xmip-core-contract-python

A content contract authored in Python: a C shim embeds CPython in-process and forwards the contract table. A technology of
[xmip-core-contract](https://github.com/IlleNilsson/xmip-core-contract); ADR-0042
decision 3 admits a contract in any declared language, and ADR-0012 makes
`include/xmip_module.h` in xmip-core-abi the boundary it conforms to.

What it claims today: well-formedness is bytes, every Stream is read to its end
and held (ADR-0042 decision 1). A descriptor a Location binds is kept and
answered by `implies`. A contract with a real standard replaces the one
judgement function and nothing else; the entrypoint, the table and the lifecycle
are done.

## Verification

`verify.ps1` is the gate xgit runs: it builds the loadable library and drives
it through a probe written against the header's own types. It needs the
toolchain `prerequisite.toml` declares for this language.
