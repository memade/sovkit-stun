// SPDX-License-Identifier: MIT
#include <libnet_stun_server.h>
#include <iostream>

int main() {
  // No listener, DNS, files, identity or network discovery in this smoke test.
  const auto source = libnet::Endpoint::Parse("192.0.2.1", 54321);
  std::array<std::uint8_t, 20> request{0, 1, 0, 0, 0x21, 0x12, 0xa4, 0x42};
  std::array<std::uint8_t, 40> reply{};
  std::size_t size = 0;
  libnet::StunServer server;
  if (!source || server.Snapshot().running ||
      !libnet::MakeStunBindingReply(request, *source, reply, size) || size != 32)
    return 1;
  std::cout << "installed SDK: codec and lifecycle smoke passed\n";
  return 0;
}
