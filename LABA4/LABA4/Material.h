#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>

struct Material {
    std::string name;

    ID3D11ShaderResourceView* diffuseTexture = nullptr;
    ID3D11ShaderResourceView* specularTexture = nullptr;
    ID3D11ShaderResourceView* normalTexture = nullptr;

    DirectX::XMFLOAT4 diffuseColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    DirectX::XMFLOAT4 specularColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

    float shininess = 32.0f;

    float textureOffsetU = 0.0f;
    float textureOffsetV = 0.0f;
    float textureScaleU = 1.0f;
    float textureScaleV = 1.0f;

    float animationSpeed = 0.0f;
    float animationTime = 0.0f;

    Material() {}

    ~Material() {
        if (diffuseTexture) diffuseTexture->Release();
        if (specularTexture) specularTexture->Release();
        if (normalTexture) normalTexture->Release();
    }
};
