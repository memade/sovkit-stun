// SPDX-License-Identifier: MIT
#include "libnet_stun_server.h"

#include <charconv>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void OnSignal(int) { stop_requested = 1; }

std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r");
  if (first == std::string::npos) return {};
  return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

unsigned Number(const std::string &value, unsigned minimum, unsigned maximum) {
  unsigned result = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
      result < minimum || result > maximum)
    throw std::runtime_error("invalid numeric value");
  return result;
}

using Settings = std::map<std::string, std::string>;
void Insert(Settings &settings, const std::string &key, const std::string &value) {
  if (value.empty() || !settings.emplace(key, value).second)
    throw std::runtime_error("empty or duplicate setting");
}

Settings ReadConfig(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot open config");
  Settings settings;
  std::string line;
  std::size_t total = 0;
  // Bounded even for a file with no newlines. No includes or expansion.
  for (char c; file.get(c);) {
    if (++total > 16384 || c == '\0')
      throw std::runtime_error("config too large or contains NUL");
    line += c;
  }
  if (!file.eof()) throw std::runtime_error("cannot read config");
  std::size_t offset = 0;
  while (offset < line.size()) {
    const auto end = line.find('\n', offset);
    auto row = Trim(line.substr(offset, end == std::string::npos ? end : end - offset));
    if (!row.empty() && row.front() != '#') {
      const auto equals = row.find('=');
      if (equals == std::string::npos) throw std::runtime_error("expected key=value");
      Insert(settings, Trim(row.substr(0, equals)), Trim(row.substr(equals + 1)));
    }
    if (end == std::string::npos) break;
    offset = end + 1;
  }
  return settings;
}

libnet::StunServerConfig Configure(int argc, char **argv) {
  Settings settings;
  if (argc > 1 && std::string_view(argv[1]) == "--config") {
    if (argc != 3) throw std::runtime_error("--config takes one path; do not mix with other options");
    settings = ReadConfig(argv[2]);
  } else {
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--filtering-diagnostics") {
        Insert(settings, "filtering_diagnostics", "true");
        continue;
      }
      if (option != "--bind" && option != "--ports" && option != "--duration")
        throw std::runtime_error("unknown option; see --help");
      if (++i == argc) throw std::runtime_error("missing option value");
      const std::string value = argv[i];
      if (option == "--bind") Insert(settings, "bind_address", value);
      else if (option == "--duration") Insert(settings, "duration_seconds", value);
      else {
        const auto comma = value.find(',');
        if (comma == std::string::npos) throw std::runtime_error("expected --ports A,B");
        Insert(settings, "port_a", value.substr(0, comma));
        Insert(settings, "port_b", value.substr(comma + 1));
      }
    }
  }
  libnet::StunServerConfig config;
  for (const auto &[key, value] : settings) {
    if (key == "bind_address") config.bind_address = value;
    else if (key == "port_a") config.ports[0] = Number(value, 1024, 65535);
    else if (key == "port_b") config.ports[1] = Number(value, 1024, 65535);
    else if (key == "duration_seconds") config.duration_seconds = Number(value, 0, 86400);
    else if (key == "filtering_diagnostics") {
      if (value != "true" && value != "false") throw std::runtime_error("expected true or false");
      config.filtering_diagnostics = value == "true";
    } else throw std::runtime_error("unknown config key");
  }
  const auto bind = libnet::Endpoint::Parse(config.bind_address, config.ports[0]);
  if (!bind || bind->family() != libnet::AddressFamily::ipv4 || config.ports[0] == config.ports[1])
    throw std::runtime_error("expected numeric IPv4 and two distinct non-privileged ports");
  const auto ip = ntohl(reinterpret_cast<const sockaddr_in *>(bind->sockaddr_ptr())->sin_addr.s_addr);
  if (ip >= 0xe0000000U) throw std::runtime_error("multicast/reserved bind address is not supported");
  // No CLI switch exposes per-request IP addresses, transaction IDs or cookies.
  config.diagnostics = false;
  return config;
}

void Summary(const char *event, const libnet::StunServerStats &s) {
  std::cout << "{\"event\":\"" << event << "\",\"reason\":\"" << s.stop_reason
            << "\",\"datagrams_received\":" << s.datagrams_received
            << ",\"invalid_requests\":" << s.invalid_requests
            << ",\"rate_limited\":" << s.rate_limited
            << ",\"bindings\":" << s.bindings
            << ",\"sends_accepted\":" << s.sends_accepted
            << ",\"send_errors\":" << s.send_errors
            << ",\"receive_errors\":" << s.receive_errors << "}" << std::endl;
}
} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::cout << "sovkit-stun " STUN_VERSION " - IPv4 UDP Binding server\n"
                 "Usage: sovkit-stun [--bind IPv4] [--ports A,B] [--duration SECONDS]\n"
                 "                   [--filtering-diagnostics]\n"
                 "       sovkit-stun --config PATH\n"
                 "Defaults: 127.0.0.1, 3478/3479, duration 0 (until SIGINT/SIGTERM).\n"
                 "Config cannot be combined with CLI settings. Unknown/duplicate settings fail.\n"
                 "No TURN, relay, authentication or persistent application data.\n";
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "sovkit-stun " STUN_VERSION << " (libuv " << uv_version_string() << ")\n";
    return 0;
  }
  libnet::StunServerConfig config;
  try { config = Configure(argc, argv); }
  catch (const std::exception &error) {
    std::cerr << "configuration error: " << error.what() << '\n';
    return 2;
  }
  try {
    if (std::signal(SIGINT, OnSignal) == SIG_ERR || std::signal(SIGTERM, OnSignal) == SIG_ERR)
      throw std::runtime_error("cannot install signal handlers");
    libnet::StunServer server;
    const int status = server.Start(config);
    if (status != 0) {
      std::cerr << "start failed: " << uv_err_name(status) << '\n';
      return 1;
    }
    std::cout << "{\"event\":\"started\",\"version\":\"" STUN_VERSION
              "\",\"bind_address\":\"" << config.bind_address
              << "\",\"ports\":[" << config.ports[0] << ',' << config.ports[1]
              << "],\"filtering_diagnostics\":" << (config.filtering_diagnostics ? "true" : "false")
              << "}" << std::endl;
    auto next_report = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!stop_requested && server.Snapshot().running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (std::chrono::steady_clock::now() >= next_report) {
        Summary("stats", server.Snapshot());
        next_report = std::chrono::steady_clock::now() + std::chrono::seconds(30);
      }
    }
    server.Stop();
    const auto stats = server.Snapshot();
    Summary("stopped", stats);
    return stats.stop_reason == "internal-error" ? 1 : 0;
  } catch (const std::exception &) {
    std::cerr << "runtime failure\n";
    return 1;
  }
}
