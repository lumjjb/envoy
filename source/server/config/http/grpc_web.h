#pragma once

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class GrpcWebFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(HttpFilterType type, const Json::Object&,
                                          const std::string&, FactoryContext&) override;

  std::string name() override;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
