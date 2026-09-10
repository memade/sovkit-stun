#include "libnet_stun_server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace libnet {
namespace {
constexpr std::uint32_t kCookie = 0x2112a442;
std::uint16_t Read16(const std::uint8_t *p) {
  return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
std::uint32_t Read32(const std::uint8_t *p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
         (std::uint32_t(p[2]) << 8) | p[3];
}
void Put16(std::uint8_t *p, std::uint16_t v) {
  p[0] = v >> 8;
  p[1] = v & 255;
}
void Put32(std::uint8_t *p, std::uint32_t v) {
  for (int i = 3; i >= 0; --i) {
    p[i] = v & 255;
    v >>= 8;
  }
}
// RFC 8489 FINGERPRINT is a CRC, not cryptographic authentication.
std::uint32_t Fingerprint(std::span<const std::uint8_t> bytes) {
  std::uint32_t crc = 0xffffffff;
  for (auto byte : bytes) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
  }
  return (crc ^ 0xffffffff) ^ 0x5354554e;
}
std::uint64_t UnixMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}
struct Bucket {
  double tokens = 0;
  std::uint64_t last = 0;
  bool Take(std::uint64_t now, double rate, double burst) {
    tokens = std::min(burst, tokens + double(now - last) * rate / 1000);
    last = now;
    if (tokens < 1)
      return false;
    tokens -= 1;
    return true;
  }
};
} // namespace

bool MakeStunBindingReply(std::span<const std::uint8_t> request,
                          const Endpoint &source,
                          std::array<std::uint8_t, 40> &response,
                          std::size_t &response_size) {
  response_size = 0;
  if (!source.valid() || source.family() != AddressFamily::ipv4 ||
      !source.port() || request.size() < 20 || request.size() > 1024 ||
      Read16(request.data()) != 1 || Read32(request.data() + 4) != kCookie ||
      Read16(request.data() + 2) % 4 ||
      std::size_t(Read16(request.data() + 2)) + 20 != request.size())
    return false;
  const auto *address =
      reinterpret_cast<const sockaddr_in *>(source.sockaddr_ptr());
  const auto ip = ntohl(address->sin_addr.s_addr);
  if (ip == 0 || ip == 0xffffffff || (ip & 0xf0000000) == 0xe0000000)
    return false;
  bool fingerprint = false;
  for (std::size_t offset = 20; offset < request.size();) {
    if (request.size() - offset < 4)
      return false;
    const auto tag = Read16(request.data() + offset);
    const auto size = Read16(request.data() + offset + 2);
    const std::size_t end =
        offset + 4 + ((std::size_t(size) + 3) & ~std::size_t(3));
    if (end > request.size() || tag < 0x8000 || tag == 0xc0a1)
      return false;
    // Required attributes (credentials, ICE, TURN, destination changes) are
    // deliberately unsupported and silently dropped, never reflected.
    if (tag == 0x8028) {
      if (size != 4 || end != request.size() ||
          Read32(request.data() + offset + 4) !=
              Fingerprint(request.first(offset)))
        return false;
      fingerprint = true;
    }
    offset = end;
  }
  response.fill(0);
  Put16(response.data(), 0x0101);
  Put16(response.data() + 2, fingerprint ? 20 : 12);
  Put32(response.data() + 4, kCookie);
  std::copy_n(request.data() + 8, 12, response.data() + 8);
  Put16(response.data() + 20, 0x0020);
  Put16(response.data() + 22, 8);
  response[25] = 1;
  Put16(response.data() + 26, source.port() ^ (kCookie >> 16));
  Put32(response.data() + 28, ip ^ kCookie);
  response_size = fingerprint ? 40 : 32;
  if (fingerprint) {
    Put16(response.data() + 32, 0x8028);
    Put16(response.data() + 34, 4);
    Put32(response.data() + 36, Fingerprint(std::span(response).first(32)));
  }
  return true;
}

struct StunServer::Impl {
  mutable std::mutex lifecycle_mutex, stats_mutex;
  uv_loop_t loop{};
  uv_async_t stop{};
  uv_timer_t timer{};
  std::array<uv_udp_t, 2> sockets{};
  std::array<char, 1024> receive_buffer{};
  std::thread thread;
  std::atomic_bool running{false};
  StunServerStats stats;
  Bucket global{40, 0};
  std::unordered_map<std::uint32_t, Bucket> sources;
  std::uint64_t expires_at = 0;
  struct Cookie {
    Endpoint source;
    std::size_t socket_index;
    std::array<std::uint8_t, 12> transaction;
    std::array<std::uint8_t, 16> value;
    std::uint64_t expires;
    unsigned remaining = 8;
  };
  std::vector<Cookie> cookies;

  bool FilteringReply(std::span<const std::uint8_t> request, const Endpoint &source,
                      std::size_t index, std::array<std::uint8_t, 64> &reply,
                      std::size_t &reply_index, std::uint8_t &operation) {
    if (!stats.filtering_diagnostics || request.size() != 64 ||
        Read16(request.data()) != 1 || Read16(request.data() + 2) != 44 ||
        Read32(request.data() + 4) != kCookie ||
        Read16(request.data() + 20) != 0xc0a1 || Read16(request.data() + 22) != 20 ||
        request[24] != 1 || request[26] || request[27] ||
        Read16(request.data() + 44) != 0x8026 || Read16(request.data() + 46) != 8 ||
        Read16(request.data() + 56) != 0x8028 || Read16(request.data() + 58) != 4 ||
        Read32(request.data() + 60) != Fingerprint(request.first(56)) ||
        !std::all_of(request.begin() + 48, request.begin() + 56, [](auto v) { return v == 0; }))
      return false;
    operation = request[25];
    const auto now = uv_now(&loop);
    std::erase_if(cookies, [now](const auto &c) { return c.expires <= now; });
    auto same_source = [&](const Cookie &c) {
      return c.socket_index == index && c.source.address() == source.address() &&
             c.source.port() == source.port();
    };
    auto match = cookies.end();
    if (operation == 1) {
      if (!std::all_of(request.begin() + 28, request.begin() + 44, [](auto v) { return v == 0; }))
        return false;
      match = std::find_if(cookies.begin(), cookies.end(), [&](const auto &c) {
        return same_source(c) && std::equal(c.transaction.begin(), c.transaction.end(), request.begin() + 8);
      });
      if (match == cookies.end()) {
        if (cookies.size() == 256) return false; // bounded, never evict an active proof
        Cookie c{source, index, {}, {}, now + 30000};
        std::copy_n(request.data() + 8, 12, c.transaction.begin());
        if (uv_random(nullptr, nullptr, c.value.data(), c.value.size(), 0, nullptr) != 0)
          return false;
        cookies.push_back(c);
        match = cookies.end() - 1;
      }
      reply_index = index;
    } else if (operation == 2) {
      match = std::find_if(cookies.begin(), cookies.end(), [&](const auto &c) {
        return same_source(c) && std::equal(c.value.begin(), c.value.end(), request.begin() + 28);
      });
      if (match == cookies.end() || match->remaining == 0) return false;
      --match->remaining;
      reply_index = 1 - index; // fixed sibling port, never user-supplied destinations
    } else return false;
    // Reuse the standard source validation and XOR-MAPPED codec with a bare header.
    std::array<std::uint8_t, 20> bare{};
    std::copy_n(request.data(), 20, bare.data());
    Put16(bare.data() + 2, 0);
    std::array<std::uint8_t, 40> base{};
    std::size_t base_size;
    if (!MakeStunBindingReply(bare, source, base, base_size)) return false;
    reply.fill(0);
    std::copy_n(base.data(), 32, reply.data());
    Put16(reply.data() + 2, 44);
    Put16(reply.data() + 32, 0xc0a1);
    Put16(reply.data() + 34, 20);
    reply[36] = 1; reply[37] = operation;
    std::copy(match->value.begin(), match->value.end(), reply.begin() + 40);
    Put16(reply.data() + 56, 0x8028); Put16(reply.data() + 58, 4);
    Put32(reply.data() + 60, Fingerprint(std::span(reply).first(56)));
    return true;
  }

  void Close(const char *reason) {
    if (!running.exchange(false))
      return;
    {
      std::lock_guard lock(stats_mutex);
      stats.running = false;
      stats.stop_reason = reason;
    }
    uv_walk(
        &loop,
        [](uv_handle_t *h, void *) {
          if (!uv_is_closing(h))
            uv_close(h, nullptr);
        },
        nullptr);
  }

  bool Allow(std::uint32_t address) {
    const auto now = uv_now(&loop);
    if (!global.Take(now, 20, 40))
      return false;
    auto it = sources.find(address);
    if (it == sources.end()) {
      if (sources.size() == 256) {
        auto oldest = std::min_element(sources.begin(), sources.end(),
                                       [](const auto &a, const auto &b) {
                                         return a.second.last < b.second.last;
                                       });
        sources.erase(oldest);
      }
      it = sources.emplace(address, Bucket{8, now}).first;
    }
    return it->second.Take(now, 4, 8);
  }

  static void Receive(uv_udp_t *socket, ssize_t nread, const uv_buf_t *buffer,
                      const sockaddr *source, unsigned flags) noexcept {
    auto *self = static_cast<Impl *>(socket->data);
    try {
      self->ReceivePacket(socket, nread, buffer, source, flags);
    } catch (...) {
      self->Close("internal-error");
    }
  }

  void ReceivePacket(uv_udp_t *socket, ssize_t nread, const uv_buf_t *buffer,
                     const sockaddr *source, unsigned flags) {
    std::lock_guard lock(stats_mutex);
    if (nread < 0) {
      ++stats.receive_errors;
      return;
    }
    if (source == nullptr)
      return;
    ++stats.datagrams_received;
    if (source->sa_family != AF_INET) {
      ++stats.invalid_requests;
      return;
    }
    const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(source);
    if (!Allow(ipv4->sin_addr.s_addr)) {
      ++stats.rate_limited;
      return;
    }
    const auto endpoint = Endpoint::FromSockaddr(source, sizeof(sockaddr_in));
    std::array<std::uint8_t, 40> reply{};
    std::array<std::uint8_t, 64> filtering_reply{};
    const std::size_t socket_index = socket == &sockets[0] ? 0 : 1;
    std::size_t response_index = socket_index;
    std::uint8_t filtering_operation = 0;
    std::size_t size = 0;
    const std::span packet(reinterpret_cast<const std::uint8_t *>(buffer->base),
                           static_cast<std::size_t>(nread));
    const bool filtering = packet.size() >= 24 && Read16(packet.data() + 20) == 0xc0a1;
    if ((flags & UV_UDP_PARTIAL) || !endpoint ||
        !(filtering ? FilteringReply(packet, *endpoint, socket_index, filtering_reply,
                                     response_index, filtering_operation)
                    : MakeStunBindingReply(packet, *endpoint, reply, size))) {
      ++stats.invalid_requests;
      return;
    }
    if (filtering) size = filtering_reply.size();
    auto data = uv_buf_init(reinterpret_cast<char *>(filtering ? filtering_reply.data() : reply.data()),
                            static_cast<unsigned>(size));
    const int result = uv_udp_try_send(&sockets[response_index], &data, 1, source);
    const int status =
        result == static_cast<int>(size) ? 0 : (result < 0 ? result : UV_EIO);
    ++stats.bindings;
    if (status == 0)
      ++stats.sends_accepted;
    else
      ++stats.send_errors;
    if (!stats.diagnostics)
      return;
    if (stats.observations.size() == 128) {
      stats.observations.erase(stats.observations.begin());
      ++stats.observations_dropped;
    }
    StunObservation record;
    record.sequence = stats.bindings;
    record.unix_ms = UnixMs();
    record.source = *endpoint;
    record.server_port = stats.ports[socket == &sockets[0] ? 0 : 1];
    record.response_port = stats.ports[response_index];
    record.filtering_operation = filtering_operation;
    std::copy_n(packet.data() + 8, 12, record.transaction.data());
    record.request_bytes = static_cast<std::uint16_t>(nread);
    record.response_bytes = static_cast<std::uint16_t>(size);
    record.send_status = status;
    stats.observations.push_back(record);
  }

  int Start(const StunServerConfig &config) {
    std::lock_guard lock(lifecycle_mutex);
    if (running)
      return UV_EALREADY;
    if (thread.joinable())
      thread.join();
    const auto bind = Endpoint::Parse(config.bind_address, config.ports[0]);
    if (!bind || bind->family() != AddressFamily::ipv4 ||
        config.ports[0] < 1024 || config.ports[1] < 1024 ||
        config.ports[0] == config.ports[1] || config.duration_seconds > 86400)
      return UV_EINVAL;
    {
      std::lock_guard stats_lock(stats_mutex);
      stats = {};
      stats.diagnostics = config.diagnostics;
      stats.filtering_diagnostics = config.filtering_diagnostics;
      stats.ports = config.ports;
      stats.started_unix_ms = UnixMs();
      stats.observations.reserve(128);
    }
    int result = uv_loop_init(&loop);
    if (result != 0)
      return result;
    running = true; // Also permits all-or-nothing cleanup during startup.
    auto fail = [this](int error) {
      Close("start-failed");
      uv_run(&loop, UV_RUN_DEFAULT);
      uv_loop_close(&loop);
      return error;
    };
    stop.data = this;
    result = uv_async_init(&loop, &stop, [](uv_async_t *h) {
      static_cast<Impl *>(h->data)->Close("stop-requested");
    });
    if (result != 0)
      return fail(result);
    sources.clear();
    cookies.clear();
    global = {40, uv_now(&loop)};
    expires_at =
        config.duration_seconds
            ? uv_now(&loop) + std::uint64_t(config.duration_seconds) * 1000
            : 0;
    for (std::size_t i = 0; i < sockets.size(); ++i) {
      auto &socket = sockets[i];
      result = uv_udp_init(&loop, &socket);
      if (result != 0)
        return fail(result);
      socket.data = this;
      const auto endpoint =
          Endpoint::Parse(config.bind_address, config.ports[i]);
      result = uv_udp_bind(&socket, endpoint->sockaddr_ptr(), 0); // no reuse
      if (result != 0)
        return fail(result);
      result = uv_udp_recv_start(
          &socket,
          [](uv_handle_t *h, std::size_t, uv_buf_t *buf) {
            auto *self = static_cast<Impl *>(h->data);
            *buf =
                uv_buf_init(self->receive_buffer.data(),
                            static_cast<unsigned>(self->receive_buffer.size()));
          },
          Receive);
      if (result != 0)
        return fail(result);
    }
    result = uv_timer_init(&loop, &timer);
    if (result != 0)
      return fail(result);
    timer.data = this;
    result = uv_timer_start(
        &timer,
        [](uv_timer_t *h) {
          auto *self = static_cast<Impl *>(h->data);
          if (self->expires_at && uv_now(&self->loop) >= self->expires_at)
            self->Close("duration-limit");
        },
        1000, 1000);
    if (result != 0)
      return fail(result);
    {
      std::lock_guard stats_lock(stats_mutex);
      stats.running = true;
      stats.stop_reason = "running";
    }
    try {
      thread = std::thread([this] {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_loop_close(&loop);
      });
    } catch (...) {
      return fail(UV_ENOMEM);
    }
    return 0;
  }
  void Stop() {
    std::lock_guard lock(lifecycle_mutex);
    // Synchronize the async send with loop-side Close; both use stats_mutex.
    {
      std::lock_guard stats_lock(stats_mutex);
      if (running)
        uv_async_send(&stop);
    }
    if (thread.joinable())
      thread.join();
  }
};

StunServer::StunServer() : impl_(std::make_unique<Impl>()) {}
StunServer::~StunServer() { Stop(); }
int StunServer::Start(const StunServerConfig &config) {
  return impl_->Start(config);
}
void StunServer::Stop() { impl_->Stop(); }
StunServerStats StunServer::Snapshot() const {
  std::lock_guard lock(impl_->stats_mutex);
  return impl_->stats;
}
} // namespace libnet
