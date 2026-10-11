// albumimgtagreader.cpp
#include "albumimgtagreader.h"
#include <platform/platform.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4item.h>
#include <taglib/id3v2frame.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/fileref.h>
#include <taglib/xiphcomment.h>
#include <taglib/attachedpictureframe.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>
#include <filesystem>

// Ԥ�����˹Ȩ�ر������ڰ뾶�� x �仯����� 81 �ְ뾶��
static std::vector<float> ComputeGaussianWeights(float radius) {
    if (radius < 1.0f) return { 1.0f };
    float sigma = radius / 3.0f;
    int kernelSize = (int)(sigma * 6) + 1;  // ���� 6��
    int half = kernelSize / 2;
    std::vector<float> weights(kernelSize);
    float sum = 0.0f;
    for (int i = 0; i < kernelSize; ++i) {
        float x = static_cast<float>(i - half);
        float w = expf(-(x * x) / (2.0f * sigma * sigma));
        weights[i] = w;
        sum += w;
    }
    for (float& w : weights) w /= sum;
    return weights;
}

// �����Ա�뾶ģ����direction: 0=ˮƽ, 1=��ֱ��
static void VariableDirectionalBlur(std::vector<unsigned char>& pixels, int width, int height, bool vertical) {
    std::vector<unsigned char> result(pixels.size());
    const float maxRadius = 80.0f;
    const float fadeEnd = width * 0.55f;  // 172.5 ����

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // ���㵱ǰ���ص�ģ���뾶������ x ���꣩
            float r = 0.0f;
            if (x <= fadeEnd) {
                r = maxRadius * (1.0f - (float)x / fadeEnd);
                if (r < 0.0f) r = 0.0f;
            }
            else {
                r = 0.0f;
            }

            if (r < 1.0f) {
                // ����ģ����ֱ�Ӹ���ԭ����
                int idx = (y * width + x) * 4;
                result[idx] = pixels[idx];
                result[idx + 1] = pixels[idx + 1];
                result[idx + 2] = pixels[idx + 2];
                result[idx + 3] = pixels[idx + 3];
                continue;
            }

            // ��ȡ�ð뾶�ĸ�˹Ȩ�ر�
            auto weights = ComputeGaussianWeights(r);
            int kernelSize = (int)weights.size();
            int half = kernelSize / 2;

            float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumA = 0.0f;
            float totalWeight = 0.0f;

            if (!vertical) {
                // ˮƽģ��
                int startX = (std::max)(0, x - half);
                int endX = (std::min)(width - 1, x + half);
                for (int sx = startX; sx <= endX; ++sx) {
                    int weightIdx = sx - (x - half);
                    if (weightIdx < 0 || weightIdx >= kernelSize) continue;
                    float w = weights[weightIdx];
                    int idx = (y * width + sx) * 4;
                    sumR += pixels[idx] * w;
                    sumG += pixels[idx + 1] * w;
                    sumB += pixels[idx + 2] * w;
                    sumA += pixels[idx + 3] * w;
                    totalWeight += w;
                }
            }
            else {
                // ��ֱģ��
                int startY = ((std::max))(0, y - half);
                int endY = (std::min)(height - 1, y + half);
                for (int sy = startY; sy <= endY; ++sy) {
                    int weightIdx = sy - (y - half);
                    if (weightIdx < 0 || weightIdx >= kernelSize) continue;
                    float w = weights[weightIdx];
                    int idx = (sy * width + x) * 4;
                    sumR += pixels[idx] * w;
                    sumG += pixels[idx + 1] * w;
                    sumB += pixels[idx + 2] * w;
                    sumA += pixels[idx + 3] * w;
                    totalWeight += w;
                }
            }

            if (totalWeight > 0.0f) {
                int outIdx = (y * width + x) * 4;
                result[outIdx] = (unsigned char)(sumR / totalWeight);
                result[outIdx + 1] = (unsigned char)(sumG / totalWeight);
                result[outIdx + 2] = (unsigned char)(sumB / totalWeight);
                result[outIdx + 3] = (unsigned char)(sumA / totalWeight);
            }
            else {
                // ����
                int idx = (y * width + x) * 4;
                result[idx] = pixels[idx];
                result[idx + 1] = pixels[idx + 1];
                result[idx + 2] = pixels[idx + 2];
                result[idx + 3] = pixels[idx + 3];
            }
        }
    }
    pixels = std::move(result);
}


// ����������Ӧ�ý���͸��Ч��
std::vector<unsigned char> ProcessAlbumArtWithGradient(const std::vector<unsigned char>& imageData, int targetWidth, int targetHeight) {
    OutputDebugStringA(("[PAWG] entry, input=" + std::to_string(imageData.size()) + "\n").c_str());

    std::vector<unsigned char> pixelData = DecodeAndScaleImage(imageData, targetWidth, targetHeight);
    if (pixelData.empty()) {
        OutputDebugStringA("[PAWG] DecodeAndScaleImage FAILED\n");
        return {};
    }
    OutputDebugStringA(("[PAWG] decode OK, size=" + std::to_string(pixelData.size()) + "\n").c_str());

    VariableDirectionalBlur(pixelData, targetWidth, targetHeight, false);
    VariableDirectionalBlur(pixelData, targetWidth, targetHeight, true);
    OutputDebugStringA("[PAWG] blur OK\n");

    VariableDirectionalBlur(pixelData, targetWidth, targetHeight, false);
    VariableDirectionalBlur(pixelData, targetWidth, targetHeight, true);

    // 3. Ӧ�� alpha ����͸����ʹ�� smoothstep ���ߣ�������һ����ʼƫ�ƣ�ǰ 10% ��ȫ͸����
    const float startFade = 0.05f;    // ǰ 5% ������ȫ͸��
    const float endFade = 0.75f;      // 75% ����ȫ��͸��
    for (int y = 0; y < targetHeight; ++y) {
        for (int x = 0; x < targetWidth; ++x) {
            size_t idx = (y * targetWidth + x) * 4 + 3;
            float t = static_cast<float>(x) / targetWidth;  // 0..1
            float alpha = 0.0f;
            if (t <= startFade) {
                alpha = 0.0f;
            }
            else if (t >= endFade) {
                alpha = 255.0f;
            }
            else {
                // �� [startFade, endFade] ֮��ƽ����ֵ��ʹ�� smoothstep
                float s = (t - startFade) / (endFade - startFade);
                // smoothstep: 3*s^2 - 2*s^3
                float smooth = s * s * (3.0f - 2.0f * s);
                alpha = 255.0f * smooth;
            }
            pixelData[idx] = static_cast<unsigned char>(alpha);
        }
    }

    OutputDebugStringA("[PAWG] done\n");
    return pixelData;
}

// ��ȡ��չ����Сд��
static std::string GetExtension(const PathType& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);   // �� ȥ��ǰ����
    return ext;
}

// ---------- MP3 ----------
static std::vector<unsigned char> ExtractFromMP3(const PathType& widePath) {
    TagLib::MPEG::File file(widePath.c_str());
    if (!file.isValid() || !file.ID3v2Tag()) return {};

    auto* tag = file.ID3v2Tag();
    auto frames = tag->frameListMap()["APIC"];
    if (frames.isEmpty()) return {};

    // ����ȡ FrontCover������ȡ��һ�� APIC ֡
    for (auto* frame : frames) {
        auto* pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frame);
        if (pic && pic->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
            TagLib::ByteVector data = pic->picture();
            return std::vector<unsigned char>(data.begin(), data.end());
        }
    }
    // ���û�� FrontCover�����ص�һ�� APIC ֡
    if (!frames.isEmpty()) {
        auto* firstPic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
        if (firstPic) {
            TagLib::ByteVector data = firstPic->picture();
            return std::vector<unsigned char>(data.begin(), data.end());
        }
    }
    return {};
}

// ---------- FLAC ----------
static std::vector<unsigned char> ExtractFromFLAC(const PathType& widePath) {
    TagLib::FLAC::File file(widePath.c_str());
    if (!file.isValid()) return {};

    auto pictures = file.pictureList();
    if (pictures.isEmpty()) return {};

    // ����ȡ FrontCover
    for (auto* pic : pictures) {
        if (pic->type() == TagLib::FLAC::Picture::FrontCover) {
            TagLib::ByteVector data = pic->data();
            return std::vector<unsigned char>(data.begin(), data.end());
        }
    }
    // ����ȡ��һ��
    TagLib::ByteVector data = pictures[0]->data();
    return std::vector<unsigned char>(data.begin(), data.end());
}

// ---------- M4A / MP4 ----------
static std::vector<unsigned char> ExtractFromM4A(const PathType& widePath) {
    TagLib::MP4::File file(widePath.c_str());
    if (!file.isValid() || !file.tag()) return {};

    TagLib::MP4::Tag* tag = file.tag();

#if defined(TAGLIB_MAJOR_VERSION) && TAGLIB_MAJOR_VERSION >= 2
    auto items = tag->itemMap();
    auto it = items.find("covr");
    if (it == items.end()) return {};
    TagLib::MP4::Item coverItem = it->second;
#else
    auto items = tag->itemListMap();
    if (!items.contains("covr")) return {};
    TagLib::MP4::Item coverItem = items["covr"];
#endif

    auto coverList = coverItem.toCoverArtList();
    if (coverList.isEmpty()) return {};
    TagLib::ByteVector data = coverList.front().data();
    return std::vector<unsigned char>(data.begin(), data.end());
}


static std::string ExtractLyricsFromFLAC(const PathType& widePath) {
    TagLib::FLAC::File file(widePath.c_str());
    if (!file.isValid()) return "";

    TagLib::Ogg::XiphComment* xiph = file.xiphComment();
    if (!xiph) return "";

    // Vorbisע���и��ͨ���洢�� "LYRICS" �ֶ�
    auto lyricsList = xiph->fieldListMap()["LYRICS"];
    if (!lyricsList.isEmpty()) {
        return lyricsList.front().to8Bit(true);
    }
    // ��ѡ����Щ����Ҳ�� "UNSYNCEDLYRICS"
    lyricsList = xiph->fieldListMap()["UNSYNCEDLYRICS"];
    if (!lyricsList.isEmpty()) {
        return lyricsList.front().to8Bit(true);
    }
    return "";
}

std::string GetLyricsFromFile(const PathType& widePath) {
    std::string ext = GetExtension(widePath);

    // FLAC �� Xiph ע��·��
    if (ext == "flac") {
        return ExtractLyricsFromFLAC(widePath);
    }

    // MP3 / �������ܴ�ID3v2�ĸ�ʽ
    TagLib::FileRef f(widePath.c_str());
    if (f.isNull() || !f.tag()) return "";

    TagLib::ID3v2::Tag* id3v2tag = nullptr;
    TagLib::MPEG::File* mpegFile = dynamic_cast<TagLib::MPEG::File*>(f.file());
    if (mpegFile) {
        id3v2tag = mpegFile->ID3v2Tag();
    }
    else {
        // ���� FLAC Ƕ�� ID3v2 ���������ѡ������
        TagLib::FLAC::File* flacFile = dynamic_cast<TagLib::FLAC::File*>(f.file());
        if (flacFile) {
            id3v2tag = flacFile->ID3v2Tag();
        }
    }
    if (!id3v2tag) return "";

    TagLib::ID3v2::FrameList frames = id3v2tag->frameListMap()["USLT"];
    if (frames.isEmpty()) return "";

    for (auto* frame : frames) {
        auto* uslt = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame*>(frame);
        if (uslt && !uslt->text().isEmpty()) {
            return uslt->text().to8Bit(true);
        }
    }
    return "";
}
PathType FindLrcFile(const PathType& musicFilePath) {
    namespace fs = std::filesystem;
    fs::path musicPath(musicFilePath);
    fs::path musicDir = musicPath.parent_path();
    fs::path baseName = musicPath.stem();

    // 1. ͬĿ¼���� .lrc / .LRC
    for (const char* ext : { ".lrc", ".LRC" }) {
        fs::path lrcPath = musicDir / baseName;
        lrcPath += ext;
        if (fs::exists(lrcPath)) {
            return lrcPath.native();   // fs::path �� PathType��ƽ̨ԭ�����ͣ�
        }
    }

    // 2. �����ĸ���ļ�������UTF-8 ����������ƽ̨ͨ�ã�
    const std::vector<fs::path> lyricFolderNames = {
        fs::path("lyrics"),
        fs::path("Lyrics"),
        fs::path("LYRICS"),
        fs::path("���"),
        fs::path("LYRIC"),
        fs::path("Lyric"),
    };

    for (const auto& folderName : lyricFolderNames) {
        fs::path lyricDir = musicDir / folderName;
        if (!fs::exists(lyricDir) || !fs::is_directory(lyricDir)) {
            continue;
        }

        for (const auto& entry : fs::directory_iterator(lyricDir)) {
            if (!entry.is_regular_file()) continue;

            // ��չ���Ƚϣ�Сд��
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            if (ext != ".lrc") continue;

            // ���ļ����Ƚ�
            if (entry.path().stem() == baseName) {
                return entry.path().native();
            }
        }
    }

    return {};   // �� PathType
}
// ---------- ͳһ��� ----------
std::vector<unsigned char> ExtractAlbumArt(const PathType& filePath) {
    std::string ext = GetExtension(filePath);
    if (ext == "mp3")   return ExtractFromMP3(filePath);
    if (ext == "flac")  return ExtractFromFLAC(filePath);
    if (ext == "m4a" || ext == "aac") return ExtractFromM4A(filePath);
    return {};
}