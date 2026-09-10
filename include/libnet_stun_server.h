#ifndef LIBNET_STUN_SERVER_H_
#define LIBNET_STUN_SERVER_H_

#include "libnet_uv.h"
#include <array>
#include <memory>
#include <span>

namespace libnet {

// IPv4 Binding-only service. No TURN or payload forwarding.
struct StunServerConfig {
  std::string bind_address = "127.0.0.1";
  std::array<std::uint16_t, 2> ports{3478, 3479};
  bool diagnostics = false;           // IP metadata must be explicitly enabled.
  bool filtering_diagnostics = false; // consent-gated, source-bound cross-port replies
  std::uint32_t duration_seconds = 0; // 0 = until explicit shutdown.
};

struct StunObservation {
  std::uint64_t sequence = 0, unix_ms = 0;
  Endpoint source;
  std::uint16_t server_port = 0;
  std::uint16_t response_port = 0;
  std::uint8_t filtering_operation = 0;
  std::array<std::uint8_t, 12> transaction{}; // hash before host export
  std::uint16_t request_bytes = 0, response_bytes = 0;
  int send_status = 0; // libuv/kernel acceptance, NOT remote receipt
};

struct StunServerStats {
  bool running = false, diagnostics = false;
  bool filtering_diagnostics = false;
  std::string stop_reason = "not-started";
  std::array<std::uint16_t, 2> ports{};
  std::uint64_t started_unix_ms = 0;
  std::uint64_t datagrams_received = 0, receive_errors = 0;
  std::uint64_t invalid_requests = 0, rate_limited = 0;
  std::uint64_t bindings = 0, sends_accepted = 0, send_errors = 0;
  std::uint64_t observations_dropped = 0;
  std::vector<StunObservation> observations;
};

// Pure codec. At most 40 response bytes. Does not reflect request payloads.
bool MakeStunBindingReply(std::span<const std::uint8_t> request,
                          const Endpoint &source,
                          std::array<std::uint8_t, 40> &response,
                          std::size_t &response_size);

// Owns one libuv loop/thread. Start/Stop/Snapshot may be called by host
// threads. No callbacks into the host, disk writes, unbounded send queues or
// raw logs.
class StunServer final {
public:
  StunServer();
  ~StunServer();
  StunServer(const StunServer &) = delete;
  StunServer &operator=(const StunServer &) = delete;
  int Start(const StunServerConfig &config);
  void Stop();
  StunServerStats Snapshot() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace libnet
#endif
