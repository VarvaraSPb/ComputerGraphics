#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <wincodec.h>  

#pragma comment(lib, "windowscodecs.lib")

class TextureLoader {
public:

    static ID3D11ShaderResourceView* CreateTGATexture(ID3D11Device* device, const std::string& filename) {
        std::cout << "  Loading TGA texture: " << filename << std::endl;

        FILE* file = nullptr;
        fopen_s(&file, filename.c_str(), "rb");
        if (!file) {
            std::cerr << "  Failed to open TGA file" << std::endl;
            return CreateDefaultTexture(device, "default");
        }

        unsigned char header[18];
        fread(header, 1, 18, file);

        int width = header[12] + header[13] * 256;
        int height = header[14] + header[15] * 256;
        int bpp = header[16]; 

        std::cout << "  TGA size: " << width << "x" << height << ", " << bpp << "bpp" << std::endl;

        if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
            std::cerr << "  Invalid TGA dimensions" << std::endl;
            fclose(file);
            return CreateDefaultTexture(device, "default");
        }

        int imageSize = width * height * (bpp / 8);
        std::vector<unsigned char> imageData(imageSize);
        fread(imageData.data(), 1, imageSize, file);
        fclose(file);

        std::vector<uint32_t> pixels(width * height);

        for (int i = 0; i < width * height; i++) {
            unsigned char r, g, b, a = 255;

            if (bpp == 24) {
                b = imageData[i * 3 + 0];
                g = imageData[i * 3 + 1];
                r = imageData[i * 3 + 2];
            }
            else if (bpp == 32) {
                b = imageData[i * 4 + 0];
                g = imageData[i * 4 + 1];
                r = imageData[i * 4 + 2];
                a = imageData[i * 4 + 3];
            }
            else {
                continue;
            }

            pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = pixels.data();
        data.SysMemPitch = width * 4;

        ID3D11Texture2D* texture = nullptr;
        HRESULT hr = device->CreateTexture2D(&texDesc, &data, &texture);
        if (FAILED(hr)) {
            std::cerr << "  Failed to create TGA texture" << std::endl;
            return CreateDefaultTexture(device, "default");
        }

        ID3D11ShaderResourceView* textureView = nullptr;
        hr = device->CreateShaderResourceView(texture, nullptr, &textureView);
        texture->Release();

        if (FAILED(hr)) {
            std::cerr << "  Failed to create shader resource view" << std::endl;
            return CreateDefaultTexture(device, "default");
        }

        std::cout << "  TGA texture loaded successfully!" << std::endl;
        return textureView;
    }

    static ID3D11ShaderResourceView* CreateTextureFromFile(ID3D11Device* device, const std::string& filename) {
        if (!device) return nullptr;

        std::cout << "Loading texture: " << filename << std::endl;

        DWORD fileAttr = GetFileAttributesA(filename.c_str());
        if (fileAttr == INVALID_FILE_ATTRIBUTES) {
            std::cout << "  Texture file not found" << std::endl;
            return CreateDefaultTexture(device, "default");
        }

        size_t dotPos = filename.find_last_of('.');
        if (dotPos == std::string::npos) {
            return CreateDefaultTexture(device, "default");
        }

        std::string ext = filename.substr(dotPos + 1);
        for (auto& c : ext) c = tolower(c);

        if (ext == "tga") {
            return CreateTGATexture(device, filename);
        }
        else if (ext == "dds") {
            return CreateDefaultTexture(device, "default");
        }
        else {
            return CreateWICTexture(device, filename);
        }
    }

private:
    static ID3D11ShaderResourceView* CreateDefaultTexture(ID3D11Device* device, const std::string& type) {
        std::cout << "  Creating default texture of type: " << type << std::endl;

        const int texSize = 64;
        std::vector<uint32_t> pixels(texSize * texSize);

        for (int y = 0; y < texSize; y++) {
            for (int x = 0; x < texSize; x++) {
                bool black = ((x / 8) + (y / 8)) % 2 == 0;

                if (type == "diffuse") {
                    pixels[y * texSize + x] = black ? 0xFFFF00FF : 0xFF000000;
                }
                else if (type == "specular") {
                    pixels[y * texSize + x] = black ? 0xFFFFFFFF : 0xFF8888FF;
                }
                else if (type == "normal") {
                    pixels[y * texSize + x] = black ? 0xFF8888FF : 0xFFFF88FF;
                }
                else {
                    pixels[y * texSize + x] = black ? 0xFFFFFFFF : 0xFF000000;
                }
            }
        }

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = texSize;
        texDesc.Height = texSize;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = pixels.data();
        data.SysMemPitch = texSize * 4;

        ID3D11Texture2D* texture = nullptr;
        HRESULT hr = device->CreateTexture2D(&texDesc, &data, &texture);
        if (FAILED(hr)) {
            std::cerr << "  Failed to create default texture" << std::endl;
            return nullptr;
        }

        ID3D11ShaderResourceView* textureView = nullptr;
        hr = device->CreateShaderResourceView(texture, nullptr, &textureView);
        texture->Release();

        return textureView;
    }

    static ID3D11ShaderResourceView* CreateWICTexture(ID3D11Device* device, const std::string& filename) {
        std::cout << "  Loading WIC texture: " << filename << std::endl;

        HRESULT hr = CoInitialize(nullptr);

        IWICImagingFactory* wicFactory = nullptr;
        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wicFactory)
        );

        if (FAILED(hr)) {
            std::cerr << "  Failed to create WIC factory" << std::endl;
            return CreateDefaultTexture(device, "default");
        }

        IWICBitmapDecoder* decoder = nullptr;

        std::wstring wfilename(filename.begin(), filename.end());

        hr = wicFactory->CreateDecoderFromFilename(
            wfilename.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder
        );

        if (FAILED(hr)) {
            std::cerr << "  Failed to create decoder for: " << filename << std::endl;
            wicFactory->Release();
            return CreateDefaultTexture(device, "default");
        }

        IWICBitmapFrameDecode* frame = nullptr;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) {
            std::cerr << "  Failed to get frame" << std::endl;
            decoder->Release();
            wicFactory->Release();
            return CreateDefaultTexture(device, "default");
        }

        UINT width, height;
        frame->GetSize(&width, &height);
        std::cout << "  Texture size: " << width << "x" << height << std::endl;

        IWICFormatConverter* converter = nullptr;
        hr = wicFactory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr)) {
            hr = converter->Initialize(
                frame,
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0f,
                WICBitmapPaletteTypeCustom
            );
        }

        if (FAILED(hr)) {
            std::cerr << "  Failed to convert format" << std::endl;
            frame->Release();
            decoder->Release();
            wicFactory->Release();
            return CreateDefaultTexture(device, "default");
        }

        UINT stride = width * 4;
        UINT imageSize = stride * height;
        std::vector<BYTE> buffer(imageSize);

        hr = converter->CopyPixels(nullptr, stride, imageSize, buffer.data());

        if (FAILED(hr)) {
            std::cerr << "  Failed to copy pixels" << std::endl;
            converter->Release();
            frame->Release();
            decoder->Release();
            wicFactory->Release();
            return CreateDefaultTexture(device, "default");
        }

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = buffer.data();
        data.SysMemPitch = stride;

        ID3D11Texture2D* texture = nullptr;
        hr = device->CreateTexture2D(&texDesc, &data, &texture);

        if (FAILED(hr)) {
            std::cerr << "  Failed to create D3D texture" << std::endl;
            converter->Release();
            frame->Release();
            decoder->Release();
            wicFactory->Release();
            return CreateDefaultTexture(device, "default");
        }

        ID3D11ShaderResourceView* textureView = nullptr;
        hr = device->CreateShaderResourceView(texture, nullptr, &textureView);

        texture->Release();
        converter->Release();
        frame->Release();
        decoder->Release();
        wicFactory->Release();
        CoUninitialize();

        if (FAILED(hr)) {
            std::cerr << "  Failed to create shader resource view" << std::endl;
            return CreateDefaultTexture(device, "default");
        }

        std::cout << "  Texture loaded successfully!" << std::endl;
        return textureView;
    }
};