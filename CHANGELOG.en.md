# Android changelog

[简体中文](CHANGELOG.md) | English


| Version | Changes |
| --- | --- |
| 0.10.22-preview | Fix clipped Latin letters in mixed SJIS/UTF-8 text; keep rendering, measurement and wrapping widths consistent. |
| 0.10.21-preview | Fix equal-size compressed translation resources being treated as uncompressed; repair affected images in existing imports. |
| 0.10.20-preview | Bundle FFmpeg for standard movie playback, with audio/video synchronization, tap to skip, and background pause. |
| 0.10.19-preview | Extract supported translation resources during import or for existing games, and apply recognized encoding and engine settings. |
| 0.10.18-preview | Improve text at non-integer scales, fix silent output caused by audio sample-rate mismatches, and report output failures. |
| 0.10.17-preview | Improve signing-key protection, add CI, reproducible builds and memory checks, and fix resource leaks. |
| 0.10.16-preview | Fix vertically compressed Noto CJK text in SJIS and UTF-8 modes on Android. |
| 0.10.15-preview | Support Making Lovers Full HD display modes, sound-effect slots, backlog and graphics parameters; fix out-of-bounds access when loading large images. |
| 0.10.14-preview | Add graphics compatibility instructions used during Making Lovers Full HD startup. |
| 0.10.13-preview | Detect the Making Lovers Full HD engine profile and enable the required startup instructions. |
| 0.10.12-preview | Add per-game save ZIP export/import, including global progress, validation and recovery; add a fast-forward toggle and separate shortcut mappings. |
| 0.10.11-preview | Add system-script object queries, directory and file-time instructions, fix archive enumeration results, and distinguish external-program platform limitations. |
| 0.10.10-preview | Optimize CBG v2 decoding, pixel copies and ARGB layer compositing. |
| 0.10.9-preview | Fix translation-module length handling; continue compatibility scans for modules with inconsistent length declarations and report affected files. |
| 0.10.8-preview | Add fixed-point, vector, array and panel-texture instructions. |
| 0.10.7-preview | Add post-import compatibility checks showing missing instructions and their locations, with rescanning for existing games. |
| 0.10.6-preview | Add vector normalization and fix scenes that failed because the instruction was missing. |
| 0.10.5-preview | Detect text encoding on import and launch, prompt when uncertain, and stop defaulting every new game to SJIS. |
| 0.10.4-preview | Unify wording across the library, import, encoding, settings and log interfaces. |
| 0.10.3-preview | Optimize CBG decoding, add ARM64 NEON compositing, and speed up case-insensitive filename lookup. |
| 0.10.2-preview | Cache decoded CBG images to reduce repeated decoding when character sprites change. |
| 0.10.1-preview | Reject game entries without a valid startup file; optimize alpha compositing, smooth sampling and canvas copies. |
| 0.10.0-preview | Redesign the library and settings, add search, sorting, renaming and custom covers, and revise portrait/landscape layouts and the shortcut overlay. |
| 0.9.1-preview | Improve initial-text encoding conversion, fix duplicate Enter/Tab confirmation, and limit crash-log size. |
| 0.9.0-preview | Add manual encoding comparison and conversion for initial input text, respecting the game's encoding and byte limit. |
| 0.8.0 | Add script-text sampling and SJIS/GBK/UTF-8 previews; fix text conversion in some titles and message boxes. |
| 0.7.0 | Switch rendering to EGL/GLES2, with dirty-region uploads, GPU scaling and vertical synchronization. |
| 0.6.0 | Expand per-game settings for scaling, dragging, long-press right click, screen shortcuts, keyboard and controller mappings. |
| 0.5.1 | Add audio-focus handling and pause/resume behavior for focus loss and headphone disconnection. |
| 0.5.0 | Switch to Activity + SurfaceView + JNI; add Android IME support, text input, system dialogs and background pause. |
| 0.4.1 | Add directory/archive queries and font enumeration; improve KanojoStep startup compatibility. |
| 0.4 | Add full-directory import, multiple games, separate settings and saves, automatic engine detection and three text-encoding options. |
| 0.3 | Record native crashes, Java exceptions and engine logs, with log export. |
| 0.2 | Improve GBK Chinese, mixed Chinese/English text and font rendering; fix crashes in some image operations. |
| 0.1-smoke | Add the Android ARM64 backend with basic graphics, touch input and audio output. |
