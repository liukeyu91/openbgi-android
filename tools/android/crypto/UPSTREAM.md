# Camellia dependency provenance

`PatchCamellia.java` adapts Bouncy Castle's MIT-licensed CamelliaEngine from fixed tag `r1rv86`. Original URLs and SHA-256 digests are in `upstream.json`; the required license is `BouncyCastle-MIT.txt` and is also retained in the Java source.

Changes: package/class renamed; provider interfaces, imports and policy registration removed; constructor accepts exactly a 16-byte encryption key; provider-specific buffer exceptions replaced with IllegalArgumentException and offset bounds strengthened. The algorithm, S-boxes, encryption key schedule and block processing remain unchanged. No JCA provider is installed or bundled. Legacy 192/256-bit private code is retained but unreachable through the constructor.

CPK code only calls Camellia encryption for nonce CMAC and CTR keystream. It does not claim EAX authentication: this resource layout has no verified authentication tag.
