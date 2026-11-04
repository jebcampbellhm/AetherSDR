# nRSP-ST capture fixtures

`wire-examples.json` contains original frames from operator-authorized local
captures on nRSP-ST firmware 1.0.15. It was exported from the research workspace
on 2026-10-22. Every example carries its capture/stream reference, stream byte
offset, packet numbers, original ciphertext, decoded payload, and SHA-256.
Large data examples additionally record a hash of decompressed sample bytes.

The fixture is intended for offline tests. It contains public bootstrap/key
information and expired session material, with no account password or private
key. Captured challenge responses cannot answer a newly generated nonce.

See [the protocol specification](../../../docs/nrsp-st-protocol.md) for
provenance, qualifications, and the distinction between observed bytes and
unverified command semantics. A decoder passing these examples does not prove
that an entire receiver implementation interoperates with the radio.
