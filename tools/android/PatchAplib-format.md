# Raw aPLib blocks in translation containers

`PatchAplib.java` is a new GPL-2.0-only Java implementation of the raw token grammar. It contains no Python package, JNI library, Windows DLL loader, or Android dependency. It was written for the independent, at-most-64-KiB blocks found in the inspected EVB containers. It does not parse EVB or the separate AP32 wrapper.

## Format sources and provenance

- Original format/library author: Jørgen Ibsen, [official aPLib product page](https://ibsensoftware.com/products_aPLib.html), version 1.1.1, [official download page](https://www.ibsensoftware.com/download.html). The official package has its own freeware terms; no package source or binary is copied or linked into this implementation.
- Public algorithm reference: [aplibsharp in svendahl/cap](https://github.com/svendahl/cap/tree/257f089c833ab716e4dfdce882418bbee872389b/aplibsharp), fixed commit `257f089c833ab716e4dfdce882418bbee872389b`. The [repository's license statement](https://github.com/svendahl/cap/blob/257f089c833ab716e4dfdce882418bbee872389b/readme.md#license) explicitly releases aplibsharp under MIT. Its encoder token rules and numeric thresholds were consulted to establish the grammar; its source was not transliterated. No code from the repository's separately licensed architecture depackers is included.
- The optional `snemes/aplib` Python package used earlier for offline investigation is GPLv3. Its code is not used in this Java implementation or synthetic tests, and it must not become an APK dependency.

Reference SHA256 values:

| Fixed reference file | SHA256 |
|---|---|
| `readme.md` | `4917925dc67a236b0688ca9c9031166a9a81933dea045e1db8d699ecfe29058d` |
| `aplibsharp/tobinary.cs` | `846536dae07dbd5e128965f1438a72a72817ec466426c368960b67c877986e4c` |
| `aplibsharp/decode.cs` | `6b87868c86e7884da3a90e514d475325cd972a58340587182b124af5f52f941e` |
| `aplibsharp/constant.cs` | `c6f1e6fa5dbc3824c47a855169f144995bfc28d3b47cc208c382c4295288b0be` |

## Grammar implemented

The first byte is an unconditional literal. Tag bits are read most-significant-first from interleaved tag bytes; raw operand bytes share the same input cursor. Interlaced gamma values start with an implicit leading one, followed by pairs of value-bit and continuation-bit.

| Tag prefix | Meaning |
|---|---|
| `0` | One raw literal byte; clears the previous-match flag. |
| `10` | Gamma high-distance code. When the previous token was not a match and code is 2, reuse the previous distance and read gamma length. Otherwise subtract 3, or 2 after a match, combine with the next raw low-distance byte, then read gamma length. Add 2 to length for distances below 128 or at least 32000; add 1 for distances 1280 through 31999. Saves the new distance and sets the match flag. |
| `110` | Raw byte: high seven bits are distance, low bit selects length 2 or 3. Distance zero terminates; otherwise saves the distance and sets the match flag. |
| `111` | Four distance bits: zero emits a zero byte, otherwise copies one byte at that distance. Clears the match flag without changing the saved distance. |

Back-references copy forward and may overlap, as required for run expansion. A repeat before any valid saved match is rejected.

## API and limits

`public static byte[] PatchAplib.decompress(byte[] packed, int maxOutput) throws IOException`

- Accepts a single raw block, 3 bytes through 1 MiB compressed; `maxOutput` must be 1 through 65536.
- Returns the actual decoded byte array, which can be shorter than the cap. The EVB caller must check per-resource totals against its declared size.
- Requires a termination token and consumption of all whole input bytes; unused bits in the last tag are permitted.
- Rejects input truncation, invalid or unavailable distances, unset repeated distance, excess output, excess gamma value, and trailing bytes with `IOException`.
- Bounds are checked before copying; gamma arithmetic and distance composition remain within integers. No allocation depends on an unbounded stream value. Instance state is private to one call.

Run synthetic tests with `sh tools/android/tests/run-patch-aplib-tests.sh` (JDK required). The committed tests create their own literal, short/long/repeated-match and four-bit-offset streams, exercise distance/length thresholds and bounds, and process deterministic random malformed input. No commercial game data is committed as a fixture.

Private validation additionally compared 26 compressed blocks from eight local MakingHD/KanojoStep script and image resources against previously verified offline output, byte-for-byte. This validates these raw blocks, not full game execution or every EVB variant. The private test inputs and output hashes remain outside public source.
