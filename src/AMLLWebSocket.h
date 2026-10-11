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

// ==========================
// 小端序读写工具（内联实现）
// ==========================
inline uint16_t read_u16(const uint8_t*& p) {
    uint16_t v;
    memcpy(&v, p, 2);
    p += 2;
    return v;
}
inline uint32_t read_u32(const uint8_t*& p) {
    uint32_t v;
    memcpy(&v, p, 4);
    p += 4;
    return v;
}
inline uint64_t read_u64(const uint8_t*& p) {
    uint64_t v;
    memcpy(&v, p, 8);
    p += 8;
    return v;
}
inline double read_f64(const uint8_t*& p) {
    double v;
    memcpy(&v, p, 8);
    p += 8;
    return v;
}
inline void write_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.insert(buf.end(), (const uint8_t*)&v, (const uint8_t*)&v + 2);
}
inline void write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.insert(buf.end(), (const uint8_t*)&v, (const uint8_t*)&v + 4);
}
inline void write_u64(std::vector<uint8_t>& buf, uint64_t v) {
    buf.insert(buf.end(), (const uint8_t*)&v, (const uint8_t*)&v + 8);
}
inline void write_f64(std::vector<uint8_t>& buf, double v) {
    buf.insert(buf.end(), (const uint8_t*)&v, (const uint8_t*)&v + 8);
}
inline std::string read_nullstr(const uint8_t*& p) {
    std::string s;
    while (*p != 0) s += static_cast<char>(*p++);
    p++;
    return s;
}
inline void write_nullstr(std::vector<uint8_t>& buf, const std::string& s) {
    buf.insert(buf.end(), s.begin(), s.end());
    buf.push_back(0);
}

// ==========================
// 协议数据结构
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

enum class MsgType : uint16_t {
    Ping = 0,
    Pong = 1,
    SetMusicInfo = 2,
    SetMusicAlbumCoverImageURI = 3,
    SetMusicAlbumCoverImageData = 4,
    OnPlayProgress = 5,
    OnVolumeChanged = 6,
    OnPaused = 7,
    OnResumed = 8,
    OnAudioData = 9,
    SetLyric = 10,
    SetLyricFromTTML = 11,
    Pause = 12,
    Resume = 13,
    ForwardSong = 14,
    BackwardSong = 15,
    SetVolume = 16,
    SeekPlayProgress = 17,
};
enum class AMLLCommand {
    None,
    Pause,
    Resume,
    Forward,
    Backward,
    Seek,
    // 需要额外数据的可以单独处理，这里只放控制类
};
// ==========================
// AMLL WebSocket 客户端类
// ==========================
class AMLLWebSocketClient {
public:
    // 构造与析构
    explicit AMLLWebSocketClient(const std::string& url);
    ~AMLLWebSocketClient();

    // 控制生命周期
    void start();           // 启动连接及内部线程
    void stop();            // 停止所有活动
    void waitForExit();     // 阻塞等待用户按回车
    AMLLCommand getNextCommand();
    // 发送接口
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
    void sendProgress(uint64_t pos);
    void sendAlbumCoverByURI(const std::string& dataUri);
    std::string imageToDataURI(const std::vector<uint8_t>& imageData, const std::string& mimeType);

    uint64_t getPendingSeek();
private:
    // WebSocket 相关成员
    std::string url_;
    ix::WebSocket webSocket_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> progressThread_;

    // 内部回调与循环
    void onMessage(const ix::WebSocketMessagePtr& msg);
    
    void sendInitialData();   // 连接成功后发送示例数据（可覆写）

    // 序列化函数（静态私有，实现位于 .cpp）
    static std::vector<uint8_t> serializeSetMusicInfo(
        const std::string& music_id,
        const std::string& music_name,
        const std::string& album_id,
        const std::string& album_name,
        const std::vector<Artist>& artists,
        uint64_t duration);

    static std::vector<uint8_t> serializeSetAlbumCoverData(const std::vector<uint8_t>& image_data);
    static std::vector<uint8_t> serializeSetLyricFromTTML(const std::string& ttmlXml);
    static std::vector<uint8_t> serializeOnResumed();
    static std::vector<uint8_t> serializeOnPaused();
    static std::vector<uint8_t> serializeOnPlayProgress(uint64_t progress);
    static std::vector<uint8_t> serializePong();
    static std::vector<uint8_t> serializeSetAlbumCoverURI(const std::string& uri);
    static std::string base64_encode(const std::vector<uint8_t>& data);


    // LRC → TTML 转换（静态工具函数）
    static double parseLrcTime(const std::string& tag);
    static std::string convertLRCToTTML(const std::string& lrc);


    std::queue<AMLLCommand> cmdQueue_;
    std::mutex cmdMutex_;
    std::atomic<uint64_t> PendingSeek;
};

#endif // AMLL_WEBSOCKET_CLIENT_H