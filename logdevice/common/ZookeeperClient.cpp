/**
 * Copyright (c) 2017-present, Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "logdevice/common/ZookeeperClient.h"

#include <cerrno>
#include <cstdio>
#include <new>

#include "logdevice/common/ConstructorFailed.h"
#include "logdevice/common/debug.h"
#include "logdevice/include/Err.h"

namespace facebook { namespace logdevice {

ZookeeperClient::ZookeeperClient(std::string quorum,
                                 std::chrono::milliseconds session_timeout)
    : ZookeeperClientBase(quorum), session_timeout_(session_timeout) {
  ld_check(session_timeout.count() > 0);

  zoo_set_log_stream(fdopen(dbg::getFD(), "w"));
  int rv = reconnect(nullptr);
  if (rv != 0) {
    ld_check(err != E::STALE);
    throw ConstructorFailed();
  }

  ld_check(zh_.get()); // initialized by reconnect()
}

ZookeeperClient::~ZookeeperClient() {
  zh_.update(nullptr);
}

int ZookeeperClient::reconnect(zhandle_t* prev) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (prev && zh_.get().get() != prev) {
    err = E::STALE;
    return -1;
  }

  ld_check(!prev || zoo_state(prev) == ZOO_EXPIRED_SESSION_STATE);

  // prev will not be leaked. If zookeeper_init() below succeeds,
  // zookeeper_close() will be called on prev in zh_.update() before
  // the function returns. Otherwise prev will remain owned
  // by this ZookeeperClient through zh_.

  zhandle_t* next = zookeeper_init(quorum_.c_str(),
                                   &ZookeeperClient::sessionWatcher,
                                   session_timeout_.count(),
                                   nullptr,
                                   this,
                                   0);
  if (!next) {
    switch (errno) {
      case ENOMEM:
        throw std::bad_alloc();
      case EMFILE:
      case ENFILE: // failed to create a pipe to ZK thread
        err = E::SYSLIMIT;
        break;
      case EINVAL:
        err = E::INVALID_PARAM;
        break;
      default:
        ld_error("Unexpected errno value %d (%s) from zookeeper_init()",
                 errno,
                 strerror(errno));
        err = E::INTERNAL;
    }
    return -1;
  }

  zh_.update(std::shared_ptr<zhandle_t>(
      next, [](zhandle_t* zh) { zookeeper_close(zh); }));
  return 0;
}

std::string ZookeeperClient::stateString(int state) {
  if (state == ZOO_CONNECTING_STATE) {
    return "ZOO_CONNECTING_STATE";
  }
  if (state == ZOO_ASSOCIATING_STATE) {
    return "ZOO_ASSOCIATING_STATE";
  }
  if (state == ZOO_CONNECTED_STATE) {
    return "ZOO_CONNECTED_STATE";
  }
  if (state == ZOO_EXPIRED_SESSION_STATE) {
    return "ZOO_EXPIRED_SESSION_STATE";
  }
  if (state == ZOO_AUTH_FAILED_STATE) {
    return "ZOO_AUTH_FAILED_STATE";
  }
  char buf[64];
  snprintf(buf, sizeof buf, "unknown state (%d)", state);
  return buf;
}

void ZookeeperClient::setDebugLevel(dbg::Level loglevel) {
  ::ZooLogLevel zdl;

  switch (loglevel) {
    case dbg::Level::CRITICAL:
      zdl = ZOO_LOG_LEVEL_ERROR;
      break;
    case dbg::Level::ERROR:
      zdl = ZOO_LOG_LEVEL_ERROR;
      break;
    case dbg::Level::WARNING:
      zdl = ZOO_LOG_LEVEL_WARN;
      break;
    case dbg::Level::NOTIFY:
      zdl = ZOO_LOG_LEVEL_INFO;
      break;
    case dbg::Level::INFO:
      zdl = ZOO_LOG_LEVEL_INFO;
      break;
    case dbg::Level::DEBUG:
      zdl = ZOO_LOG_LEVEL_DEBUG;
      break;
    case dbg::Level::SPEW:
      zdl = ZOO_LOG_LEVEL_DEBUG;
      break;
    default:
      ld_error("Unknown loglevel: %u. Not changing Zookeeper debug level.",
               (unsigned)loglevel);
      return;
  }

  zoo_set_debug_level(zdl);
}

void ZookeeperClient::sessionWatcher(zhandle_t* zh,
                                     int type,
                                     int /*state*/,
                                     const char* /*path*/,
                                     void* watcherCtx) {
  ld_check(zh);
  ld_check(type == ZOO_SESSION_EVENT); // this is the session watcher, don't
                                       // expect any other events

  int session_state = zoo_state(zh);
  ZookeeperClient* self = reinterpret_cast<ZookeeperClient*>(watcherCtx);

  ld_check(self);

  ld_info(
      "Zookeeper client entered state %s", stateString(session_state).c_str());

  if (session_state == ZOO_EXPIRED_SESSION_STATE) {
    ld_info("Session expired, reconnecting...");
    int rv = self->reconnect(zh);
    if (rv != 0 && err == E::STALE) {
      ld_info("zhandle %p in SessionExpired watch does not match current "
              "zhandle %p in ZookeeperClient. Probably session watcher was "
              "called twice for the same state transition.",
              zh,
              self->zh_.get().get());
    }
  }
}

int ZookeeperClient::state() {
  return zoo_state(zh_.get().get());
}

int ZookeeperClient::setData(const char* znode_path,
                             const char* znode_value,
                             int znode_value_size,
                             int version,
                             stat_completion_t completion,
                             const void* data) {
  return zoo_aset(zh_.get().get(),
                  znode_path,
                  znode_value,
                  znode_value_size,
                  version,
                  completion,
                  data);
}

int ZookeeperClient::getData(const char* znode_path,
                             data_completion_t completion,
                             const void* data) {
  return zoo_aget(
      zh_.get().get(), znode_path, /* watch = */ 0, completion, data);
}

int ZookeeperClient::multiOp(int count,
                             const zoo_op_t* ops,
                             zoo_op_result_t* results,
                             void_completion_t completion,
                             const void* data) {
  return zoo_amulti(zh_.get().get(), count, ops, results, completion, data);
}

/* static */
void ZookeeperClient::getDataCompletion(int rc,
                                        const char* value,
                                        int value_len,
                                        const struct Stat* stat,
                                        const void* context) {
  if (!context) {
    return;
  }
  auto callback = std::unique_ptr<data_callback_t>(const_cast<data_callback_t*>(
      reinterpret_cast<const data_callback_t*>(context)));
  ld_check(callback);
  // callback should not be empty
  ld_check(*callback);
  if (rc == ZOK) {
    ld_check_ge(value_len, 0);
    ld_check(stat);
    (*callback)(
        rc, {value, static_cast<size_t>(value_len)}, zk::Stat{stat->version});
  } else {
    std::string s;
    (*callback)(rc, s, {});
  }
}

int ZookeeperClient::getData(std::string path, data_callback_t cb) {
  // Use the callback function object as context, which must be freed in
  // completion. The callback could also be empty.
  const void* context = nullptr;
  if (cb) {
    auto p = std::make_unique<data_callback_t>(std::move(cb));
    context = p.release();
  }
  return zoo_aget(zh_.get().get(),
                  path.data(),
                  /* watch = */ 0,
                  &ZookeeperClient::getDataCompletion,
                  context);
}

/* static */ void ZookeeperClient::setDataCompletion(int rc,
                                                     const struct Stat* stat,
                                                     const void* context) {
  if (!context) {
    return;
  }
  auto callback = std::unique_ptr<stat_callback_t>(const_cast<stat_callback_t*>(
      reinterpret_cast<const stat_callback_t*>(context)));
  ld_check(callback);
  // callback shouldn't be empty
  ld_check(*callback);
  if (rc == ZOK) {
    ld_check(stat);
    (*callback)(rc, zk::Stat{stat->version});
  } else {
    (*callback)(rc, {});
  }
}

int ZookeeperClient::setData(std::string path,
                             std::string data,
                             stat_callback_t cb,
                             zk::version_t base_version) {
  // Use the callback function object as context, which must be freed in
  // completion. The callback could also be nullptr.
  const void* context = nullptr;
  if (cb) {
    auto p = std::make_unique<stat_callback_t>(std::move(cb));
    context = p.release();
  }
  return zoo_aset(zh_.get().get(),
                  path.data(),
                  data.data(),
                  data.size(),
                  base_version,
                  &ZookeeperClient::setDataCompletion,
                  context);
}

int ZookeeperClient::multiOp(std::vector<zk::Op>, multi_op_callback_t) {
  throw std::runtime_error("unimplemented");
}

}} // namespace facebook::logdevice
