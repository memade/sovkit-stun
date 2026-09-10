// Endpoint-only extraction from SovKit libnet; see docs/PROVENANCE.md.
#include "libnet_uv.h"
#include <algorithm>
#include <cstring>
namespace libnet {
std::optional<Endpoint> Endpoint::Parse(std::string_view address,
                                        std::uint16_t port) {
  if (address.empty())
    return std::nullopt;
  Endpoint endpoint;
  const std::string text(address);
  sockaddr_in address4{};
  if (uv_ip4_addr(text.c_str(), port, &address4) == 0) {
    std::memcpy(&endpoint.storage_, &address4, sizeof(address4));
    endpoint.length_ = sizeof(address4);
    return endpoint;
  }
  sockaddr_in6 address6{};
  if (uv_ip6_addr(text.c_str(), port, &address6) == 0) {
    std::memcpy(&endpoint.storage_, &address6, sizeof(address6));
    endpoint.length_ = sizeof(address6);
    return endpoint;
  }
  return std::nullopt;
}

std::optional<Endpoint> Endpoint::FromSockaddr(const sockaddr *address,
                                               std::size_t length) {
  if (address == nullptr)
    return std::nullopt;
  const std::size_t required =
      address->sa_family == AF_INET    ? sizeof(sockaddr_in)
      : address->sa_family == AF_INET6 ? sizeof(sockaddr_in6)
                                       : 0;
  if (required == 0 || length < required)
    return std::nullopt;
  Endpoint endpoint;
  std::memcpy(&endpoint.storage_, address, required);
  endpoint.length_ = static_cast<int>(required);
  return endpoint;
}

AddressFamily Endpoint::family() const {
  return storage_.ss_family == AF_INET6 ? AddressFamily::ipv6
                                        : AddressFamily::ipv4;
}

std::string Endpoint::address() const {
  if (!valid())
    return {};
  char text[INET6_ADDRSTRLEN]{};
  const int status =
      storage_.ss_family == AF_INET
          ? uv_ip4_name(reinterpret_cast<const sockaddr_in *>(&storage_), text,
                        sizeof(text))
          : uv_ip6_name(reinterpret_cast<const sockaddr_in6 *>(&storage_), text,
                        sizeof(text));
  return status == 0 ? std::string(text) : std::string{};
}

std::uint16_t Endpoint::port() const {
  if (!valid())
    return 0;
  return storage_.ss_family == AF_INET
             ? ntohs(reinterpret_cast<const sockaddr_in *>(&storage_)->sin_port)
             : ntohs(reinterpret_cast<const sockaddr_in6 *>(&storage_)
                         ->sin6_port);
}

std::string Endpoint::ToString() const {
  const std::string host = address();
  if (host.empty())
    return {};
  return family() == AddressFamily::ipv6
             ? "[" + host + "]:" + std::to_string(port())
             : host + ":" + std::to_string(port());
}

bool Endpoint::IsGlobal() const {
  if (!valid())
    return false;
  if (storage_.ss_family == AF_INET) {
    const auto *value = reinterpret_cast<const sockaddr_in *>(&storage_);
    const std::uint32_t address = ntohl(value->sin_addr.s_addr);
    const std::uint8_t first = static_cast<std::uint8_t>(address >> 24U);
    const std::uint8_t second =
        static_cast<std::uint8_t>((address >> 16U) & 0xffU);
    const std::uint8_t third =
        static_cast<std::uint8_t>((address >> 8U) & 0xffU);
    return first != 0 && first != 10 && first != 127 &&
           !(first == 100 && second >= 64 && second <= 127) &&
           !(first == 169 && second == 254) &&
           !(first == 172 && second >= 16 && second <= 31) &&
           !(first == 192 && second == 168) &&
           !(first == 192 && second == 0 && third == 2) &&
           !(first == 198 && (second == 18 || second == 19)) &&
           !(first == 198 && second == 51 && third == 100) &&
           !(first == 203 && second == 0 && third == 113) && first < 224;
  }
  const auto *value = reinterpret_cast<const sockaddr_in6 *>(&storage_);
  const auto &bytes = value->sin6_addr.s6_addr;
  const bool unspecified = std::all_of(
      bytes, bytes + 16, [](std::uint8_t byte) { return byte == 0; });
  const bool loopback =
      std::all_of(bytes, bytes + 15,
                  [](std::uint8_t byte) { return byte == 0; }) &&
      bytes[15] == 1;
  const bool unique_local = (bytes[0] & 0xfeU) == 0xfcU;
  const bool link_local = bytes[0] == 0xfeU && (bytes[1] & 0xc0U) == 0x80U;
  const bool multicast = bytes[0] == 0xffU;
  const bool documentation = bytes[0] == 0x20U && bytes[1] == 0x01U &&
                             bytes[2] == 0x0dU && bytes[3] == 0xb8U;
  return !unspecified && !loopback && !unique_local && !link_local &&
         !multicast && !documentation && (bytes[0] & 0xe0U) == 0x20U;
}

const sockaddr *Endpoint::sockaddr_ptr() const {
  return valid() ? reinterpret_cast<const sockaddr *>(&storage_) : nullptr;
}

int Endpoint::sockaddr_length() const { return length_; }

} // namespace libnet
