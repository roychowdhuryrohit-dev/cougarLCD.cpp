# Contributing

Issues and pull requests are welcome for the confirmed COUGAR CFV235 LCD
interface. Please keep contributions asset-free and narrowly scoped.

Before opening a pull request:

1. Read `AGENTS.md` and `docs/PROTOCOL.md`.
2. Build with warnings enabled on the affected platform.
3. Run the Linux render smoke test or the Windows read-only probe build.
4. Redact serial numbers, usernames, paths, and machine identifiers from logs.
5. Describe the exact case/LCD model, firmware, and validation performed.

Unknown USB writes need protocol evidence and conservative bounds checks. Do
not submit vendor binaries, extracted application files, fonts, backgrounds,
firmware, or other copyrighted media.

