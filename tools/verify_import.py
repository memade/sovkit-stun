#!/usr/bin/env python3
"""Read-only checks for the frozen, allowlisted initial source export."""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(text, mode):
    if mode == "exact":
        return text
    if mode == "endpoint-header":
        body = text[text.index("enum class AddressFamily"):text.index("using ResolveHandler")]
        return (
            "// Endpoint-only extraction from SovKit libnet; see docs/PROVENANCE.md.\n"
            "#ifndef LIBNET_UV_H_\n#define LIBNET_UV_H_\n#include <uv.h>\n"
            "#include <cstddef>\n#include <cstdint>\n#include <optional>\n"
            "#include <string>\n#include <string_view>\n#include <vector>\n"
            "namespace libnet {\n" + body + "} // namespace libnet\n#endif\n"
        )
    if mode == "endpoint-source":
        body = text[text.index("std::optional<Endpoint> Endpoint::Parse"):
                    text.index("struct Resolver::State")]
        return (
            "// Endpoint-only extraction from SovKit libnet; see docs/PROVENANCE.md.\n"
            '#include "libnet_uv.h"\n#include <algorithm>\n#include <cstring>\n'
            "namespace libnet {\n" + body + "} // namespace libnet\n"
        )
    raise ValueError("unknown extraction mode")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path,
                        help="optional local SovKit root; not needed to build or test")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / "docs/export-manifest.json").read_text())
    if manifest["format_version"] != 1:
        raise ValueError("unknown manifest format")
    for entry in manifest["files"]:
        target = root / entry["destination"]
        if sha(target) != entry["destination_sha256"]:
            raise ValueError("export changed: " + entry["destination"])
        if args.source_root:
            source = args.source_root / entry["source"]
            if sha(source) != entry["source_sha256"]:
                raise ValueError("source changed; review before updating manifest: " + entry["source"])
            expected = extract(source.read_text(), entry["mode"])
            if target.read_text() != expected:
                raise ValueError("extraction mismatch: " + entry["destination"])
    print(f"Verified {len(manifest['files'])} allowlisted imports"
          + (" against local source" if args.source_root else " against manifest"))


if __name__ == "__main__":
    main()
