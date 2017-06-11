#include "server/config/http/router.h"

#include <string>

#include "common/json/config_schemas.h"
#include "common/router/router.h"
#include "common/router/shadow_writer_impl.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb RouterFilterConfig::createFilterFactory(HttpFilterType type,
                                                            const Json::Object& json_config,
                                                            const std::string& stat_prefix,
                                                            FactoryContext& context) {
  if (type != HttpFilterType::Decoder) {
    throw EnvoyException(
        fmt::format("{} http filter must be configured as a decoder filter.", name()));
  }

  json_config.validateSchema(Json::Schema::ROUTER_HTTP_FILTER_SCHEMA);

  Router::FilterConfigSharedPtr config(new Router::FilterConfig(
      stat_prefix, context.localInfo(), context.stats(), context.clusterManager(),
      context.runtime(), context.random(),
      Router::ShadowWriterPtr{new Router::ShadowWriterImpl(context.clusterManager())},
      json_config.getBoolean("dynamic_stats", true)));

  return [config](Http::FilterChainFactoryCallbacks& callbacks)
      -> void { callbacks.addStreamDecoderFilter(std::make_shared<Router::ProdFilter>(*config)); };
}

std::string RouterFilterConfig::name() { return "router"; }

/**
 * Static registration for the router filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<RouterFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
