// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Local IPC Server Implementation                  ║
// ║  Lightweight, high-performance Winsock HTTP/SSE daemon              ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ipc_server.h"
#include "runtime_config.h"
#include "runtime_state.h"
#include "state_engine.h"
#include "config_io.h"
#include "debug_logger.h"
#include "workspace.h"
#include "timing.h"
#include "bhop.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cctype>

namespace ipc {

static std::atomic<bool> s_running{false};
static std::thread       s_serverThread;
static std::thread       s_sseThread;
static HWND              s_msgHwnd = nullptr;
static std::string       s_authToken;
static SOCKET            s_listenSocket = INVALID_SOCKET;

static std::mutex        s_sseClientsMutex;
static std::vector<SOCKET> s_sseClients;
static std::atomic<bool> s_stateChanged{false};

// ── Token Generation & Storage ──
static std::string GenerateToken() {
    char hex[33] = {0};
    HCRYPTPROV hProv = 0;
    if (CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        uint8_t bytes[16];
        CryptGenRandom(hProv, sizeof(bytes), bytes);
        CryptReleaseContext(hProv, 0);
        for (int i = 0; i < 16; ++i) {
            sprintf_s(hex + i * 2, 3, "%02x", bytes[i]);
        }
    } else {
        // Fallback pseudorandom
        for (int i = 0; i < 32; ++i) {
            sprintf_s(hex + i, 2, "%x", rand() % 16);
        }
    }
    return std::string(hex);
}

static void SaveTokenFile(const std::string& token) {
    std::string tokenPath = workspace::GetProjectRootA() + "marco.token";
    std::ofstream ofs(tokenPath, std::ios::trunc);
    if (ofs.is_open()) {
        ofs << token;
        ofs.close();
    }
}

static void RemoveTokenFile() {
    std::string tokenPath = workspace::GetProjectRootA() + "marco.token";
    DeleteFileA(tokenPath.c_str());
}

// ── JSON Helpers ──
static std::string EscapeJson(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\b') o << "\\b";
        else if (c == '\f') o << "\\f";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') o << "\\r";
        else if (c == '\t') o << "\\t";
        else if (static_cast<unsigned char>(c) <= 0x1f) {
            char buf[8];
            sprintf_s(buf, sizeof(buf), "\\u%04x", c);
            o << buf;
        } else {
            o << c;
        }
    }
    return o.str();
}

static bool ExtractBool(const std::string& json, const std::string& key, bool defaultVal) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;
    size_t vpos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (vpos == std::string::npos) return defaultVal;
    if (json.compare(vpos, 4, "true") == 0) return true;
    if (json.compare(vpos, 5, "false") == 0) return false;
    return defaultVal;
}

static int ExtractInt(const std::string& json, const std::string& key, int defaultVal) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;
    size_t vpos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (vpos == std::string::npos) return defaultVal;
    try {
        return std::stoi(json.substr(vpos));
    } catch (...) {
        return defaultVal;
    }
}

static int64_t ExtractInt64(const std::string& json, const std::string& key, int64_t defaultVal) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;
    size_t vpos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (vpos == std::string::npos) return defaultVal;
    try {
        return std::stoll(json.substr(vpos));
    } catch (...) {
        return defaultVal;
    }
}

static double ExtractDouble(const std::string& json, const std::string& key, double defaultVal) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;
    size_t vpos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (vpos == std::string::npos) return defaultVal;
    try {
        return std::stod(json.substr(vpos));
    } catch (...) {
        return defaultVal;
    }
}

// ── Serialization ──
static std::string SerializeConfig(const RuntimeConfig& cfg) {
    std::ostringstream ss;
    ss << "{\n"
       << "  \"quickTapMs\": " << cfg.quickTapMs << ",\n"
       << "  \"maxScaleMs\": " << cfg.maxScaleMs << ",\n"
       << "  \"crouchMult\": " << cfg.crouchMult << ",\n"
       << "  \"latencyMarginMs\": " << cfg.latencyMarginMs << ",\n"
       << "  \"minStopMs\": " << cfg.minStopMs << ",\n"
       << "  \"lutMaxMs\": " << cfg.lutMaxMs << ",\n"
       << "  \"walkMemoryMs\": " << cfg.walkMemoryMs << ",\n"
       << "  \"walkRatioSkip\": " << cfg.walkRatioSkip << ",\n"
       << "  \"walkRatioLight\": " << cfg.walkRatioLight << ",\n"
       << "  \"minWalkStopMs\": " << cfg.minWalkStopMs << ",\n"
       << "  \"walkMaxStopMs\": " << cfg.walkMaxStopMs << ",\n"
       << "  \"decayK\": " << cfg.decayK << ",\n"
       << "  \"dirChangePenaltyMs\": " << cfg.dirChangePenaltyMs << ",\n"
       << "  \"tapSpamWindowMs\": " << cfg.tapSpamWindowMs << ",\n"
       << "  \"stopStrengthMin\": " << cfg.stopStrengthMin << ",\n"
       << "  \"tapSpamAlpha\": " << cfg.tapSpamAlpha << ",\n"
       << "  \"tapSpamHalfLifeMs\": " << cfg.tapSpamHalfLifeMs << ",\n"
       << "  \"minTapUs\": " << cfg.minTapUs << ",\n"
       << "  \"physMaxSpeed\": " << cfg.physMaxSpeed << ",\n"
       << "  \"physFriction\": " << cfg.physFriction << ",\n"
       << "  \"physStopSpeed\": " << cfg.physStopSpeed << ",\n"
       << "  \"physAccelerate\": " << cfg.physAccelerate << ",\n"
       << "  \"hardwareDebounceUs\": " << cfg.hardwareDebounceUs << ",\n"
       << "  \"humanizeMinUs\": " << cfg.humanizeMinUs << ",\n"
       << "  \"humanizeMaxUs\": " << cfg.humanizeMaxUs << ",\n"
       << "  \"activeBrakeProfileIndex\": " << cfg.activeBrakeProfileIndex << ",\n"
       << "  \"safeModeEnabled\": " << (cfg.safeModeEnabled ? "true" : "false") << ",\n"
       << "  \"bhopEnabled\": " << (cfg.bhopEnabled ? "true" : "false") << ",\n"
       << "  \"bhopMode\": " << cfg.bhopMode << ",\n"
       << "  \"airborneDelayMs\": " << cfg.airborneDelayMs << ",\n"
       << "  \"scrollBurstGapMs\": " << cfg.scrollBurstGapMs << ",\n"
       << "  \"landingScanMs\": " << cfg.landingScanMs << ",\n"
       << "  \"airborneLockMs\": " << cfg.airborneLockMs << ",\n"
       << "  \"spamIntervalMs\": " << cfg.spamIntervalMs << ",\n"
       << "  \"brakeProfiles\": [\n";

    for (int i = 0; i < 5; ++i) {
        const auto& p = cfg.brakeProfiles[i];
        ss << "    {\n"
           << "      \"overlapDurationUs\": " << p.overlap_duration_us << ",\n"
           << "      \"brakeBiasMultiplier\": " << p.brake_bias_multiplier << ",\n"
           << "      \"authorityBiasMs\": " << p.authority_bias_ms << ",\n"
           << "      \"aggressivenessCurve\": " << p.aggressiveness_curve << ",\n"
           << "      \"momentumMemoryMs\": " << p.momentum_memory_ms << ",\n"
           << "      \"accuracyThreshold\": " << p.accuracyThreshold << "\n"
           << "    }" << (i < 4 ? ",\n" : "\n");
    }
    ss << "  ]\n}";
    return ss.str();
}

static std::string SerializeTelemetry(const RuntimeSnapshot& snap) {
    std::ostringstream ss;
    char targetNameUtf8[64] = {0};
    WideCharToMultiByte(CP_UTF8, 0, snap.targetName, -1, targetNameUtf8, sizeof(targetNameUtf8), nullptr, nullptr);
    char profileNameUtf8[64] = {0};
    WideCharToMultiByte(CP_UTF8, 0, snap.activeBrakeProfileName, -1, profileNameUtf8, sizeof(profileNameUtf8), nullptr, nullptr);

    ss << "{\n"
       << "  \"runtimeState\": " << static_cast<int>(snap.runtimeState) << ",\n"
       << "  \"suspended\": " << (snap.suspended ? "true" : "false") << ",\n"
       << "  \"axisStateX\": " << static_cast<int>(snap.axisState[0]) << ",\n"
       << "  \"axisStateY\": " << static_cast<int>(snap.axisState[1]) << ",\n"
       << "  \"keys\": {\n"
       << "    \"phys\": [" << snap.phys[0] << "," << snap.phys[1] << "," << snap.phys[2] << "," << snap.phys[3] << "],\n"
       << "    \"logical\": [" << snap.logical[0] << "," << snap.logical[1] << "," << snap.logical[2] << "," << snap.logical[3] << "]\n"
       << "  },\n"
       << "  \"bhop\": {\n"
       << "    \"enabled\": " << (snap.bhopEnabled ? "true" : "false") << ",\n"
       << "    \"mode\": " << static_cast<int>(snap.bhopMode) << ",\n"
       << "    \"state\": " << static_cast<int>(snap.bhopState) << "\n"
       << "  },\n"
       << "  \"hookInstalled\": " << (snap.hookInstalled ? "true" : "false") << ",\n"
       << "  \"targetActive\": " << (snap.targetActive ? "true" : "false") << ",\n"
       << "  \"targetName\": \"" << EscapeJson(targetNameUtf8) << "\",\n"
       << "  \"uptimeMs\": " << snap.uptimeMs << ",\n"
       << "  \"timingActive\": " << (snap.timingActive ? "true" : "false") << ",\n"
       << "  \"metrics\": {\n"
       << "    \"hookLatencyP50Us\": " << snap.hookLatencyP50Us << ",\n"
       << "    \"hookLatencyP99Us\": " << snap.hookLatencyP99Us << ",\n"
       << "    \"timerJitterUs\": " << snap.timerJitterUs << ",\n"
       << "    \"wakeOversleepUs\": " << snap.wakeOversleepUs << ",\n"
       << "    \"spinDurationUs\": " << snap.spinDurationUs << ",\n"
       << "    \"stateMutationLatencyUs\": " << snap.stateMutationLatencyUs << ",\n"
       << "    \"schedulerSpikes\": " << snap.schedulerSpikeCount << ",\n"
       << "    \"coreMigrations\": " << snap.coreMigrationCount << ",\n"
       << "    \"wakeVarianceUs\": " << snap.wakeVarianceUs << ",\n"
       << "    \"timerOversleepPeakUs\": " << snap.timerOversleepPeakUs << "\n"
       << "  },\n"
       << "  \"profile\": {\n"
       << "    \"name\": \"" << EscapeJson(profileNameUtf8) << "\",\n"
       << "    \"overlapUs\": " << snap.profileOverlapUs << ",\n"
       << "    \"brakeBias\": " << snap.profileBrakeBias << ",\n"
       << "    \"authorityBiasMs\": " << snap.profileAuthorityBiasMs << ",\n"
       << "    \"aggrCurve\": " << snap.profileAggrCurve << ",\n"
       << "    \"accuracyThreshold\": " << snap.profileAccuracyThreshold << "\n"
       << "  },\n"
       << "  \"affinity\": {\n"
       << "    \"timingCore\": " << snap.activeTimingCore << ",\n"
       << "    \"hookCore\": " << snap.activeHookCore << ",\n"
       << "    \"smtCollision\": " << (snap.smtCollision ? "true" : "false") << "\n"
       << "  },\n"
       << "  \"timeline\": {\n"
       << "    \"index\": " << snap.timelineIndex << ",\n"
       << "    \"jitter\": [";
    for (int i = 0; i < RuntimeSnapshot::TIMELINE_SIZE; ++i) {
        ss << snap.timelineJitter[i] << (i < RuntimeSnapshot::TIMELINE_SIZE - 1 ? "," : "");
    }
    ss << "],\n    \"oversleep\": [";
    for (int i = 0; i < RuntimeSnapshot::TIMELINE_SIZE; ++i) {
        ss << snap.timelineOversleep[i] << (i < RuntimeSnapshot::TIMELINE_SIZE - 1 ? "," : "");
    }
    ss << "]\n  }\n}";
    return ss.str();
}

static void UpdateConfigFromJson(RuntimeConfig& cfg, const std::string& body) {
    cfg.quickTapMs = ExtractInt(body, "quickTapMs", cfg.quickTapMs);
    cfg.maxScaleMs = ExtractInt(body, "maxScaleMs", cfg.maxScaleMs);
    cfg.crouchMult = ExtractDouble(body, "crouchMult", cfg.crouchMult);
    cfg.latencyMarginMs = ExtractInt(body, "latencyMarginMs", cfg.latencyMarginMs);
    cfg.minStopMs = ExtractInt(body, "minStopMs", cfg.minStopMs);
    cfg.lutMaxMs = ExtractInt(body, "lutMaxMs", cfg.lutMaxMs);
    cfg.walkMemoryMs = ExtractInt(body, "walkMemoryMs", cfg.walkMemoryMs);
    cfg.walkRatioSkip = ExtractDouble(body, "walkRatioSkip", cfg.walkRatioSkip);
    cfg.walkRatioLight = ExtractDouble(body, "walkRatioLight", cfg.walkRatioLight);
    cfg.minWalkStopMs = ExtractInt(body, "minWalkStopMs", cfg.minWalkStopMs);
    cfg.walkMaxStopMs = ExtractInt(body, "walkMaxStopMs", cfg.walkMaxStopMs);
    cfg.decayK = ExtractDouble(body, "decayK", cfg.decayK);
    cfg.dirChangePenaltyMs = ExtractInt(body, "dirChangePenaltyMs", cfg.dirChangePenaltyMs);
    cfg.tapSpamWindowMs = ExtractInt(body, "tapSpamWindowMs", cfg.tapSpamWindowMs);
    cfg.stopStrengthMin = ExtractDouble(body, "stopStrengthMin", cfg.stopStrengthMin);
    cfg.tapSpamAlpha = ExtractDouble(body, "tapSpamAlpha", cfg.tapSpamAlpha);
    cfg.tapSpamHalfLifeMs = ExtractInt(body, "tapSpamHalfLifeMs", cfg.tapSpamHalfLifeMs);
    cfg.minTapUs = ExtractInt64(body, "minTapUs", cfg.minTapUs);
    cfg.physMaxSpeed = ExtractDouble(body, "physMaxSpeed", cfg.physMaxSpeed);
    cfg.physFriction = ExtractDouble(body, "physFriction", cfg.physFriction);
    cfg.physStopSpeed = ExtractDouble(body, "physStopSpeed", cfg.physStopSpeed);
    cfg.physAccelerate = ExtractDouble(body, "physAccelerate", cfg.physAccelerate);
    cfg.hardwareDebounceUs = ExtractInt(body, "hardwareDebounceUs", cfg.hardwareDebounceUs);
    cfg.humanizeMinUs = ExtractInt(body, "humanizeMinUs", cfg.humanizeMinUs);
    cfg.humanizeMaxUs = ExtractInt(body, "humanizeMaxUs", cfg.humanizeMaxUs);
    cfg.activeBrakeProfileIndex = ExtractInt(body, "activeBrakeProfileIndex", cfg.activeBrakeProfileIndex);
    cfg.safeModeEnabled = ExtractBool(body, "safeModeEnabled", cfg.safeModeEnabled);
    cfg.bhopEnabled = ExtractBool(body, "bhopEnabled", cfg.bhopEnabled);
    cfg.bhopMode = ExtractInt(body, "bhopMode", cfg.bhopMode);
    cfg.airborneDelayMs = ExtractInt(body, "airborneDelayMs", cfg.airborneDelayMs);
    cfg.scrollBurstGapMs = ExtractInt(body, "scrollBurstGapMs", cfg.scrollBurstGapMs);
    cfg.landingScanMs = ExtractInt(body, "landingScanMs", cfg.landingScanMs);
    cfg.airborneLockMs = ExtractInt(body, "airborneLockMs", cfg.airborneLockMs);
    cfg.spamIntervalMs = ExtractInt(body, "spamIntervalMs", cfg.spamIntervalMs);

    // Profile updates if provided
    for (int i = 1; i <= 4; ++i) {
        std::string pKey = "\"profile_" + std::to_string(i) + "\"";
        size_t pPos = body.find(pKey);
        if (pPos != std::string::npos) {
            size_t endPos = body.find('}', pPos);
            std::string sub = body.substr(pPos, endPos - pPos + 1);
            auto& prof = cfg.brakeProfiles[i];
            prof.overlap_duration_us = ExtractInt64(sub, "overlapDurationUs", prof.overlap_duration_us);
            prof.brake_bias_multiplier = ExtractDouble(sub, "brakeBiasMultiplier", prof.brake_bias_multiplier);
            prof.authority_bias_ms = ExtractDouble(sub, "authorityBiasMs", prof.authority_bias_ms);
            prof.aggressiveness_curve = ExtractDouble(sub, "aggressivenessCurve", prof.aggressiveness_curve);
            prof.momentum_memory_ms = ExtractDouble(sub, "momentumMemoryMs", prof.momentum_memory_ms);
            prof.accuracyThreshold = ExtractDouble(sub, "accuracyThreshold", prof.accuracyThreshold);
        }
    }
}

// ── HTTP Protocol Handling ──
static void SendResponse(SOCKET s, int statusCode, const std::string& statusText,
                         const std::string& contentType, const std::string& body) {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
       << "Content-Type: " << contentType << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type, X-Marco-Token, Authorization\r\n\r\n"
       << body;
    std::string resp = ss.str();
    send(s, resp.data(), static_cast<int>(resp.size()), 0);
}

static void SendCorsHeaders(SOCKET s) {
    std::string resp =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, X-Marco-Token, Authorization\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Content-Length: 0\r\n\r\n";
    send(s, resp.data(), static_cast<int>(resp.size()), 0);
}

static bool CheckAuth(const std::string& req, const std::string& path) {
    // Static web files don't require auth token to initially fetch
    if (path == "/" || path == "/index.html" || path.rfind("/assets/", 0) == 0 ||
        path.rfind("/vite.svg", 0) == 0 || path.rfind("/favicon", 0) == 0) {
        return true;
    }
    // Check query param ?token=
    size_t qPos = path.find("token=");
    if (qPos != std::string::npos) {
        std::string tokenInQuery = path.substr(qPos + 6);
        size_t ampPos = tokenInQuery.find('&');
        if (ampPos != std::string::npos) tokenInQuery = tokenInQuery.substr(0, ampPos);
        if (tokenInQuery == s_authToken) return true;
    }
    // Check X-Marco-Token header
    size_t hPos = req.find("X-Marco-Token: ");
    if (hPos != std::string::npos) {
        size_t endLine = req.find("\r\n", hPos);
        std::string tokenInHeader = req.substr(hPos + 15, endLine - (hPos + 15));
        while (!tokenInHeader.empty() && isspace(tokenInHeader.back())) tokenInHeader.pop_back();
        if (tokenInHeader == s_authToken) return true;
    }
    // Check Authorization: Bearer
    size_t aPos = req.find("Authorization: Bearer ");
    if (aPos != std::string::npos) {
        size_t endLine = req.find("\r\n", aPos);
        std::string tokenInAuth = req.substr(aPos + 22, endLine - (aPos + 22));
        while (!tokenInAuth.empty() && isspace(tokenInAuth.back())) tokenInAuth.pop_back();
        if (tokenInAuth == s_authToken) return true;
    }
    return false;
}

static void ServeStaticFile(SOCKET s, const std::string& path) {
    std::string relPath = path;
    size_t qmark = relPath.find('?');
    if (qmark != std::string::npos) relPath = relPath.substr(0, qmark);
    if (relPath == "/" || relPath.empty()) relPath = "/index.html";

    std::string fullPath = workspace::GetProjectRootA() + "ui/dist" + relPath;
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        // SPA Fallback to /index.html
        fullPath = workspace::GetProjectRootA() + "ui/dist/index.html";
        file.open(fullPath, std::ios::binary);
    }

    if (!file.is_open()) {
        SendResponse(s, 404, "Not Found", "application/json", "{\"error\":\"Not Found\"}");
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string mime = "text/plain";
    if (fullPath.ends_with(".html")) mime = "text/html";
    else if (fullPath.ends_with(".js")) mime = "application/javascript";
    else if (fullPath.ends_with(".css")) mime = "text/css";
    else if (fullPath.ends_with(".svg")) mime = "image/svg+xml";
    else if (fullPath.ends_with(".json")) mime = "application/json";

    SendResponse(s, 200, "OK", mime, content);
}

static void HandleClient(SOCKET clientSock) {
    char buf[8192] = {0};
    int received = recv(clientSock, buf, sizeof(buf) - 1, 0);
    if (received <= 0) {
        closesocket(clientSock);
        return;
    }
    std::string req(buf, received);
    std::istringstream stream(req);
    std::string method, rawPath, protocol;
    stream >> method >> rawPath >> protocol;

    if (method == "OPTIONS") {
        SendCorsHeaders(clientSock);
        closesocket(clientSock);
        return;
    }

    std::string cleanPath = rawPath;
    size_t qPos = cleanPath.find('?');
    std::string pathOnly = (qPos != std::string::npos) ? cleanPath.substr(0, qPos) : cleanPath;

    if (!CheckAuth(req, cleanPath)) {
        SendResponse(clientSock, 401, "Unauthorized", "application/json",
                     "{\"error\":\"Unauthorized. Provide valid X-Marco-Token or ?token=\"}");
        closesocket(clientSock);
        return;
    }

    // Extract body
    std::string body;
    size_t bodyPos = req.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        body = req.substr(bodyPos + 4);
    }

    if (method == "GET" && pathOnly == "/api/config") {
        RuntimeConfig cfg = rcfg::GetUserConfig();
        SendResponse(clientSock, 200, "OK", "application/json", SerializeConfig(cfg));
        closesocket(clientSock);
    } else if (method == "POST" && pathOnly == "/api/config") {
        RuntimeConfig cfg = rcfg::GetUserConfig();
        UpdateConfigFromJson(cfg, body);
        rcfg::Apply(cfg);
        config_io::Save(cfg);
        SendResponse(clientSock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        closesocket(clientSock);
    } else if (method == "GET" && pathOnly == "/api/telemetry") {
        RuntimeSnapshot snap{};
        engine::TakeSnapshot(snap);
        SendResponse(clientSock, 200, "OK", "application/json", SerializeTelemetry(snap));
        closesocket(clientSock);
    } else if (method == "GET" && (pathOnly == "/api/events" || pathOnly == "/api/sse")) {
        // Upgrade to SSE stream
        std::string sseHeader =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n";
        send(clientSock, sseHeader.data(), static_cast<int>(sseHeader.size()), 0);

        // Send initial state immediately
        RuntimeSnapshot initialSnap{};
        engine::TakeSnapshot(initialSnap);
        std::string initialData = "event: telemetry\ndata: " + SerializeTelemetry(initialSnap) + "\n\n";
        send(clientSock, initialData.data(), static_cast<int>(initialData.size()), 0);

        {
            std::lock_guard<std::mutex> lock(s_sseClientsMutex);
            s_sseClients.push_back(clientSock);
        }
        // Do not close socket; kept open for SSE worker
    } else if (method == "POST" && pathOnly == "/api/safemode") {
        bool enable = ExtractBool(body, "enabled", false);
        rcfg::SetSafeMode(enable);
        std::string resp = std::string("{\"status\":\"ok\",\"safeMode\":") + (enable ? "true" : "false") + "}";
        SendResponse(clientSock, 200, "OK", "application/json", resp);
        closesocket(clientSock);
    } else if (method == "POST" && pathOnly == "/api/revert") {
        bool reverted = rcfg::RevertToSnapshot();
        std::string resp = std::string("{\"status\":\"ok\",\"reverted\":") + (reverted ? "true" : "false") + "}";
        SendResponse(clientSock, 200, "OK", "application/json", resp);
        closesocket(clientSock);
    } else if (method == "POST" && pathOnly == "/api/profile") {
        int idx = ExtractInt(body, "index", 1);
        if (idx >= 1 && idx <= 4) {
            RuntimeConfig cfg = rcfg::GetMutable();
            cfg.activeBrakeProfileIndex = idx;
            rcfg::Apply(cfg);
            config_io::Save(cfg);
            SendResponse(clientSock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        } else {
            SendResponse(clientSock, 400, "Bad Request", "application/json", "{\"error\":\"Invalid profile index\"}");
        }
        closesocket(clientSock);
    } else if (method == "POST" && pathOnly == "/api/suspend") {
        engine::ToggleSuspend();
        std::string resp = std::string("{\"status\":\"ok\",\"suspended\":") + (engine::IsSuspended() ? "true" : "false") + "}";
        SendResponse(clientSock, 200, "OK", "application/json", resp);
        closesocket(clientSock);
    } else if (method == "POST" && pathOnly == "/api/bhop") {
        RuntimeConfig cfg = rcfg::GetMutable();
        cfg.bhopEnabled = ExtractBool(body, "enabled", cfg.bhopEnabled);
        cfg.bhopMode = ExtractInt(body, "mode", cfg.bhopMode);
        rcfg::Apply(cfg);
        config_io::Save(cfg);
        SendResponse(clientSock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        closesocket(clientSock);
    } else if (method == "POST" && pathOnly == "/api/quit") {
        SendResponse(clientSock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        closesocket(clientSock);
        if (s_msgHwnd) {
            PostMessageW(s_msgHwnd, WM_CLOSE, 0, 0);
        }
    } else {
        // Fallback: serve static UI files from ui/dist
        ServeStaticFile(clientSock, pathOnly);
        closesocket(clientSock);
    }
}

// ── Background SSE Stream Worker (~30Hz) ──
static void SseWorker() {
    int counter = 0;
    while (s_running.load(std::memory_order_relaxed)) {
        Sleep(33); // ~30 FPS

        bool hasClients = false;
        {
            std::lock_guard<std::mutex> lock(s_sseClientsMutex);
            hasClients = !s_sseClients.empty();
        }
        if (!hasClients) continue;

        RuntimeSnapshot snap{};
        engine::TakeSnapshot(snap);

        std::string payload;
        if (s_stateChanged.exchange(false, std::memory_order_acq_rel)) {
            payload = "event: state_changed\ndata: " + SerializeTelemetry(snap) + "\n\n";
        } else {
            payload = "event: telemetry\ndata: " + SerializeTelemetry(snap) + "\n\n";
        }

        std::lock_guard<std::mutex> lock(s_sseClientsMutex);
        for (auto it = s_sseClients.begin(); it != s_sseClients.end();) {
            SOCKET cs = *it;
            int sent = send(cs, payload.data(), static_cast<int>(payload.size()), 0);
            if (sent == SOCKET_ERROR) {
                closesocket(cs);
                it = s_sseClients.erase(it);
            } else {
                ++it;
            }
        }
        counter++;
    }
}

// ── Main Server Listener ──
static void ServerWorker(uint16_t port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        DLOG_ERR(Runtime, "WSAStartup failed");
        return;
    }

    s_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listenSocket == INVALID_SOCKET) {
        DLOG_ERR(Runtime, "Failed to create IPC listen socket");
        WSACleanup();
        return;
    }

    BOOL opt = TRUE;
    setsockopt(s_listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in service{};
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr("127.0.0.1"); // Loopback only
    service.sin_port = htons(port);

    if (bind(s_listenSocket, reinterpret_cast<SOCKADDR*>(&service), sizeof(service)) == SOCKET_ERROR) {
        DLOG_ERR(Runtime, "IPC bind failed on port %u", port);
        closesocket(s_listenSocket);
        WSACleanup();
        return;
    }

    if (listen(s_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        DLOG_ERR(Runtime, "IPC listen failed");
        closesocket(s_listenSocket);
        WSACleanup();
        return;
    }

    DLOG_INFO(Runtime, "IPC Server listening on http://127.0.0.1:%u", port);

    while (s_running.load(std::memory_order_relaxed)) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s_listenSocket, &readSet);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms timeout for graceful shutdown check

        int sel = select(0, &readSet, nullptr, nullptr, &tv);
        if (sel > 0 && FD_ISSET(s_listenSocket, &readSet)) {
            SOCKET clientSock = accept(s_listenSocket, nullptr, nullptr);
            if (clientSock != INVALID_SOCKET) {
                // Handle client in lightweight detached thread to not block server
                std::thread([clientSock]() {
                    HandleClient(clientSock);
                }).detach();
            }
        }
    }

    closesocket(s_listenSocket);
    s_listenSocket = INVALID_SOCKET;

    {
        std::lock_guard<std::mutex> lock(s_sseClientsMutex);
        for (SOCKET s : s_sseClients) {
            closesocket(s);
        }
        s_sseClients.clear();
    }

    WSACleanup();
}

bool StartServer(uint16_t port, HWND msgHwnd) {
    if (s_running.load(std::memory_order_acquire)) return true;

    s_msgHwnd = msgHwnd;
    s_authToken = GenerateToken();
    SaveTokenFile(s_authToken);

    s_running.store(true, std::memory_order_release);
    s_serverThread = std::thread(ServerWorker, port);
    s_sseThread = std::thread(SseWorker);
    return true;
}

void StopServer() {
    if (!s_running.exchange(false, std::memory_order_acq_rel)) return;

    if (s_listenSocket != INVALID_SOCKET) {
        closesocket(s_listenSocket);
        s_listenSocket = INVALID_SOCKET;
    }

    if (s_serverThread.joinable()) {
        s_serverThread.join();
    }
    if (s_sseThread.joinable()) {
        s_sseThread.join();
    }

    RemoveTokenFile();
    DLOG_INFO(Shutdown, "IPC Server stopped");
}

void NotifyStateChanged() {
    s_stateChanged.store(true, std::memory_order_release);
}

std::string GetAuthToken() {
    return s_authToken;
}

bool IsRunning() {
    return s_running.load(std::memory_order_acquire);
}

} // namespace ipc
