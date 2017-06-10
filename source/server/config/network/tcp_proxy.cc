#include "server/config/network/tcp_proxy.h"

#include <string>

#include "envoy/network/connection.h"

#include "common/filter/tcp_proxy.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb TcpProxyConfigFactory::createFilterFactory(NetworkFilterType type,
                                                                  const Json::Object& config,
                                                                  FactoryContext& context) {
  if (type != NetworkFilterType::Read) {
    throw EnvoyException(
        fmt::format("{} network filter must be configured as a read filter.", name()));
  }

  Filter::TcpProxyConfigSharedPtr filter_config(
      new Filter::TcpProxyConfig(config, context.clusterManager(), context.stats()));
  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{
        new Filter::TcpProxy(filter_config, context.clusterManager())});
  };
}

std::string TcpProxyConfigFactory::name() { return "tcp_proxy"; }

/**
 * Static registration for the tcp_proxy filter. @see RegisterNamedNetworkFilterConfigFactory.
 */
static RegisterNamedNetworkFilterConfigFactory<TcpProxyConfigFactory> registered_;

} // Configuration
} // Server
} // Envoy
