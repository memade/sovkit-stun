# Changes

## 0.1.0-preview.1 — development preview

- Extract IPv4 UDP STUN Binding server and bounded, default-off cross-port diagnostics from SovKit libnet.
- Standalone CLI with strict configuration, loopback defaults, aggregate JSON logging and signal shutdown.
- Installable static C++20 SDK: SovkitStun::stun, CMake package, independent host example and relocation test.
- Allowlisted source snapshot and macOS preview packaging, per-file SHA-256 and original license notices.
- Parser, UDP, lifecycle/configuration, source-provenance and packaging checks.

Release availability is recorded on GitHub Releases, not implied by this changelog.
macOS arm64 artifacts are development previews; Linux ARM64 has native Release test coverage, not a binary release.
Not a full RFC 8489/5780 implementation. No TURN, relay or full SovKit SDK is included.
