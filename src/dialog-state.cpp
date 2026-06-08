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
#include <cstdlib>

#include "dialog-state.hpp"

namespace {
  std::string get(const std::unordered_map<std::string, std::string>& m, const char* k) {
    auto it = m.find(k);
    return it == m.end() ? std::string() : it->second;
  }
  long getl(const std::unordered_map<std::string, std::string>& m, const char* k, long def = 0) {
    auto it = m.find(k);
    return it == m.end() || it->second.empty() ? def : std::strtol(it->second.c_str(), nullptr, 10);
  }
}

namespace drachtio {

  std::vector<std::pair<std::string, std::string>> DialogState::toFields() const {
    return {
      {"dialogId", dialogId},
      {"callId", callId},
      {"transactionId", transactionId},
      {"role", std::to_string(role)},
      {"localTag", localTag},
      {"remoteTag", remoteTag},
      {"fromUri", fromUri},
      {"toUri", toUri},
      {"remoteContact", remoteContact},
      {"localSdp", localSdp},
      {"remoteSdp", remoteSdp},
      {"localContentType", localContentType},
      {"remoteContentType", remoteContentType},
      {"localContact", localContact},
      {"localSignalingAddress", localSignalingAddress},
      {"localSignalingPort", std::to_string(localSignalingPort)},
      {"remoteSignalingAddress", remoteSignalingAddress},
      {"remoteSignalingPort", std::to_string(remoteSignalingPort)},
      {"transportAddress", transportAddress},
      {"transportPort", transportPort},
      {"protocol", protocol},
      {"sourceAddress", sourceAddress},
      {"sourcePort", std::to_string(sourcePort)},
      {"routeUri", routeUri},
      {"recentSipStatus", std::to_string(recentSipStatus)},
      {"startTime", std::to_string(startTime)},
      {"connectTime", std::to_string(connectTime)},
      {"seq", std::to_string(seq)},
      {"sessionExpiresSecs", std::to_string(sessionExpiresSecs)},
      {"minSE", std::to_string(minSE)},
      {"refresher", std::to_string(refresher)},
      {"lastRefreshTs", std::to_string(lastRefreshTs)},
      {"appName", appName}
    };
  }

  DialogState DialogState::fromFields(const std::unordered_map<std::string, std::string>& m) {
    DialogState s;
    s.dialogId = get(m, "dialogId");
    s.callId = get(m, "callId");
    s.transactionId = get(m, "transactionId");
    s.role = (int) getl(m, "role");
    s.localTag = get(m, "localTag");
    s.remoteTag = get(m, "remoteTag");
    s.fromUri = get(m, "fromUri");
    s.toUri = get(m, "toUri");
    s.remoteContact = get(m, "remoteContact");
    s.localSdp = get(m, "localSdp");
    s.remoteSdp = get(m, "remoteSdp");
    s.localContentType = get(m, "localContentType");
    s.remoteContentType = get(m, "remoteContentType");
    s.localContact = get(m, "localContact");
    s.localSignalingAddress = get(m, "localSignalingAddress");
    s.localSignalingPort = (unsigned int) getl(m, "localSignalingPort");
    s.remoteSignalingAddress = get(m, "remoteSignalingAddress");
    s.remoteSignalingPort = (unsigned int) getl(m, "remoteSignalingPort");
    s.transportAddress = get(m, "transportAddress");
    s.transportPort = get(m, "transportPort");
    s.protocol = get(m, "protocol");
    s.sourceAddress = get(m, "sourceAddress");
    s.sourcePort = (unsigned int) getl(m, "sourcePort");
    s.routeUri = get(m, "routeUri");
    s.recentSipStatus = (unsigned int) getl(m, "recentSipStatus");
    s.startTime = getl(m, "startTime");
    s.connectTime = getl(m, "connectTime");
    s.seq = (unsigned long) getl(m, "seq");
    s.sessionExpiresSecs = (unsigned long) getl(m, "sessionExpiresSecs");
    s.minSE = (unsigned long) getl(m, "minSE", 90);
    s.refresher = (int) getl(m, "refresher");
    s.lastRefreshTs = getl(m, "lastRefreshTs");
    s.appName = get(m, "appName");
    return s;
  }

}
