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
#ifndef __DIALOG_STATE_HPP__
#define __DIALOG_STATE_HPP__

#include <string>
#include <vector>
#include <utility>
#include <unordered_map>

namespace drachtio {

  /**
   * DialogState is the serializable, instance-independent snapshot of a SIP
   * dialog.  It deliberately contains NO Sofia-SIP pointers (nta_leg_t*, tport_t*,
   * nta_incoming_t*, ...): those are recreated on the instance that recovers the
   * dialog.  It is persisted to Redis as a hash so that any instance can rebuild
   * the SipDialog object and re-register the Sofia leg after a failover.
   */
  struct DialogState {
    // identity
    std::string dialogId;
    std::string callId;
    std::string transactionId;
    int         role = 0;            // 0 = we_are_uac, 1 = we_are_uas

    // tags
    std::string localTag;
    std::string remoteTag;

    // dialog addresses (for leg reconstruction on recovery)
    std::string fromUri;     // our AOR (local)
    std::string toUri;       // peer AOR (remote)
    std::string remoteContact;

    // sdp / content
    std::string localSdp;
    std::string remoteSdp;
    std::string localContentType;
    std::string remoteContentType;

    // contact / signaling
    std::string localContact;
    std::string localSignalingAddress;
    unsigned int localSignalingPort = 0;
    std::string remoteSignalingAddress;
    unsigned int remoteSignalingPort = 0;

    // transport
    std::string transportAddress;
    std::string transportPort;
    std::string protocol;

    // source (peer) network tuple
    std::string sourceAddress;
    unsigned int sourcePort = 0;

    // routing
    std::string routeUri;

    // status / sequencing
    unsigned int recentSipStatus = 0;
    long startTime = 0;
    long connectTime = 0;
    unsigned long seq = 0;

    // session timer (RFC 4028)
    unsigned long sessionExpiresSecs = 0;
    unsigned long minSE = 90;
    int           refresher = 0;     // 0 = none, 1 = we_are_refresher, 2 = they_are_refresher
    long          lastRefreshTs = 0; // unix time of last received/sent refresh

    // application binding (which named app owns the call logic)
    std::string appName;

    /* serialize to an ordered list of (field, value) pairs for HSET */
    std::vector<std::pair<std::string, std::string>> toFields() const;

    /* rebuild from a field->value map produced by HGETALL */
    static DialogState fromFields(const std::unordered_map<std::string, std::string>& m);
  };

}

#endif
