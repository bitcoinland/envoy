#include "server/config_validation/server.h"

#include "common/common/version.h"
#include "common/config/bootstrap_json.h"
#include "common/local_info/local_info_impl.h"
#include "common/protobuf/utility.h"
#include "common/singleton/manager_impl.h"

#include "server/configuration_impl.h"

#include "api/bootstrap.pb.h"

namespace Envoy {
namespace Server {

bool validateConfig(Options& options, Network::Address::InstanceConstSharedPtr local_address,
                    ComponentFactory& component_factory) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;

  try {
    ValidationInstance server(options, local_address, stats_store, access_log_lock,
                              component_factory);
    std::cout << "configuration '" << options.configPath() << "' OK" << std::endl;
    server.shutdown();
    return true;
  } catch (const EnvoyException& e) {
    return false;
  }
}

ValidationInstance::ValidationInstance(Options& options,
                                       Network::Address::InstanceConstSharedPtr local_address,
                                       Stats::IsolatedStoreImpl& store,
                                       Thread::BasicLockable& access_log_lock,
                                       ComponentFactory& component_factory)
    : options_(options), stats_store_(store),
      api_(new Api::ValidationImpl(options.fileFlushIntervalMsec())),
      dispatcher_(api_->allocateDispatcher()), singleton_manager_(new Singleton::ManagerImpl()),
      access_log_manager_(*api_, *dispatcher_, access_log_lock, store),
      listener_manager_(*this, *this, *this) {
  try {
    initialize(options, local_address, component_factory);
  } catch (const EnvoyException& e) {
    ENVOY_LOG(critical, "error initializing configuration '{}': {}", options.configPath(),
              e.what());
    thread_local_.shutdownThread();
    throw;
  }
}

void ValidationInstance::initialize(Options& options,
                                    Network::Address::InstanceConstSharedPtr local_address,
                                    ComponentFactory& component_factory) {
  // See comments on InstanceImpl::initialize() for the overall flow here.
  //
  // For validation, we only do a subset of normal server initialization: everything that could fail
  // on a malformed config (e.g. JSON parsing and all the object construction that follows), but
  // more importantly nothing with observable effects (e.g. binding to ports or shutting down any
  // other Envoy process).
  //
  // If we get all the way through that stripped-down initialization flow, to the point where we'd
  // be ready to serve, then the config has passed validation.
  // Handle configuration that needs to take place prior to the main configuration load.
  envoy::api::v2::Bootstrap bootstrap;
  try {
    MessageUtil::loadFromFile(options.configPath(), bootstrap);
  } catch (const EnvoyException& e) {
    // TODO(htuch): When v1 is deprecated, make this a warning encouraging config upgrade.
    ENVOY_LOG(debug, "Unable to initialize config as v2, will retry as v1: {}", e.what());
  }
  if (!bootstrap.has_admin()) {
    Json::ObjectSharedPtr config_json = Json::Factory::loadFromFile(options.configPath());
    Config::BootstrapJson::translateBootstrap(*config_json, bootstrap);
  }
  bootstrap.mutable_node()->set_build_version(VersionInfo::version());

  local_info_.reset(
      new LocalInfo::LocalInfoImpl(bootstrap.node(), local_address, options.serviceZone(),
                                   options.serviceClusterName(), options.serviceNodeName()));

  Configuration::InitialImpl initial_config(bootstrap);
  thread_local_.registerThread(*dispatcher_, true);
  runtime_loader_ = component_factory.createRuntime(*this, initial_config);
  ssl_context_manager_.reset(new Ssl::ContextManagerImpl(*runtime_loader_));
  cluster_manager_factory_.reset(new Upstream::ValidationClusterManagerFactory(
      runtime(), stats(), threadLocal(), random(), dnsResolver(), sslContextManager(), dispatcher(),
      localInfo()));

  Configuration::MainImpl* main_config = new Configuration::MainImpl();
  config_.reset(main_config);
  main_config->initialize(bootstrap, *this, *cluster_manager_factory_);

  clusterManager().setInitializedCb(
      [this]() -> void { init_manager_.initialize([]() -> void {}); });
}

void ValidationInstance::shutdown() {
  // This normally happens at the bottom of InstanceImpl::run(), but we don't have a run(). We can
  // do an abbreviated shutdown here since there's less to clean up -- for example, no workers to
  // exit.
  thread_local_.shutdownGlobalThreading();
  config_->clusterManager().shutdown();
  thread_local_.shutdownThread();
}

} // namespace Server
} // namespace Envoy
