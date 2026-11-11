# Device identity decision for pairing

The project owner selected a software-stored device identity for the first usable pairing implementation. Do not program eFuses or require external secure-element hardware. The tradeoff is explicit: an attacker who can copy unprotected device flash may clone the device identity. A Home Assistant backup may likewise contain the HA private key.

This decision permits development of a software-key pairing implementation; it does not authorize configuration deployment, OTA, eFuse programming, automatic trust migration, or untested trust grants. The existing laboratory identity and its signed touch receipt are not themselves durable pairing.

Remaining engineering gates include a reviewed session-bound protocol with authenticated cipher-suite selection, durable and recoverable trust on both peers, verified device pinning and HA identity possession on fresh encrypted reconnects, explicit revocation/re-pairing without erasing configuration, failure/replay and power-interruption tests, and real-device acceptance. Keep the existing deployment kill switch off. Preserve the possibility of a future stronger key backend without implying that an existing software key becomes non-exportable.
