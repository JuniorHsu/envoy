#pragma once

#include <chrono>

#include "envoy/upstream/upstream.h"

#include "source/extensions/filters/network/thrift_proxy/thrift.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Extends Upstream::ProtocolOptionsConfig with Thrift-specific cluster options.
 */
class ProtocolOptionsConfig : public Upstream::ProtocolOptionsConfig {
public:
  ~ProtocolOptionsConfig() override = default;

  virtual TransportType transport(TransportType downstream_transport) const PURE;
  virtual ProtocolType protocol(ProtocolType downstream_protocol) const PURE;
  virtual absl::optional<std::chrono::milliseconds> idleTimeout() const PURE;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
