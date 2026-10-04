// 进入游戏票据：单进程串行、完全合成凭据的教学模型。
// 自实现 SHA/HMAC 仅用于选定向量学习，不用于生产密钥或真实认证服务。
// 严格 C++17；运行入口及覆盖边界见 ../README.md。
#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <utility>
#include <limits>
#include <map>
#include <string_view>
#include <tuple>
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

// 教学用全字节异或比较；只测布尔功能，不证明优化后二进制的常量时间/侧信道性质。
static bool EqualBytesDemo(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
    return diff == 0;
}


// v1 固定字段/顺序；ID=[A-Za-z0-9_-]{1,64}，nonce=16..64个小写hex。
// iat/exp 是 [0, INT64_MAX] 规范十进制；sig=64小写hex；wire<=1024字节。
struct Ticket {
    std::string playerId, serverId, nonce;
    int64_t issuedAt = 0, expireAt = 0;
    std::string signature, payload;
    bool operator==(const Ticket& b) const {
        return std::tie(playerId, serverId, nonce, issuedAt, expireAt, signature, payload) ==
               std::tie(b.playerId, b.serverId, b.nonce, b.issuedAt, b.expireAt, b.signature, b.payload);
    }
};
struct TicketCodec {
    std::string key = "synthetic-classroom-key-never-deploy";
    int64_t ttlSeconds = 60, maxClockSkew = 5;
    static constexpr int64_t kMaxTtl = 300, kMaxSkew = 30;
    static constexpr size_t kMaxWire = 1024, kMaxConsumed = 1024;
    // 作用域是本codec签发域中的 (player, server, nonce)，并非全局一次性。
    // 本模型不清理；满表拒绝消费。生产需持久化原子消费/到期清理与恢复查询。
    std::map<std::tuple<std::string, std::string, std::string>, int64_t> usedNonces;
    enum class Result { kOk, kMalformed, kBadConfig, kBadSignature, kExpired,
                        kNotYetValid, kReplayed, kServerMismatch, kStateFull };
    static bool Id(std::string_view v) {
        return !v.empty() && v.size() <= 64 && std::all_of(v.begin(), v.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
    }
    static bool HexField(std::string_view v, size_t lo, size_t hi) {
        return v.size() >= lo && v.size() <= hi && std::all_of(v.begin(), v.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
    }
    static bool Decimal(std::string_view v, int64_t& out) {
        if (v.empty() || v.size() > 19 || (v.size() > 1 && v.front() == '0') ||
            !std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
        int64_t local = 0;
        const auto r = std::from_chars(v.data(), v.data() + v.size(), local, 10);
        if (r.ec != std::errc{} || r.ptr != v.data() + v.size()) return false;
        out = local;
        return true;
    }
    bool ConfigValid() const {
        return !key.empty() && key.size() <= 128 && ttlSeconds > 0 && ttlSeconds <= kMaxTtl &&
               maxClockSkew >= 0 && maxClockSkew <= kMaxSkew;
    }
    std::string Sign(const std::string& payload) const {
        const auto mac = HmacSha256(key, payload);
        return Hex(mac.data(), mac.size());
    }
    std::string Serialize(const Ticket& t) const { return t.payload + "|sig=" + t.signature; }
    Result Issue(const std::string& player, const std::string& server,
                 const std::string& nonce, int64_t now, Ticket* out) const {
        if (!ConfigValid()) return Result::kBadConfig;
        if (!out || !Id(player) || !Id(server) || !HexField(nonce, 16, 64) || now < 0 ||
            now > std::numeric_limits<int64_t>::max() - ttlSeconds) return Result::kMalformed;
        Ticket t;
        t.playerId = player; t.serverId = server; t.nonce = nonce;
        t.issuedAt = now; t.expireAt = now + ttlSeconds;
        t.payload = "v1|player=" + player + "|server=" + server + "|nonce=" + nonce +
                    "|iat=" + std::to_string(now) + "|exp=" + std::to_string(t.expireAt);
        t.signature = Sign(t.payload);
        *out = std::move(t);
        return Result::kOk;
    }
    bool Parse(const std::string& wire, Ticket* out) const {
        if (!out || wire.size() > kMaxWire) return false;
        std::array<std::string_view, 7> parts{};
        const std::string_view view(wire);
        size_t begin = 0;
        for (size_t i = 0; i < parts.size(); ++i) {
            const auto end = view.find('|', begin);
            if ((i + 1 == parts.size()) != (end == std::string_view::npos)) return false;
            parts[i] = view.substr(begin, end == std::string_view::npos ? view.size() - begin : end - begin);
            if (end != std::string_view::npos) begin = end + 1;
        }
        const std::array<std::string_view, 7> prefixes = {"v1", "player=", "server=", "nonce=", "iat=", "exp=", "sig="};
        if (parts[0] != "v1") return false;
        for (size_t i = 1; i < parts.size(); ++i) {
            if (parts[i].substr(0, prefixes[i].size()) != prefixes[i]) return false;
            parts[i].remove_prefix(prefixes[i].size());
        }
        Ticket t;
        if (!Id(parts[1]) || !Id(parts[2]) || !HexField(parts[3], 16, 64) ||
            !Decimal(parts[4], t.issuedAt) || !Decimal(parts[5], t.expireAt) ||
            !HexField(parts[6], 64, 64)) return false;
        t.playerId = std::string(parts[1]); t.serverId = std::string(parts[2]);
        t.nonce = std::string(parts[3]); t.signature = std::string(parts[6]);
        t.payload = wire.substr(0, wire.size() - 69); // "|sig=" + 64 hex
        *out = std::move(t); // 格式全部合法才发布输出；不吞掉bad_alloc冒充格式错误。
        return true;
    }
    Result Verify(const std::string& wire, int64_t now, const std::string& expectedServer,
                  bool consume, Ticket* out = nullptr) {
        if (!ConfigValid() || now < 0 || !Id(expectedServer)) return Result::kBadConfig;
        Ticket t;
        if (!Parse(wire, &t)) return Result::kMalformed;
        if (!EqualBytesDemo(Sign(t.payload), t.signature)) return Result::kBadSignature;
        if (t.expireAt <= t.issuedAt || t.expireAt - t.issuedAt > kMaxTtl) return Result::kMalformed;
        // 避免 now +/- skew 溢出：已知较大者减非负较小者。
        if (t.issuedAt > now && t.issuedAt - now > maxClockSkew) return Result::kNotYetValid;
        // 接受窗 [iat-skew, exp+skew)，边界由差值判断，无溢出加法。
        if (now >= t.expireAt && now - t.expireAt >= maxClockSkew) return Result::kExpired;
        if (t.serverId != expectedServer) return Result::kServerMismatch;
        const auto nonceKey = std::make_tuple(t.playerId, t.serverId, t.nonce);
        if (usedNonces.count(nonceKey)) return Result::kReplayed;
        if (consume && usedNonces.size() >= kMaxConsumed) return Result::kStateFull;
        if (consume) usedNonces.emplace(nonceKey, t.expireAt);
        if (out) *out = std::move(t);
        return Result::kOk;
    }
};

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

    // RFC 4231 Test Case 3：key为20个0xaa，data为50个0xdd
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


static Ticket Sentinel() { return {"unchanged", "sentinel", "not-a-ticket", 7, 8, "sig", "payload"}; }
static void TestTicket() {
    using R = TicketCodec::Result;
    TicketCodec c;
    Ticket t;
    Check(c.Issue("p1", "ds-1", "0123456789abcdef", 100, &t) == R::kOk,
          "ticket: legal issue follows the wire grammar");
    const auto wire = c.Serialize(t);
    for (int64_t now : {95, 100, 159, 160, 164})
        Check(c.Verify(wire, now, "ds-1", false) == R::kOk && c.usedNonces.empty(),
              ("ticket: accepted window now=" + std::to_string(now) + " without consumption").c_str());
    auto failure = [&](const std::string& input, R expected, int64_t now, const std::string& server,
                       const std::string& name) {
        const auto before = c.usedNonces;
        auto out = Sentinel(); const auto saved = out;
        bool noException = true; R actual = R::kOk;
        try { actual = c.Verify(input, now, server, true, &out); }
        catch (const std::exception&) { noException = false; }
        Check(noException && actual == expected && c.usedNonces == before && out == saved,
              ("reject + unchanged nonce/output: " + name).c_str());
    };
    failure(wire, R::kNotYetValid, 94, "ds-1", "one before not-before boundary");
    failure(wire, R::kExpired, 165, "ds-1", "exact expiry including skew");
    failure(wire, R::kExpired, 166, "ds-1", "after expiry");
    failure(wire, R::kServerMismatch, 100, "ds-2", "wrong audience");
    failure(wire, R::kBadConfig, -1, "ds-1", "negative now");
    failure(wire, R::kBadConfig, 100, "", "audience cannot be skipped");
    auto signedPayload = [&](const std::string& payload) { return payload + "|sig=" + c.Sign(payload); };
    auto payload = [](const std::string& iat, const std::string& exp) {
        return "v1|player=p1|server=ds-1|nonce=0123456789abcdef|iat=" + iat + "|exp=" + exp;
    };
    for (const auto& invalid : std::vector<std::string>{"", "not-a-number", "9223372036854775808",
            "999999999999999999999", "100junk", "+100", "-1", " 100", "100 ", "0100", "00"}) {
        failure(signedPayload(payload(invalid, "160")), R::kMalformed, 100, "ds-1", "iat '" + invalid + "'");
        failure(signedPayload(payload("100", invalid)), R::kMalformed, 100, "ds-1", "exp '" + invalid + "'");
    }
    for (const auto& bad : std::vector<std::string>{
            "v1|player=p1|player=p2|server=ds-1|nonce=0123456789abcdef|iat=100|exp=160",
            "v1|server=ds-1|player=p1|nonce=0123456789abcdef|iat=100|exp=160",
            "v1|player=p1|server=ds-1|iat=100|exp=160",
            "v1|player=p1|server=ds-1|nonce=0123456789abcdef|iat=100|exp=160|x=1",
            "v1|player=p=1|server=ds-1|nonce=0123456789abcdef|iat=100|exp=160",
            "v2|player=p1|server=ds-1|nonce=0123456789abcdef|iat=100|exp=160",
            payload("104", "96"), payload("100", "100"), payload("100", "401")})
        failure(signedPayload(bad), R::kMalformed, 100, "ds-1", "signed invalid grammar/interval " + bad);
    std::string embedded = t.payload; embedded[embedded.find("player=p1") + 7] = '\0';
    failure(signedPayload(embedded), R::kMalformed, 100, "ds-1", "embedded NUL");
    failure(std::string(1025, 'a'), R::kMalformed, 100, "ds-1", "wire exceeds bound");
    for (const auto& sig : std::vector<std::string>{"", std::string(63, 'a'), std::string(65, 'a'),
            std::string(64, 'A'), std::string(64, 'g')})
        failure(t.payload + "|sig=" + sig, R::kMalformed, 100, "ds-1", "signature encoding/length");
    std::string tampered = wire; tampered[tampered.find("player=p1") + 8] = '2';
    failure(tampered, R::kBadSignature, 100, "ds-1", "payload changed without MAC");
    TicketCodec wrong = c; wrong.key = "other-synthetic-key";
    auto out = Sentinel();
    Check(wrong.Verify(wire, 100, "ds-1", true, &out) == R::kBadSignature && wrong.usedNonces.empty() && out == Sentinel(),
          "reject wrong synthetic key without side effects");
    // 直接Parse失败也不能发布半成品。
    for (const auto& malformed : {std::string("garbage"), t.payload + "|sig=g"}) {
        out = Sentinel();
        Check(!c.Parse(malformed, &out) && out == Sentinel(), "parse output commits only after full grammar validation");
    }
    Check(c.Verify(wire, 100, "ds-1", true, &out) == R::kOk && out == t && c.usedNonces.size() == 1,
          "first consumption publishes authenticated fields");
    failure(wire, R::kReplayed, 100, "ds-1", "replay");
    Ticket second;
    c.Issue("p1", "ds-1", "fedcba9876543210", 101, &second);
    Check(c.Verify(c.Serialize(second), 101, "ds-1", true) == R::kOk, "new nonce supports explicit reissue");
    for (const std::string& badId : std::vector<std::string>{"", "p|server=x", "p=x", std::string("p\0x", 3), std::string(65, 'p')}) {
        out = Sentinel();
        Check(c.Issue(badId, "ds-1", "0123456789abcdef", 100, &out) == R::kMalformed && out == Sentinel(),
              "issue rejects invalid player without partial output");
        Check(c.Issue("p1", badId, "0123456789abcdef", 100, &out) == R::kMalformed && out == Sentinel(),
              "issue rejects invalid server without partial output");
    }
    for (const auto& nonce : {std::string(15, 'a'), std::string(65, 'a'), std::string(16, 'G'), std::string("a|b")}) {
        out = Sentinel();
        Check(c.Issue("p1", "ds-1", nonce, 100, &out) == R::kMalformed && out == Sentinel(), "issue enforces nonce grammar");
    }
    for (int64_t now : std::array<int64_t, 2>{-1, std::numeric_limits<int64_t>::max()}) {
        out = Sentinel();
        Check(c.Issue("p1", "ds-1", "0123456789abcdef", now, &out) == R::kMalformed && out == Sentinel(),
              "issue rejects invalid or overflowing clock input");
    }
    for (int64_t ttl : {-1, 0, 301}) {
        c.ttlSeconds = ttl; out = Sentinel();
        Check(c.Issue("p1", "ds-1", "0123456789abcdef", 100, &out) == R::kBadConfig && out == Sentinel(),
              "issue rejects out-of-policy TTL");
    }
    c.ttlSeconds = 60;
    for (int64_t skew : {-1, 31}) {
        c.maxClockSkew = skew; failure(wire, R::kBadConfig, 100, "ds-1", "invalid skew");
        out = Sentinel();
        Check(c.Issue("p1", "ds-1", "0123456789abcdef", 100, &out) == R::kBadConfig && out == Sentinel(),
              "issue also rejects invalid skew configuration");
    }
    c.maxClockSkew = 0;
    Ticket edge;
    const auto max = std::numeric_limits<int64_t>::max();
    Check(c.Issue("edge", "ds-1", "0123456789abcdef", max - 60, &edge) == R::kOk &&
          c.Verify(c.Serialize(edge), max - 1, "ds-1", false) == R::kOk &&
          c.Verify(c.Serialize(edge), max, "ds-1", false) == R::kExpired, "INT64_MAX expiry boundary without signed overflow");
    c.maxClockSkew = 5;
    Check(c.Verify(c.Serialize(edge), max, "ds-1", false) == R::kOk, "max time plus skew compared without addition");
    Ticket zero;
    Check(c.Issue("zero", "ds-1", "0123456789abcdef", 0, &zero) == R::kOk &&
          c.Verify(c.Serialize(zero), 0, "ds-1", false) == R::kOk, "canonical zero timestamp accepted");
    TicketCodec policyEdge; policyEdge.ttlSeconds = 300; policyEdge.maxClockSkew = 30;
    Ticket policyTicket;
    Check(policyEdge.Issue(std::string(64, 'p'), std::string(64, 's'), std::string(64, 'a'), 100, &policyTicket) == R::kOk &&
          policyEdge.Verify(policyEdge.Serialize(policyTicket), 70, std::string(64, 's'), false) == R::kOk &&
          policyEdge.Verify(policyEdge.Serialize(policyTicket), 429, std::string(64, 's'), false) == R::kOk &&
          policyEdge.Verify(policyEdge.Serialize(policyTicket), 430, std::string(64, 's'), false) == R::kExpired,
          "maximum legal identifier/nonce/TTL/skew bounds are accepted with exact expiry");
    TicketCodec full;
    for (size_t i = 0; i < TicketCodec::kMaxConsumed; ++i) full.usedNonces[{"fixture", "ds-1", std::to_string(i)}] = 1;
    out = Sentinel(); const auto before = full.usedNonces;
    Check(full.Verify(wire, 100, "ds-1", true, &out) == R::kStateFull && full.usedNonces == before && out == Sentinel(),
          "bounded consumption table fails closed without partial output");
}
static void TestCompare() {
    Check(EqualBytesDemo("abcdef", "abcdef"), "compare boolean: equal");
    Check(!EqualBytesDemo("abcdef", "zbcdef"), "compare boolean: first byte differs (not timing evidence)");
    Check(!EqualBytesDemo("abcdef", "abcdeg"), "compare boolean: last byte differs (not timing evidence)");
    Check(!EqualBytesDemo("abc", "abcdef"), "compare boolean: unequal length");
}
int main() {
    std::puts("entry_ticket: synthetic data; selected vectors + bounded protocol, no side-channel claim");
    TestSha256(); TestHmac(); TestTicket(); TestCompare();
    std::printf("RESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
