/**
 * Unit test for DialogState serialization (high-availability dialog replication).
 *
 * DialogState is what drachtio persists to redis and reloads on failover/recovery, so a
 * lossless toFields() -> fromFields() round-trip is what keeps a recovered call correct.
 * This test exercises that round-trip with no Sofia-SIP or redis dependency.
 */
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "dialog-state.hpp"

using namespace std;
using namespace drachtio;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK_EQ(actual, expected, what)                                        \
  do {                                                                          \
    if ((actual) == (expected)) { g_passed++; }                                 \
    else {                                                                      \
      g_failed++;                                                               \
      cerr << "FAIL: " << (what) << " expected [" << (expected)                 \
           << "] got [" << (actual) << "]" << endl;                            \
    }                                                                           \
  } while (0)

#define CHECK_TRUE(cond, what)                                                  \
  do {                                                                          \
    if (cond) { g_passed++; }                                                   \
    else { g_failed++; cerr << "FAIL: " << (what) << endl; }                    \
  } while (0)

/* serialize a DialogState and deserialize it back through the field map (as redis HGETALL would) */
static DialogState roundtrip(const DialogState& in) {
  unordered_map<string, string> m;
  for (const auto& kv : in.toFields()) m[kv.first] = kv.second;
  return DialogState::fromFields(m);
}

static void test_full_roundtrip() {
  DialogState s;
  s.dialogId = "callid-abc;from-tag=xyz";
  s.callId = "callid-abc";
  s.transactionId = "txn-123";
  s.role = 1; // we_are_uas
  s.localTag = "localtag-1";
  s.remoteTag = "remotetag-2";
  s.fromUri = "sip:alice@example.com";
  s.toUri = "sip:bob@example.org";
  s.remoteContact = "sip:bob@10.0.0.5:5060";
  s.localSdp = "v=0\r\no=- 1 1 IN IP4 1.1.1.1\r\ns=-\r\nc=IN IP4 1.1.1.1\r\nt=0 0\r\nm=audio 4000 RTP/AVP 0\r\n";
  s.remoteSdp = "v=0\r\no=- 2 2 IN IP4 2.2.2.2\r\n";
  s.localContentType = "application/sdp";
  s.remoteContentType = "application/sdp";
  s.localContact = "<sip:alice@1.1.1.1:5060>";
  s.localSignalingAddress = "1.1.1.1";
  s.localSignalingPort = 5060;
  s.remoteSignalingAddress = "2.2.2.2";
  s.remoteSignalingPort = 5070;
  s.transportAddress = "1.1.1.1";
  s.transportPort = "5060";
  s.protocol = "udp";
  s.sourceAddress = "2.2.2.2";
  s.sourcePort = 1234;
  s.routeUri = "sip:2.2.2.2:5070;transport=udp";
  s.recentSipStatus = 200;
  s.startTime = 1700000000;
  s.connectTime = 1700000005;
  s.seq = 42;
  s.sessionExpiresSecs = 1800;
  s.minSE = 120;
  s.refresher = 2; // they_are_refresher
  s.lastRefreshTs = 1700000123;
  s.appName = "myapp";

  DialogState o = roundtrip(s);

  CHECK_EQ(o.dialogId, s.dialogId, "dialogId");
  CHECK_EQ(o.callId, s.callId, "callId");
  CHECK_EQ(o.transactionId, s.transactionId, "transactionId");
  CHECK_EQ(o.role, s.role, "role");
  CHECK_EQ(o.localTag, s.localTag, "localTag");
  CHECK_EQ(o.remoteTag, s.remoteTag, "remoteTag");
  CHECK_EQ(o.fromUri, s.fromUri, "fromUri");
  CHECK_EQ(o.toUri, s.toUri, "toUri");
  CHECK_EQ(o.remoteContact, s.remoteContact, "remoteContact");
  CHECK_EQ(o.localSdp, s.localSdp, "localSdp (with CRLFs)");
  CHECK_EQ(o.remoteSdp, s.remoteSdp, "remoteSdp");
  CHECK_EQ(o.localContentType, s.localContentType, "localContentType");
  CHECK_EQ(o.remoteContentType, s.remoteContentType, "remoteContentType");
  CHECK_EQ(o.localContact, s.localContact, "localContact");
  CHECK_EQ(o.localSignalingAddress, s.localSignalingAddress, "localSignalingAddress");
  CHECK_EQ(o.localSignalingPort, s.localSignalingPort, "localSignalingPort");
  CHECK_EQ(o.remoteSignalingAddress, s.remoteSignalingAddress, "remoteSignalingAddress");
  CHECK_EQ(o.remoteSignalingPort, s.remoteSignalingPort, "remoteSignalingPort");
  CHECK_EQ(o.transportAddress, s.transportAddress, "transportAddress");
  CHECK_EQ(o.transportPort, s.transportPort, "transportPort");
  CHECK_EQ(o.protocol, s.protocol, "protocol");
  CHECK_EQ(o.sourceAddress, s.sourceAddress, "sourceAddress");
  CHECK_EQ(o.sourcePort, s.sourcePort, "sourcePort");
  CHECK_EQ(o.routeUri, s.routeUri, "routeUri");
  CHECK_EQ(o.recentSipStatus, s.recentSipStatus, "recentSipStatus");
  CHECK_EQ(o.startTime, s.startTime, "startTime");
  CHECK_EQ(o.connectTime, s.connectTime, "connectTime");
  CHECK_EQ(o.seq, s.seq, "seq");
  CHECK_EQ(o.sessionExpiresSecs, s.sessionExpiresSecs, "sessionExpiresSecs");
  CHECK_EQ(o.minSE, s.minSE, "minSE");
  CHECK_EQ(o.refresher, s.refresher, "refresher");
  CHECK_EQ(o.lastRefreshTs, s.lastRefreshTs, "lastRefreshTs");
  CHECK_EQ(o.appName, s.appName, "appName");
}

static void test_defaults_on_empty_map() {
  unordered_map<string, string> empty;
  DialogState o = DialogState::fromFields(empty);
  CHECK_TRUE(o.dialogId.empty(), "empty map -> empty dialogId");
  CHECK_EQ(o.role, 0, "empty map -> role default 0 (uac)");
  CHECK_EQ(o.minSE, (unsigned long) 90, "empty map -> minSE default 90");
  CHECK_EQ(o.refresher, 0, "empty map -> refresher default 0 (none)");
  CHECK_EQ(o.sourcePort, (unsigned int) 0, "empty map -> sourcePort default 0");
}

static void test_uac_role_and_empty_optionals() {
  DialogState s;
  s.dialogId = "x;from-tag=y";
  s.callId = "x";
  s.role = 0; // we_are_uac
  // leave tags / sdp / appName empty on purpose
  DialogState o = roundtrip(s);
  CHECK_EQ(o.dialogId, s.dialogId, "uac dialogId");
  CHECK_EQ(o.role, 0, "uac role preserved");
  CHECK_TRUE(o.localTag.empty(), "empty localTag preserved");
  CHECK_TRUE(o.remoteSdp.empty(), "empty remoteSdp preserved");
  CHECK_TRUE(o.appName.empty(), "empty appName preserved");
}

static void test_field_count_stable() {
  DialogState s;
  // toFields must emit a fixed set of keys regardless of content
  auto fields = s.toFields();
  CHECK_TRUE(fields.size() >= 33, "toFields emits all persisted keys");
}

int main() {
  cout << "Testing DialogState serialization round-trip" << endl;
  test_full_roundtrip();
  test_defaults_on_empty_map();
  test_uac_role_and_empty_optionals();
  test_field_count_stable();

  cout << "passed: " << g_passed << ", failed: " << g_failed << endl;
  return g_failed == 0 ? 0 : 1;
}
