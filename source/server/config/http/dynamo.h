#pragma once

#include <string>

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for http dynamodb filter.
 */
class DynamoFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(HttpFilterType type, const Json::Object&,
                                          const std::string& stat_prefix,
                                          FactoryContext& context) override;
  std::string name() override;
};

} // Configuration
} // Server
} // Envoy
