// SPDX-License-Identifier: MIT
#include "libnet_stun_server.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
void Check(bool value) {
  if (!value) throw std::runtime_error("codec check failed");
}
}
int main() {
  try {
    const auto source = libnet::Endpoint::Parse("192.0.2.1", 54321);
    Check(source.has_value());
    Check(source->address() == "192.0.2.1" && source->port() == 54321);
    Check(!libnet::Endpoint::Parse("not-an-address", 3478));
    Check(!libnet::Endpoint::FromSockaddr(nullptr, 0));
    Check(!libnet::Endpoint::FromSockaddr(source->sockaddr_ptr(), 1));
    std::vector<std::uint8_t> request{
      0, 1, 0, 0, 0x21, 0x12, 0xa4, 0x42, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    std::array<std::uint8_t, 40> reply{};
    std::size_t size = 999;
    auto accept = [&](const auto &data, const libnet::Endpoint &endpoint) {
      return libnet::MakeStunBindingReply(data, endpoint, reply, size);
    };
    Check(accept(request, *source) && size == 32);
    Check(reply[0] == 1 && reply[1] == 1 && reply[3] == 12);
    Check(reply[20] == 0 && reply[21] == 0x20 && reply[25] == 1);
    Check(((reply[26] << 8 | reply[27]) ^ 0x2112) == 54321);
    Check(reply[28] == (192 ^ 0x21) && reply[31] == (1 ^ 0x42));
    for (unsigned i = 8; i < 20; ++i) Check(reply[i] == request[i]);
    for (const auto &host : {"::1", "0.0.0.0", "255.255.255.255", "224.0.0.1"}) {
      const auto endpoint = libnet::Endpoint::Parse(host, 1234);
      Check(endpoint.has_value() && !accept(request, *endpoint) && size == 0);
    }
    Check(!accept(request, libnet::Endpoint{}) && size == 0);
    Check(!accept(request, *libnet::Endpoint::Parse("127.0.0.1", 0)));
    for (std::size_t n = 0; n < 20; ++n)
      Check(!accept(std::span(request).first(n), *source) && size == 0);
    auto required = request;
    required[3] = 4;
    required.insert(required.end(), {0, 6, 0, 0}); // USERNAME unsupported
    Check(!accept(required, *source));
    required[20] = 0x80;
    required[21] = 0x22; // optional SOFTWARE, empty
    Check(accept(required, *source) && size == 32);
    required[23] = 1; // declared data missing
    Check(!accept(required, *source));
    auto oversized = request;
    oversized.resize(1028);
    Check(!accept(oversized, *source) && size == 0);

    // Deterministic parser stress, including structurally valid STUN envelopes.
    std::mt19937 random(0x5354554e);
    for (unsigned i = 0; i < 20000; ++i) {
      std::vector<std::uint8_t> bytes(random() % 1100);
      for (auto &byte : bytes) byte = static_cast<std::uint8_t>(random());
      if (bytes.size() >= 20 && i % 2 == 0) {
        std::copy(request.begin(), request.end(), bytes.begin());
        bytes[2] = (bytes.size() - 20) >> 8;
        bytes[3] = (bytes.size() - 20) & 255;
      }
      const bool ok = accept(bytes, *source);
      Check(ok ? (size == 32 || size == 40) : size == 0);
    }
    std::cout << "codec: bounds, mapping, source checks, 20000 parser samples passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
