#include "server/config/http/grpc_http1_bridge.h"

#include <string>

#include "common/grpc/http1_bridge_filter.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb GrpcHttp1BridgeFilterConfig::createFilterFactory(HttpFilterType type,
                                                                     const Json::Object&,
                                                                     const std::string&,
                                                                     FactoryContext& context) {
  if (type != HttpFilterType::Both) {
    throw EnvoyException(fmt::format(
        "{} http filter must be configured as both a decoder and encoder filter.", name()));
  }

  return [&context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new Grpc::Http1BridgeFilter(context.clusterManager())});
  };
}

std::string GrpcHttp1BridgeFilterConfig::name() { return "grpc_http1_bridge"; }

/**
 * Static registration for the grpc HTTP1 bridge filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<GrpcHttp1BridgeFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
