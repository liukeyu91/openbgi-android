# Android crash diagnostics

Export the log ZIP from the app's About page after a failure. The v2 reporter
records the fault's native TID, signal/code/address, ARM64 registers, up to 32
frame-pointer caller candidates, the last 32 VM instructions, and bounded
memory maps. The embedded session identifies the APK, game entry, selected
encoding/profile and native library SHA256. No save or game-resource files
are collected. Engine messages can contain game/device/file names.

The VM breadcrumb is the last observed script instruction, not proof that
the VM thread crashed. Compare its owner TID with the fault TID. Loading a
module immediately before a failure also does not establish causation.

The signal path uses preopened files, bounded formatting, lock-free atomic
snapshots, and async-signal-safe I/O. It does not allocate, lock, invoke Java,
or run a general unwinder. Frame records are read with `pread`, so a corrupt
frame pointer cannot itself cause a second memory fault. `/proc` restrictions,
missing frame pointers, or damaged stacks can leave the caller list incomplete.
The UI and VM threads have alternate signal stacks; other threads retain their
normal signal stacks. No complete native backtrace or system tombstone is promised.

ART sigchain can repeatedly intercept a re-raised fatal signal. After recording,
OpenBGI continues to use `_exit(128 + signal)`; Android can consequently report
`REASON_EXIT_SELF`, status 139 for SIGSEGV or 134 for SIGABRT. This is a failure,
not a successful session. System force stops and low-memory exits are separate
history entries. The ZIP includes process name/PID, timestamps, reason and trace
availability, so unrelated older exits can be distinguished.

The last nonempty native report survives subsequent normal launches as
`native-crash-previous.txt`. Engine/session records rotate independently; use
the session metadata embedded in each crash instead of assuming all files
describe the latest session. Export sizes are bounded and truncation/unavailable
system traces are listed in `export-notes.txt`.

CI Android builds keep `libopenbgi.so.debug` outside the APK and enable frame
pointers. Preserve the matching debug artifact. Given an extracted report and
the exact `lib/arm64-v8a/libopenbgi.so` from that APK:

```sh
python3 tools/android/symbolize-crash.py native-crash.txt libopenbgi.so libopenbgi.so.debug \
  --addr2line /path/to/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-addr2line
```

The tool rejects a native SHA256 mismatch or mismatched ELF/debug build IDs,
subtracts the recorded ELF load bias (also when the ELF was mapped from an APK),
and resolves candidates inside the engine. External-library faults still require
the corresponding vendor/system symbols. Raw stack memory and script text are
not dumped.

`tools/ci/test_crash.py` deliberately triggers SIGSEGV, SIGABRT, a worker-thread
fault and a stack overflow in disposable Linux subprocesses. It checks report
completion, address-to-source resolution, concurrent VM snapshots, and retention
across normal launches. These tests do not establish Android/device coverage.
