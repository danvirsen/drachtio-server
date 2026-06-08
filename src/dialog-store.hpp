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
#ifndef __DIALOG_STORE_HPP__
#define __DIALOG_STORE_HPP__

#include <string>
#include <vector>
#include <mutex>

#include "dialog-state.hpp"

struct redisContext;

namespace drachtio {

  /**
   * DialogStore persists SIP dialog state to a shared Redis instance so that any
   * drachtio-server instance can recover a call after a restart or failover.
   *
   * Keys:
   *   ha:dialogs                SET    of all dialogIds (recovery enumeration)
   *   ha:dialog:{dialogId}      HASH   serialized DialogState
   *   ha:owner:{dialogId}       STRING owning instanceId, with TTL (heartbeat)
   *
   * All hiredis access is synchronous and guarded by a mutex; a single redis
   * context is shared and lazily (re)connected.  Calls are made from the Sofia
   * stack thread (dialog lifecycle / session timers) and, during recovery, from
   * the main thread before the event loop starts.
   */
  class DialogStore {
  public:
    // direct redis (host:port)
    DialogStore(const std::string& instanceId, const std::string& address, unsigned int port,
                const std::string& password, unsigned int ownershipTtlSecs = 30);
    // redis sentinel
    DialogStore(const std::string& instanceId, const std::string& sentinels, const std::string& masterName,
                const std::string& password, unsigned int ownershipTtlSecs, bool useSentinel);
    ~DialogStore();

    const std::string& instanceId() const { return m_instanceId; }
    unsigned int ownershipTtl() const { return m_ownershipTtlSecs; }

    /* dialog persistence */
    bool saveDialog(const DialogState& state);
    bool updateField(const std::string& dialogId, const std::string& field, const std::string& value);
    bool deleteDialog(const std::string& dialogId);
    bool getDialog(const std::string& dialogId, DialogState& state);
    bool getAllDialogs(std::vector<DialogState>& dialogs);

    /* ownership (single-writer guarantee) */
    bool acquireOwnership(const std::string& dialogId);          // claim if free or already ours
    bool claimOwnership(const std::string& dialogId);            // forcibly take over (recovery/promote)
    bool renewOwnership(const std::string& dialogId);            // refresh TTL if we own it
    bool releaseOwnership(const std::string& dialogId);          // give up ownership we hold
    bool isOwner(const std::string& dialogId);                   // do we currently own it?
    bool getOwner(const std::string& dialogId, std::string& owner);

    /* session-timer refresh propagation */
    bool touchRefresh(const std::string& dialogId);              // write lastRefreshTs = now
    long getLastRefresh(const std::string& dialogId);            // read lastRefreshTs (0 if none)

  private:
    bool ensureConnected();
    void disconnect();
    bool resolveMasterViaSentinels(std::string& host, unsigned int& port);

    std::recursive_mutex m_mutex;
    redisContext*  m_ctx = nullptr;

    std::string    m_instanceId;
    std::string    m_address;
    unsigned int   m_port = 6379;
    std::string    m_password;
    std::string    m_sentinels;
    std::string    m_masterName;
    bool           m_useSentinel = false;
    unsigned int   m_ownershipTtlSecs = 30;
  };

}

#endif
