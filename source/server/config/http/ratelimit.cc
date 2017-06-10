#include "server/config/http/ratelimit.h"

#include <chrono>
#include <string>

#include "common/http/filter/ratelimit.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb RateLimitFilterConfig::createFilterFactory(HttpFilterType type,
                                                               const Json::Object& config,
                                                               const std::string&,
                                                               FactoryContext& context) {
  if (type != HttpFilterType::Decoder) {
    throw EnvoyException(
        fmt::format("{} http filter must be configured as a decoder filter.", name()));
  }

  Http::RateLimit::FilterConfigSharedPtr filter_config(new Http::RateLimit::FilterConfig(
      config, context.localInfo(), context.stats(), context.runtime(), context.clusterManager()));
  return [filter_config, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{new Http::RateLimit::Filter(
        filter_config, context.rateLimitClient(std::chrono::milliseconds(20)))});
  };
}

std::string RateLimitFilterConfig::name() { return "rate_limit"; }

/**
 * Static registration for the rate limit filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<RateLimitFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
