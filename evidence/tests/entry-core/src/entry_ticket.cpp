// evidence/tests/entry-core/src/entry_ticket.cpp
//
// 进入游戏 · 登录票据证据
//   1) SHA-256 / HMAC-SHA256 自实现（对 RFC 4231 与标准 SHA-256 向量校验）
//   2) 一次性登录票据：签发 / 验签 / 过期 / 时钟偏移 / nonce 重放 / 常量时间比较
//
// 构建：g++ -std=c++17 -O2 -o build/entry_ticket.exe src/entry_ticket.cpp
// 依赖：仅 C++17 标准库（不引入 OpenSSL，保证在任何机器上可复现）

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// 断言框架
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

static std::string Hex(const uint8_t* p, size_t n) {
    static const char* k = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(k[p[i] >> 4]);
        s.push_back(k[p[i] & 0x0F]);
    }
    return s;
}

static void Check(bool ok, const char* name, const std::string& detail = "") {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!detail.empty()) std::printf("        %s\n", detail.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

// ---------------------------------------------------------------------------
// SHA-256（FIPS 180-4）
// ---------------------------------------------------------------------------
struct Sha256 {
    uint32_t h[8];
    uint64_t len = 0;
    uint8_t buf[64];
    size_t bufLen = 0;

    Sha256() {
        h[0] = 0x6a09e667; h[1] = 0xbb67ae85; h[2] = 0x3c6ef372; h[3] = 0xa54ff53a;
        h[4] = 0x510e527f; h[5] = 0x9b05688c; h[6] = 0x1f83d9ab; h[7] = 0x5be0cd19;
    }

    static uint32_t Ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Block(const uint8_t* p) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
                   (uint32_t)p[i * 4 + 2] << 8 | (uint32_t)p[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const uint32_t S0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void Update(const uint8_t* p, size_t n) {
        len += n;
        while (n > 0) {
            if (bufLen == 0 && n >= 64) { Block(p); p += 64; n -= 64; continue; }
            const size_t take = std::min<size_t>(n, 64 - bufLen);
            std::memcpy(buf + bufLen, p, take);
            bufLen += take; p += take; n -= take;
            if (bufLen == 64) { Block(buf); bufLen = 0; }
        }
    }

    void Update(const std::string& s) { Update(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

    std::array<uint8_t, 32> Final() {
        const uint64_t bits = len * 8;
        const uint8_t one = 0x80;
        const uint8_t zero = 0x00;
        Update(&one, 1);
        while (bufLen != 56) Update(&zero, 1);
        uint8_t lb[8];
        for (int i = 0; i < 8; ++i) lb[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        Update(lb, 8);
        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4]     = static_cast<uint8_t>(h[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
        }
        return out;
    }
};

static std::string Sha256Hex(const std::string& in) {
    Sha256 s;
    s.Update(in);
    const auto d = s.Final();
    return Hex(d.data(), d.size());
}

// ---------------------------------------------------------------------------
// HMAC-SHA256（RFC 2104）
// ---------------------------------------------------------------------------
static std::array<uint8_t, 32> HmacSha256(const std::string& key, const std::string& msg) {
    uint8_t k[64];
    std::memset(k, 0, sizeof(k));
    if (key.size() > 64) {
        Sha256 s;
        s.Update(key);
        const auto d = s.Final();
        std::memcpy(k, d.data(), 32);
    } else {
        std::memcpy(k, key.data(), key.size());
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5C; }

    uint8_t inner[32];
    {
        Sha256 s;
        s.Update(ipad, 64);
        s.Update(reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
        const auto d = s.Final();
        std::memcpy(inner, d.data(), 32);
    }
    std::array<uint8_t, 32> out{};
    {
        Sha256 s;
        s.Update(opad, 64);
        s.Update(inner, 32);
        out = s.Final();
    }
    return out;
}

// 常量时间比较：不因"第几个字节不同"而提前返回，避免时序侧信道。
static bool ConstantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
    return diff == 0;
}

// ---------------------------------------------------------------------------
// 登录票据（One-Time Login Ticket）
//   payload = v1|player=<id>|server=<ds>|nonce=<hex>|iat=<unix>|exp=<unix>
//   ticket  = payload|sig=<hex hmac(key, payload)>
// ---------------------------------------------------------------------------
struct Ticket {
    std::string playerId;
    std::string serverId;
    std::string nonce;
    int64_t issuedAt = 0;
    int64_t expireAt = 0;
    std::string signature;
    std::string payload;   // 被签名的原文
};

struct TicketCodec {
    std::string key;
    int64_t ttlSeconds = 60;
    int64_t maxClockSkew = 5;      // 容忍客户端/网关时钟偏移
    std::unordered_set<std::string> usedNonces;   // 一次性消费表

    std::string Sign(const std::string& payload) const {
        const auto mac = HmacSha256(key, payload);
        return Hex(mac.data(), mac.size());
    }

    Ticket Issue(const std::string& playerId, const std::string& serverId,
                 const std::string& nonce, int64_t now) const {
        Ticket t;
        t.playerId = playerId;
        t.serverId = serverId;
        t.nonce = nonce;
        t.issuedAt = now;
        t.expireAt = now + ttlSeconds;
        t.payload = "v1|player=" + playerId + "|server=" + serverId + "|nonce=" + nonce +
                    "|iat=" + std::to_string(t.issuedAt) + "|exp=" + std::to_string(t.expireAt);
        t.signature = Sign(t.payload);
        return t;
    }

    std::string Serialize(const Ticket& t) const { return t.payload + "|sig=" + t.signature; }

    // 解析不做验签；验签单独一步，便于把"格式错误"和"签名错误"分开报告。
    bool Parse(const std::string& wire, Ticket* out) const {
        const auto sigPos = wire.rfind("|sig=");
        if (sigPos == std::string::npos) return false;
        const std::string payload = wire.substr(0, sigPos);
        const std::string sig = wire.substr(sigPos + 5);
        if (payload.rfind("v1|", 0) != 0) return false;

        auto field = [&](const std::string& name, std::string* dst) -> bool {
            const std::string pat = "|" + name + "=";
            auto p = payload.find(pat);
            if (p == std::string::npos) return false;
            p += pat.size();
            auto q = payload.find('|', p);
            *dst = payload.substr(p, q == std::string::npos ? std::string::npos : q - p);
            return !dst->empty();
        };
        std::string iat, exp;
        if (!field("player", &out->playerId)) return false;
        if (!field("server", &out->serverId)) return false;
        if (!field("nonce", &out->nonce)) return false;
        if (!field("iat", &iat)) return false;
        if (!field("exp", &exp)) return false;
        out->issuedAt = std::stoll(iat);
        out->expireAt = std::stoll(exp);
        out->payload = payload;
        out->signature = sig;
        return true;
    }

    enum class VerifyResult { kOk, kMalformed, kBadSignature, kExpired, kNotYetValid, kReplayed, kServerMismatch };

    static const char* Name(VerifyResult r) {
        switch (r) {
            case VerifyResult::kOk: return "ok";
            case VerifyResult::kMalformed: return "malformed";
            case VerifyResult::kBadSignature: return "bad_signature";
            case VerifyResult::kExpired: return "expired";
            case VerifyResult::kNotYetValid: return "not_yet_valid";
            case VerifyResult::kReplayed: return "replayed";
            case VerifyResult::kServerMismatch: return "server_mismatch";
        }
        return "?";
    }

    // expectedServer 为空表示不校验归属；校验顺序：格式 → 签名 → 时间窗 → 归属 → 一次性消费
    VerifyResult Verify(const std::string& wire, int64_t now,
                        const std::string& expectedServer, bool consume) {
        Ticket t;
        if (!Parse(wire, &t)) return VerifyResult::kMalformed;

        const std::string expect = Sign(t.payload);
        if (!ConstantTimeEqual(expect, t.signature)) return VerifyResult::kBadSignature;

        if (now + maxClockSkew < t.issuedAt) return VerifyResult::kNotYetValid;
        if (now - maxClockSkew > t.expireAt) return VerifyResult::kExpired;

        if (!expectedServer.empty() && t.serverId != expectedServer)
            return VerifyResult::kServerMismatch;

        if (usedNonces.count(t.nonce)) return VerifyResult::kReplayed;
        if (consume) usedNonces.insert(t.nonce);
        return VerifyResult::kOk;
    }
};

// ---------------------------------------------------------------------------
// 断言组
// ---------------------------------------------------------------------------
static void TestSha256() {
    const std::string empty = "";
    Check(Sha256Hex(empty) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "T1 SHA-256 over empty input matches FIPS 180-4 vector",
          Sha256Hex(empty));

    Check(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "T2 SHA-256(\"abc\") matches FIPS 180-4 vector",
          Sha256Hex("abc"));

    const std::string longMsg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Check(Sha256Hex(longMsg) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "T3 SHA-256 over a 56-byte message (padding boundary) matches vector",
          Sha256Hex(longMsg));

    // 跨块输入：分段喂入必须与一次喂入等价（流式实现最容易在这里出错）
    Sha256 a, b;
    a.Update(std::string(1000, 'x'));
    const std::string big(1000, 'x');
    for (size_t i = 0; i < big.size(); i += 7) b.Update(big.substr(i, 7));
    const auto da = a.Final(), db = b.Final();
    Check(Hex(da.data(), 32) == Hex(db.data(), 32),
          "T4 streaming SHA-256 is chunk-boundary independent (1000B in 7B chunks)",
          Hex(da.data(), 32));
}

static void TestHmac() {
    // RFC 4231 Test Case 1
    const std::string key1(20, '\x0b');
    Check(Hex(HmacSha256(key1, "Hi There").data(), 32) ==
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
          "T5 HMAC-SHA256 matches RFC 4231 Test Case 1 (20-byte 0x0b key)",
          Hex(HmacSha256(key1, "Hi There").data(), 32));

    // RFC 4231 Test Case 2
    Check(Hex(HmacSha256("Jefe", "what do ya want for nothing?").data(), 32) ==
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
          "T6 HMAC-SHA256 matches RFC 4231 Test Case 2 (\"Jefe\")",
          Hex(HmacSha256("Jefe", "what do ya want for nothing?").data(), 32));

    // RFC 4231 Test Case 3：key 与 data 均为 0xaa 重复
    const std::string key3(20, '\xaa');
    const std::string data3(50, '\xdd');
    Check(Hex(HmacSha256(key3, data3).data(), 32) ==
              "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
          "T7 HMAC-SHA256 matches RFC 4231 Test Case 3 (20B key / 50B data)",
          Hex(HmacSha256(key3, data3).data(), 32));

    // 密钥长于分块（>64B）时必须先哈希，这是最常见的实现缺陷
    const std::string longKey(131, '\xaa');
    const std::string data4 = "Test Using Larger Than Block-Size Key - Hash Key First";
    Check(Hex(HmacSha256(longKey, data4).data(), 32) ==
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
          "T8 HMAC-SHA256 hashes a >64-byte key first (RFC 4231 Test Case 6)",
          Hex(HmacSha256(longKey, data4).data(), 32));
}

static void TestTicket() {
    TicketCodec codec;
    codec.key = "server-side-secret-key";
    const int64_t t0 = 1700000000;

    const Ticket t = codec.Issue("10086", "ds-7", "9a3f1c", t0);
    const std::string wire = codec.Serialize(t);

    Check(codec.Verify(wire, t0, "", false) == TicketCodec::VerifyResult::kOk,
          "T9 a freshly issued ticket verifies",
          "wire=" + wire.substr(0, 60) + "...");

    // 篡改 payload（把玩家改成别人）必须被签名拦下
    std::string tampered = wire;
    tampered.replace(tampered.find("player=10086"), 12, "player=10087");
    Check(codec.Verify(tampered, t0, "", false) == TicketCodec::VerifyResult::kBadSignature,
          "T10 tampering player id in payload breaks verification",
          "swapped 10086 -> 10087");

    // 换密钥必须失败（防止跨环境/跨服务复用票据）
    TicketCodec other;
    other.key = "another-secret-key";
    Check(other.Verify(wire, t0, "", false) == TicketCodec::VerifyResult::kBadSignature,
          "T11 a ticket signed with another key is rejected");

    // 过期：TTL 60s，81 秒后（超过 5s 时钟容差）拒绝
    Check(codec.Verify(wire, t0 + 54, "", false) == TicketCodec::VerifyResult::kOk,
          "T12 ticket is still valid 54s later (inside 60s TTL)");
    Check(codec.Verify(wire, t0 + 81, "", false) == TicketCodec::VerifyResult::kExpired,
          "T13 ticket is rejected at +81s (TTL 60s, skew 5s)",
          "exp=" + std::to_string(t.expireAt));

    // 时钟漂移：客户端时间快 3 秒（在容差内）仍可用，快 30 秒判 not_yet_valid
    Ticket future = codec.Issue("10086", "ds-7", "aa01", t0 + 3);
    Check(codec.Verify(codec.Serialize(future), t0, "", false) == TicketCodec::VerifyResult::kOk,
          "T14 ticket issued 3s in the future is accepted within clock skew");
    Ticket farFuture = codec.Issue("10086", "ds-7", "aa02", t0 + 30);
    Check(codec.Verify(codec.Serialize(farFuture), t0, "", false) ==
              TicketCodec::VerifyResult::kNotYetValid,
          "T15 ticket issued 30s in the future is rejected (beyond skew)");

    // 归属校验：拿 A 服的票去 B 服进服必须拒绝
    Check(codec.Verify(wire, t0, "ds-8", false) == TicketCodec::VerifyResult::kServerMismatch,
          "T16 ticket for ds-7 is refused by ds-8");

    // 一次性消费：同一张票消费后重放被拒
    TicketCodec fresh;
    fresh.key = codec.key;
    Check(fresh.Verify(wire, t0, "ds-7", true) == TicketCodec::VerifyResult::kOk,
          "T17 first consumption of the ticket succeeds");
    Check(fresh.Verify(wire, t0, "ds-7", true) == TicketCodec::VerifyResult::kReplayed,
          "T18 replaying the same ticket is rejected (one-time use)");

    // 同一玩家换 nonce 重新签发应可用（正常重连路径）
    const Ticket second = fresh.Issue("10086", "ds-7", "bb02", t0 + 1);
    Check(fresh.Verify(fresh.Serialize(second), t0 + 1, "ds-7", true) ==
              TicketCodec::VerifyResult::kOk,
          "T19 a re-issued ticket with a new nonce is accepted (normal re-join)");

    Check(codec.Verify("garbage", t0, "", false) == TicketCodec::VerifyResult::kMalformed,
          "T20 a malformed string is rejected as malformed, not as bad signature");
}

static void TestConstantTime() {
    Check(ConstantTimeEqual("abcdef", "abcdef"), "T21 constant-time compare accepts equal strings");
    Check(!ConstantTimeEqual("abcdef", "abcdeg"),
          "T22 constant-time compare rejects a difference in the LAST byte",
          "early-return implementations usually pass this one too");
    Check(!ConstantTimeEqual("abcdef", "zbcdef"),
          "T23 constant-time compare rejects a difference in the FIRST byte");
    Check(!ConstantTimeEqual("abc", "abcdef"),
          "T24 constant-time compare rejects length mismatch");
}

int main() {
    std::printf("== entry_ticket: 进入游戏·登录票据证据 ==\n");
    std::printf("   SHA-256 / HMAC-SHA256 self-check vs RFC 4231 and FIPS 180-4\n\n");

    TestSha256();
    TestHmac();
    TestTicket();
    TestConstantTime();

    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
