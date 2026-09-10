// Endpoint-only extraction from SovKit libnet; see docs/PROVENANCE.md.
#ifndef LIBNET_UV_H_
#define LIBNET_UV_H_
#include <uv.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace libnet {
enum class AddressFamily : std::uint8_t { ipv4 = 4, ipv6 = 6 };

class Endpoint final {
public:
  Endpoint() = default;

  static std::optional<Endpoint> Parse(std::string_view address,
                                       std::uint16_t port);
  static std::optional<Endpoint> FromSockaddr(const sockaddr *address,
                                              std::size_t length);

  bool valid() const { return length_ != 0; }
  AddressFamily family() const;
  std::string address() const;
  std::uint16_t port() const;
  std::string ToString() const;
  /// True only for an address that is globally routable by address class.
  /// This is a diagnostic/routing hint, never an authorization decision.
  bool IsGlobal() const;
  const sockaddr *sockaddr_ptr() const;
  int sockaddr_length() const;

private:
  sockaddr_storage storage_{};
  int length_ = 0;
};

} // namespace libnet
#endif
