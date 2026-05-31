// texture_utils.h
#pragma once
#include <windows.h>
#include <d3d11.h>

bool LoadTextureFromFileWIC(const wchar_t* filename, ID3D11ShaderResourceView** out_srv, int* out_width = nullptr, int* out_height = nullptr);
void ReleaseTexture(ID3D11ShaderResourceView*& srv);
bool LoadTextureFromResource(int resourceId, const wchar_t* resourceType, ID3D11ShaderResourceView** out_srv, int* out_width = nullptr, int* out_height = nullptr);
