/*
Copyright (c) 2024, FirstFive8, Inc

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <sstream>

#include "hiredis.h"

#include "dialog-store.hpp"
#include "controller.hpp"

namespace {
  using std::string;

  /* parse "host:port[,host:port...]" -> first (host,port) */
  bool firstHostPort(const string& s, string& host, unsigned int& port) {
    std::istringstream ss(s);
    string token;
    if (!std::getline(ss, token, ',')) return false;
    size_t start = token.find_first_not_of(" \t");
    size_t end = token.find_last_not_of(" \t");
    if (start == string::npos) return false;
    token = token.substr(start, end - start + 1);
    size_t colon = token.find(':');
    if (colon == string::npos) return false;
    host = token.substr(0, colon);
    port = (unsigned int) ::atoi(token.substr(colon + 1).c_str());
    return port != 0;
  }

  std::vector<std::pair<string, unsigned int>> allHostPorts(const string& s) {
    std::vector<std::pair<string, unsigned int>> out;
    std::istringstream ss(s);
    string token;
    while (std::getline(ss, token, ',')) {
      size_t start = token.find_first_not_of(" \t");
      size_t end = token.find_last_not_of(" \t");
      if (start == string::npos) continue;
      token = token.substr(start, end - start + 1);
      size_t colon = token.find(':');
      if (colon == string::npos) continue;
      out.emplace_back(token.substr(0, colon), (unsigned int) ::atoi(token.substr(colon + 1).c_str()));
    }
    return out;
  }
}

namespace drachtio {

  DialogStore::DialogStore(const string& instanceId, const string& address, unsigned int port,
                           const string& password, unsigned int ownershipTtlSecs) :
    m_instanceId(instanceId), m_address(address), m_port(port ? port : 6379),
    m_password(password), m_useSentinel(false), m_ownershipTtlSecs(ownershipTtlSecs ? ownershipTtlSecs : 30) {
  }

  DialogStore::DialogStore(const string& instanceId, const string& sentinels, const string& masterName,
                           const string& password, unsigned int ownershipTtlSecs, bool /*useSentinel*/) :
    m_instanceId(instanceId), m_password(password), m_sentinels(sentinels), m_masterName(masterName),
    m_useSentinel(true), m_ownershipTtlSecs(ownershipTtlSecs ? ownershipTtlSecs : 30) {
  }

  DialogStore::~DialogStore() {
    disconnect();
  }

  void DialogStore::disconnect() {
    if (m_ctx) {
      redisFree(m_ctx);
      m_ctx = nullptr;
    }
  }

  bool DialogStore::resolveMasterViaSentinels(string& host, unsigned int& port) {
    for (const auto& hp : allHostPorts(m_sentinels)) {
      redisContext* c = redisConnect(hp.first.c_str(), hp.second);
      if (!c || c->err) {
        if (c) redisFree(c);
        continue;
      }
      redisReply* r = (redisReply*) redisCommand(c, "SENTINEL get-master-addr-by-name %s", m_masterName.c_str());
      bool ok = false;
      if (r && r->type == REDIS_REPLY_ARRAY && r->elements == 2) {
        host = r->element[0]->str;
        port = (unsigned int) ::atoi(r->element[1]->str);
        ok = port != 0;
      }
      if (r) freeReplyObject(r);
      redisFree(c);
      if (ok) {
        DR_LOG(log_info) << "DialogStore - sentinel resolved master " << m_masterName << " to " << host << ":" << port;
        return true;
      }
    }
    DR_LOG(log_error) << "DialogStore - failed to resolve master via sentinels: " << m_sentinels;
    return false;
  }

  bool DialogStore::ensureConnected() {
    if (m_ctx && !m_ctx->err) return true;
    disconnect();

    string host = m_address;
    unsigned int port = m_port;
    if (m_useSentinel) {
      if (!resolveMasterViaSentinels(host, port)) return false;
    }

    struct timeval tv = { 1, 500000 }; // 1.5s connect timeout
    m_ctx = redisConnectWithTimeout(host.c_str(), port, tv);
    if (!m_ctx || m_ctx->err) {
      DR_LOG(log_error) << "DialogStore - error connecting to redis " << host << ":" << port
        << " : " << (m_ctx ? m_ctx->errstr : "alloc failed");
      disconnect();
      return false;
    }
    if (!m_password.empty()) {
      redisReply* r = (redisReply*) redisCommand(m_ctx, "AUTH %s", m_password.c_str());
      bool authFailed = (!r || r->type == REDIS_REPLY_ERROR);
      if (r) freeReplyObject(r);
      if (authFailed) {
        DR_LOG(log_error) << "DialogStore - redis AUTH failed";
        disconnect();
        return false;
      }
    }
    return true;
  }

  /* run a command from a vector of string args; returns reply (caller frees) or nullptr */
  static redisReply* runCmd(redisContext* ctx, const std::vector<string>& args) {
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    argv.reserve(args.size());
    argvlen.reserve(args.size());
    for (const auto& a : args) {
      argv.push_back(a.c_str());
      argvlen.push_back(a.size());
    }
    return (redisReply*) redisCommandArgv(ctx, (int) args.size(), argv.data(), argvlen.data());
  }

  bool DialogStore::saveDialog(const DialogState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (state.dialogId.empty() || !ensureConnected()) return false;

    std::vector<string> args = { "HSET", "ha:dialog:" + state.dialogId };
    for (const auto& kv : state.toFields()) {
      args.push_back(kv.first);
      args.push_back(kv.second);
    }
    redisReply* r = runCmd(m_ctx, args);
    bool ok = (r != nullptr && r->type != REDIS_REPLY_ERROR);
    if (r) freeReplyObject(r); else { disconnect(); return false; }

    r = runCmd(m_ctx, { "SADD", "ha:dialogs", state.dialogId });
    if (r) freeReplyObject(r);
    DR_LOG(log_debug) << "DialogStore::saveDialog - saved " << state.dialogId;
    return ok;
  }

  bool DialogStore::updateField(const string& dialogId, const string& field, const string& value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureConnected()) return false;
    redisReply* r = runCmd(m_ctx, { "HSET", "ha:dialog:" + dialogId, field, value });
    bool ok = (r != nullptr);
    if (r) freeReplyObject(r); else disconnect();
    return ok;
  }

  bool DialogStore::deleteDialog(const string& dialogId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureConnected()) return false;
    redisReply* r = runCmd(m_ctx, { "DEL", "ha:dialog:" + dialogId });
    if (r) freeReplyObject(r); else { disconnect(); return false; }
    r = runCmd(m_ctx, { "SREM", "ha:dialogs", dialogId });
    if (r) freeReplyObject(r);
    r = runCmd(m_ctx, { "DEL", "ha:owner:" + dialogId });
    if (r) freeReplyObject(r);
    DR_LOG(log_debug) << "DialogStore::deleteDialog - removed " << dialogId;
    return true;
  }

  bool DialogStore::getDialog(const string& dialogId, DialogState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureConnected()) return false;
    redisReply* r = runCmd(m_ctx, { "HGETALL", "ha:dialog:" + dialogId });
    if (!r) { disconnect(); return false; }
    bool ok = false;
    if (r->type == REDIS_REPLY_ARRAY && r->elements >= 2) {
      std::unordered_map<string, string> m;
      for (size_t i = 0; i + 1 < r->elements; i += 2) {
        m[r->element[i]->str] = r->element[i + 1]->str ? r->element[i + 1]->str : "";
      }
      state = DialogState::fromFields(m);
      ok = !state.dialogId.empty();
    }
    freeReplyObject(r);
    return ok;
  }

  bool DialogStore::getAllDialogs(std::vector<DialogState>& dialogs) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureConnected()) return false;
    redisReply* r = runCmd(m_ctx, { "SMEMBERS", "ha:dialogs" });
    if (!r) { disconnect(); return false; }
    std::vector<string> ids;
    if (r->type == REDIS_REPLY_ARRAY) {
      for (size_t i = 0; i < r->elements; i++) {
        if (r->element[i]->str) ids.push_back(r->element[i]->str);
      }
    }
    freeReplyObject(r);

    for (const auto& id : ids) {
      DialogState s;
      if (getDialog(id, s)) dialogs.push_back(s);
      else {
        /* stale index entry: dialog hash gone, clean it up */
        redisReply* r2 = runCmd(m_ctx, { "SREM", "ha:dialogs", id });
        if (r2) freeReplyObject(r2);
      }
    }
    DR_LOG(log_info) << "DialogStore::getAllDialogs - loaded " << dialogs.size() << " dialog(s) from redis";
    return true;
  }

  bool DialogStore::acquireOwnership(const string& dialogId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureConnected()) return false;
    const string key = "ha:owner:" + dialogId;
    /* SET key instanceId NX EX ttl  -> claim only if currently unowned */
    redisReply* r = runCmd(m_ctx, { "SET", key, m_instanceId, "NX", "EX", std::to_string(m_ownershipTtlSecs) });
    bool acquired = (r && r->type == REDIS_REPLY_STATUS && r->str && 0 == strcmp(r->str, "OK"));
    if (r) freeReplyObject(r); else { disconnect(); return false; }
    if (acquired) {
      DR_LOG(log_info) << "DialogStore::acquireOwnership - " << m_instanceId << " acquired " << dialogId;
      return true;
    }
    /* already owned - is it us? */
    string owner;
    if (getOwner(dialogId, owner) && owner == m_instanceId) {
      renewOwnership(dialogId);
      return true;
    }
    DR_LOG(log_debug) << "DialogStore::acquireOwnership - " << dialogId << " owned by " << owner;
    return false;
  }

  bool DialogStore::claimOwnership(const string& dialogId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureConnected()) return false;
    /* SET key instanceId EX ttl (NO NX) -> forcibly take ownership.
       Used only on --recover-on-start promotion: this node is being made active and must
       steal ownership from a (dead/fenced) prior owner whose key may not have expired yet. */
    redisReply* r = runCmd(m_ctx, { "SET", "ha:owner:" + dialogId, m_instanceId,
                                    "EX", std::to_string(m_ownershipTtlSecs) });
    bool ok = (r && r->type == REDIS_REPLY_STATUS && r->str && 0 == strcmp(r->str, "OK"));
    if (r) freeReplyObject(r); else { disconnect(); return false; }
    if (ok) DR_LOG(log_info) << "DialogStore::claimOwnership - " << m_instanceId << " claimed " << dialogId;
    return ok;
  }

  bool DialogStore::renewOwnership(const string& dialogId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureConnected()) return false;
    string owner;
    if (!getOwner(dialogId, owner) || owner != m_instanceId) {
      /* not (yet) owned by anyone -> take it; owned by someone else -> fail */
      if (owner.empty()) return acquireOwnership(dialogId);
      return false;
    }
    redisReply* r = runCmd(m_ctx, { "SET", "ha:owner:" + dialogId, m_instanceId, "EX", std::to_string(m_ownershipTtlSecs) });
    bool ok = (r != nullptr);
    if (r) freeReplyObject(r); else disconnect();
    return ok;
  }

  bool DialogStore::releaseOwnership(const string& dialogId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureConnected()) return false;
    string owner;
    if (getOwner(dialogId, owner) && owner != m_instanceId) return false;
    redisReply* r = runCmd(m_ctx, { "DEL", "ha:owner:" + dialogId });
    if (r) freeReplyObject(r); else disconnect();
    return true;
  }

  bool DialogStore::getOwner(const string& dialogId, string& owner) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    owner.clear();
    if (!ensureConnected()) return false;
    redisReply* r = runCmd(m_ctx, { "GET", "ha:owner:" + dialogId });
    if (!r) { disconnect(); return false; }
    bool ok = false;
    if (r->type == REDIS_REPLY_STRING && r->str) { owner = r->str; ok = true; }
    freeReplyObject(r);
    return ok;
  }

  bool DialogStore::isOwner(const string& dialogId) {
    string owner;
    return getOwner(dialogId, owner) && owner == m_instanceId;
  }

  bool DialogStore::touchRefresh(const string& dialogId) {
    return updateField(dialogId, "lastRefreshTs", std::to_string((long) time(nullptr)));
  }

  long DialogStore::getLastRefresh(const string& dialogId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureConnected()) return 0;
    redisReply* r = runCmd(m_ctx, { "HGET", "ha:dialog:" + dialogId, "lastRefreshTs" });
    long ts = 0;
    if (r) {
      if (r->type == REDIS_REPLY_STRING && r->str) ts = ::strtol(r->str, nullptr, 10);
      freeReplyObject(r);
    } else disconnect();
    return ts;
  }

}
