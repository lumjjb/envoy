#include "server/config/http/dynamo.h"

#include <string>

#include "common/dynamo/dynamo_filter.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb DynamoFilterConfig::createFilterFactory(HttpFilterType type,
                                                            const Json::Object&,
                                                            const std::string& stat_prefix,
                                                            FactoryContext& context) {
  if (type != HttpFilterType::Both) {
    throw EnvoyException(fmt::format(
        "{} http filter must be configured as both a decoder and encoder filter.", name()));
  }

  return [&context, stat_prefix](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{
        new Dynamo::DynamoFilter(context.runtime(), stat_prefix, context.stats())});
  };
}

std::string DynamoFilterConfig::name() { return "http_dynamo_filter"; }

/**
 * Static registration for the http dynamodb filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<DynamoFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
