#include "common/http/filter/fault_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/event/timer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Http {

const std::string FaultFilter::DELAY_PERCENT_KEY = "fault.http.delay.fixed_delay_percent";
const std::string FaultFilter::ABORT_PERCENT_KEY = "fault.http.abort.abort_percent";
const std::string FaultFilter::DELAY_DURATION_KEY = "fault.http.delay.fixed_duration_ms";
const std::string FaultFilter::ABORT_HTTP_STATUS_KEY = "fault.http.abort.http_status";

FaultFilterConfig::FaultFilterConfig(const Json::Object& json_config, Runtime::Loader& runtime,
                                     const std::string& stats_prefix, Stats::Scope& scope)
    : runtime_(runtime), stats_(generateStats(stats_prefix, scope)), stats_prefix_(stats_prefix),
      scope_(scope) {

  json_config.validateSchema(Json::Schema::FAULT_HTTP_FILTER_SCHEMA);

  const Json::ObjectSharedPtr config_abort = json_config.getObject("abort", true);
  const Json::ObjectSharedPtr config_delay = json_config.getObject("delay", true);

  if (config_abort->empty() && config_delay->empty()) {
    throw EnvoyException("fault filter must have at least abort or delay specified in the config.");
  }

  if (!config_abort->empty()) {
    abort_percent_ = static_cast<uint64_t>(config_abort->getInteger("abort_percent", 0));

    // TODO(mattklein123): Throw error if invalid return code is provided
    http_status_ = static_cast<uint64_t>(config_abort->getInteger("http_status"));
  }

  if (!config_delay->empty()) {
    const std::string type = config_delay->getString("type");
    ASSERT(type == "fixed");
    UNREFERENCED_PARAMETER(type);
    fixed_delay_percent_ =
        static_cast<uint64_t>(config_delay->getInteger("fixed_delay_percent", 0));
    fixed_duration_ms_ = static_cast<uint64_t>(config_delay->getInteger("fixed_duration_ms", 0));
  }

  if (json_config.hasObject("headers")) {
    std::vector<Json::ObjectSharedPtr> config_headers = json_config.getObjectArray("headers");
    for (const Json::ObjectSharedPtr& header_map : config_headers) {
      fault_filter_headers_.push_back(*header_map);
    }
  }

  upstream_cluster_ = json_config.getString("upstream_cluster", EMPTY_STRING);

  if (json_config.hasObject("downstream_nodes")) {
    std::vector<std::string> nodes = json_config.getStringArray("downstream_nodes");
    downstream_nodes_.insert(nodes.begin(), nodes.end());
  }
}

FaultFilter::FaultFilter(FaultFilterConfigSharedPtr config) : config_(config) {}

FaultFilter::~FaultFilter() { ASSERT(!delay_timer_); }

// Delays and aborts are independent events. One can inject a delay
// followed by an abort or inject just a delay or abort. In this callback,
// if we inject a delay, then we will inject the abort in the delay timer
// callback.
FilterHeadersStatus FaultFilter::decodeHeaders(HeaderMap& headers, bool) {
  if (!matchesTargetUpstreamCluster()) {
    return FilterHeadersStatus::Continue;
  }

  if (!matchesDownstreamNodes(headers)) {
    return FilterHeadersStatus::Continue;
  }

  // Check for header matches
  if (!Router::ConfigUtility::matchHeaders(headers, config_->filterHeaders())) {
    return FilterHeadersStatus::Continue;
  }

  if (headers.EnvoyDownstreamServiceCluster()) {
    downstream_cluster_ = headers.EnvoyDownstreamServiceCluster()->value().c_str();

    downstream_cluster_delay_percent_key_ =
        fmt::format("fault.http.{}.delay.fixed_delay_percent", downstream_cluster_);
    downstream_cluster_abort_percent_key_ =
        fmt::format("fault.http.{}.abort.abort_percent", downstream_cluster_);
    downstream_cluster_delay_duration_key_ =
        fmt::format("fault.http.{}.delay.fixed_duration_ms", downstream_cluster_);
    downstream_cluster_abort_http_status_key_ =
        fmt::format("fault.http.{}.abort.http_status", downstream_cluster_);
  }

  Optional<uint64_t> duration_ms = delayDuration();
  if (duration_ms.valid()) {
    delay_timer_ = callbacks_->dispatcher().createTimer([this]() -> void { postDelayInjection(); });
    delay_timer_->enableTimer(std::chrono::milliseconds(duration_ms.value()));
    recordDelaysInjectedStats();
    callbacks_->requestInfo().setResponseFlag(Http::AccessLog::ResponseFlag::DelayInjected);
    return FilterHeadersStatus::StopIteration;
  }

  if (isAbortEnabled()) {
    abortWithHTTPStatus();
    return FilterHeadersStatus::StopIteration;
  }

  return FilterHeadersStatus::Continue;
}

bool FaultFilter::isDelayEnabled() {
  bool enabled =
      config_->runtime().snapshot().featureEnabled(DELAY_PERCENT_KEY, config_->delayPercent());

  if (!downstream_cluster_delay_percent_key_.empty()) {
    enabled |= config_->runtime().snapshot().featureEnabled(downstream_cluster_delay_percent_key_,
                                                            config_->delayPercent());
  }

  return enabled;
}

bool FaultFilter::isAbortEnabled() {
  bool enabled =
      config_->runtime().snapshot().featureEnabled(ABORT_PERCENT_KEY, config_->abortPercent());

  if (!downstream_cluster_abort_percent_key_.empty()) {
    enabled |= config_->runtime().snapshot().featureEnabled(downstream_cluster_abort_percent_key_,
                                                            config_->abortPercent());
  }

  return enabled;
}

Optional<uint64_t> FaultFilter::delayDuration() {
  Optional<uint64_t> ret;

  if (!isDelayEnabled()) {
    return ret;
  }

  uint64_t duration =
      config_->runtime().snapshot().getInteger(DELAY_DURATION_KEY, config_->delayDuration());
  if (!downstream_cluster_delay_duration_key_.empty()) {
    duration =
        config_->runtime().snapshot().getInteger(downstream_cluster_delay_duration_key_, duration);
  }

  // Delay only if the duration is >0ms
  if (duration > 0) {
    ret.value(duration);
  }

  return ret;
}

std::string FaultFilter::abortHttpStatus() {
  // TODO(mattklein123): check http status codes obtained from runtime.
  uint64_t http_status =
      config_->runtime().snapshot().getInteger(ABORT_HTTP_STATUS_KEY, config_->abortCode());

  if (!downstream_cluster_abort_http_status_key_.empty()) {
    http_status = config_->runtime().snapshot().getInteger(
        downstream_cluster_abort_http_status_key_, http_status);
  }

  return std::to_string(http_status);
}

void FaultFilter::recordDelaysInjectedStats() {
  // Downstream specific stats.
  if (!downstream_cluster_.empty()) {
    const std::string stats_counter =
        fmt::format("{}fault.{}.delays_injected", config_->statsPrefix(), downstream_cluster_);

    config_->scope().counter(stats_counter).inc();
  }

  // General stats.
  config_->stats().delays_injected_.inc();
}

void FaultFilter::recordAbortsInjectedStats() {
  // Downstream specific stats.
  if (!downstream_cluster_.empty()) {
    const std::string stats_counter =
        fmt::format("{}fault.{}.aborts_injected", config_->statsPrefix(), downstream_cluster_);

    config_->scope().counter(stats_counter).inc();
  }

  // General stats.
  config_->stats().aborts_injected_.inc();
}

FilterDataStatus FaultFilter::decodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
}

FilterTrailersStatus FaultFilter::decodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

FaultFilterStats FaultFilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  std::string final_prefix = prefix + "fault.";
  return {ALL_FAULT_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

void FaultFilter::onDestroy() { resetTimerState(); }

void FaultFilter::postDelayInjection() {
  resetTimerState();

  // Delays can be followed by aborts
  if (isAbortEnabled()) {
    abortWithHTTPStatus();
  } else {
    // Continue request processing.
    callbacks_->continueDecoding();
  }
}

void FaultFilter::abortWithHTTPStatus() {
  Http::HeaderMapPtr response_headers{
      new HeaderMapImpl{{Headers::get().Status, abortHttpStatus()}}};
  callbacks_->encodeHeaders(std::move(response_headers), true);
  recordAbortsInjectedStats();
  callbacks_->requestInfo().setResponseFlag(Http::AccessLog::ResponseFlag::FaultInjected);
}

bool FaultFilter::matchesTargetUpstreamCluster() {
  bool matches = true;

  if (!config_->upstreamCluster().empty()) {
    Router::RouteConstSharedPtr route = callbacks_->route();
    matches = route && route->routeEntry() &&
              (route->routeEntry()->clusterName() == config_->upstreamCluster());
  }

  return matches;
}

bool FaultFilter::matchesDownstreamNodes(const HeaderMap& headers) {
  if (config_->downstreamNodes().empty()) {
    return true;
  }

  if (!headers.EnvoyDownstreamServiceNode()) {
    return false;
  }

  const std::string downstream_node = headers.EnvoyDownstreamServiceNode()->value().c_str();
  return config_->downstreamNodes().find(downstream_node) != config_->downstreamNodes().end();
}

void FaultFilter::resetTimerState() {
  if (delay_timer_) {
    delay_timer_->disableTimer();
    delay_timer_.reset();
  }
}

void FaultFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // Http
} // Envoy
