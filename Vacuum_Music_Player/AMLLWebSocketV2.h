#ifndef AMLL_WEBSOCKET_CLIENT_H
#define AMLL_WEBSOCKET_CLIENT_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ==========================
// 数据结构（与 V1 兼容）
// ==========================
struct Artist {
    std::string id;
    std::string name;
};

struct LyricWord {
    uint64_t start_time;
    uint64_t end_time;
    std::string word;
};

struct LyricLine {
    uint64_t start_time;
    uint64_t end_time;
    std::vector<LyricWord> words;
    std::string translated_lyric;
    std::string roman_lyric;
    uint8_t flag;
};

enum class AMLLCommand {
    None,
    Pause,
    Resume,
    Forward,
    Backward,
    Seek,
};

// ==========================
// AMLL WebSocket V2 标准协议客户端
// ==========================
class AMLLWebSocketClient {
public:
    explicit AMLLWebSocketClient(const std::string& url);
    ~AMLLWebSocketClient();

    // 生命周期
    void start();
    void stop();
    void waitForExit();
    AMLLCommand getNextCommand();

    // ===== V1 兼容公共接口（完全一致） =====
    void sendMusicInfo(const std::string& musicId,
        const std::string& musicName,
        const std::string& albumId,
        const std::string& albumName,
        const std::vector<Artist>& artists,
        uint64_t durationMs);

    void sendAlbumCover(const std::vector<uint8_t>& imageData);

    void sendLyricFromLRC(const std::string& lrcContent);

    void sendResumed();
    void sendPaused();
    void sendProgress(uint64_t posMs);

    void sendAlbumCoverByURI(const std::string& dataUri);

    // V1 中的辅助函数（public 以便调用）
    std::string imageToDataURI(const std::vector<uint8_t>& imageData, const std::string& mimeType);
    static std::string base64_encode(const std::vector<uint8_t>& data);

    uint64_t getPendingSeek();

    // ===== V2 新增功能（可选） =====
    void sendLyricTTML(const std::string& ttmlXml);
    void sendVolume(float vol);
    void sendAudioData(const std::vector<uint8_t>& pcmData);

    // LRC → TTML 转换（静态工具）
    static std::string convertLRCToTTML(const std::string& lrc);
    void sendInitialData();
    // LRC → Structured 歌词转换（新增）

    static std::vector<LyricLine> convertLRCToStructured(const std::string& lrc,
        uint64_t defaultDurationMs = 5000,
        bool enableSplit = true);

    // 公共接口增加参数
    void sendLyricFromLRC(const std::string& lrcContent, bool enableSplit = true);
private:
    std::string url_;
    ix::WebSocket webSocket_;
    std::atomic<bool> connected_{ false };
    std::atomic<bool> running_{ false };
    std::unique_ptr<std::thread> progressThread_;

    void onMessage(const ix::WebSocketMessagePtr& msg);
    

    // 内部发送 JSON 消息
    void sendJson(const json& msg);

    // 构造标准 V2 状态消息
    json makeStateMsg(json valueObj);

    // 命令队列
    std::queue<AMLLCommand> cmdQueue_;
    std::mutex cmdMutex_;
    std::atomic<uint64_t> PendingSeek{ UINT64_MAX };

    // 内部解析 LRC 时间
    static double parseLrcTime(const std::string& tag);
};

#endif // AMLL_WEBSOCKET_CLIENT_H