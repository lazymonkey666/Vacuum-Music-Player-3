#include "texture_utils.h"
#include <wincodec.h>
#include <comdef.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")

extern ID3D11Device* g_pd3dDevice;  // 从 main.cpp 中声明

// 辅助函数：创建 WIC 工厂
static IWICImagingFactory* GetWICFactory() {
    static IWICImagingFactory* factory = nullptr;
    if (!factory) {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return nullptr;
    }
    return factory;
}

bool LoadTextureFromFileWIC(const wchar_t* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height) {
    if (!out_srv || !g_pd3dDevice) return false;

    IWICImagingFactory* factory = GetWICFactory();
    if (!factory) return false;

    // 创建解码器
    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(filename, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) return false;

    // 获取第一帧
    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr)) return false;

    // 转换为 32-bit BGRA 格式（DXGI_FORMAT_B8G8R8A8_UNORM）
    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    }
    frame->Release();
    if (FAILED(hr)) {
        if (converter) converter->Release();
        return false;
    }

    // 获取图像尺寸
    UINT width, height;
    converter->GetSize(&width, &height);

    // 复制像素数据到内存
    UINT stride = width * 4;
    UINT bufferSize = stride * height;
    BYTE* pixelData = new BYTE[bufferSize];
    hr = converter->CopyPixels(NULL, stride, bufferSize, pixelData);
    converter->Release();
    if (FAILED(hr)) {
        delete[] pixelData;
        return false;
    }

    // 创建 D3D11 纹理
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // 与 WIC 输出的 BGRA 匹配
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = pixelData;
    subResource.SysMemPitch = stride;

    ID3D11Texture2D* texture = nullptr;
    hr = g_pd3dDevice->CreateTexture2D(&desc, &subResource, &texture);
    delete[] pixelData;

    if (FAILED(hr) || !texture) return false;

    // 创建着色器资源视图
    hr = g_pd3dDevice->CreateShaderResourceView(texture, nullptr, out_srv);
    texture->Release();

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;

    return SUCCEEDED(hr);
}


bool LoadTextureFromResource(int resourceId, const wchar_t* resourceType, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height) {
    if (!out_srv || !g_pd3dDevice) return false;

    // 查找资源
    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), resourceType);
    if (!hRes) {
        OutputDebugString(L"FindResource failed\n");
        return false;
    }

    // 加载资源
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return false;

    // 获取资源大小和指针
    DWORD dataSize = SizeofResource(NULL, hRes);
    void* pData = LockResource(hData);
    if (!pData || dataSize == 0) return false;

    // 创建 IStream
    IStream* stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (FAILED(hr)) return false;

    ULONG bytesWritten = 0;
    hr = stream->Write(pData, dataSize, &bytesWritten);
    if (FAILED(hr)) {
        stream->Release();
        return false;
    }

    // 重置流位置
    LARGE_INTEGER li = { 0 };
    stream->Seek(li, STREAM_SEEK_SET, NULL);

    // 使用 WIC 解码
    IWICImagingFactory* factory = GetWICFactory();  // 你之前写的获取工厂的函数
    if (!factory) {
        stream->Release();
        return false;
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder);
    stream->Release();
    if (FAILED(hr)) return false;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr)) return false;

    // 转换到 BGRA
    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    }
    frame->Release();
    if (FAILED(hr)) {
        if (converter) converter->Release();
        return false;
    }

    UINT width, height;
    converter->GetSize(&width, &height);

    UINT stride = width * 4;
    UINT bufferSize = stride * height;
    BYTE* pixelData = new BYTE[bufferSize];
    hr = converter->CopyPixels(NULL, stride, bufferSize, pixelData);
    converter->Release();
    if (FAILED(hr)) {
        delete[] pixelData;
        return false;
    }

    // 创建纹理
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = pixelData;
    subResource.SysMemPitch = stride;

    ID3D11Texture2D* texture = nullptr;
    hr = g_pd3dDevice->CreateTexture2D(&desc, &subResource, &texture);
    delete[] pixelData;
    if (FAILED(hr) || !texture) return false;

    hr = g_pd3dDevice->CreateShaderResourceView(texture, nullptr, out_srv);
    texture->Release();

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;

    return SUCCEEDED(hr);
}



void ReleaseTexture(ID3D11ShaderResourceView*& srv) {
    if (srv) {
        srv->Release();
        srv = nullptr;
    }
}