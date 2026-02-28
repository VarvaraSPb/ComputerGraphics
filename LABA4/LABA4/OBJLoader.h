#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <map>
#include "Material.h"
#include "TextureLoader.h"

using namespace DirectX;

struct OBJVertex {
    XMFLOAT3 pos;
    XMFLOAT3 normal;
    XMFLOAT2 texCoord;
    int materialIndex;

    bool operator==(const OBJVertex& other) const {
        return (pos.x == other.pos.x && pos.y == other.pos.y && pos.z == other.pos.z &&
            normal.x == other.normal.x && normal.y == other.normal.y && normal.z == other.normal.z &&
            texCoord.x == other.texCoord.x && texCoord.y == other.texCoord.y &&
            materialIndex == other.materialIndex);
    }
};

struct OBJVertexHash {
    size_t operator()(const OBJVertex& v) const {
        size_t h1 = std::hash<float>()(v.pos.x) ^ (std::hash<float>()(v.pos.y) << 1) ^ (std::hash<float>()(v.pos.z) << 2);
        size_t h2 = std::hash<float>()(v.normal.x) ^ (std::hash<float>()(v.normal.y) << 1) ^ (std::hash<float>()(v.normal.z) << 2);
        size_t h3 = std::hash<float>()(v.texCoord.x) ^ (std::hash<float>()(v.texCoord.y) << 1);
        size_t h4 = std::hash<int>()(v.materialIndex);
        return h1 ^ (h2 << 3) ^ (h3 << 7) ^ (h4 << 11);
    }
};

struct OBJMesh {
    std::vector<OBJVertex> vertices;
    std::vector<UINT> indices;
    int materialIndex = -1;
    std::string name;

    OBJMesh() : materialIndex(-1) {}
};

class OBJLoader {
public:
    static bool LoadOBJ32(const std::string& filename,
        std::vector<OBJVertex>& outVertices,
        std::vector<UINT>& outIndices,
        std::vector<Material*>& outMaterials,
        std::vector<OBJMesh>& outMeshes,
        ID3D11Device* device,
        bool generateNormals = true) {

        std::cout << "\n=== OBJLoader Debug ===" << std::endl;
        std::cout << "Loading file: " << filename << std::endl;

        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "ERROR: Cannot open file: " << filename << std::endl;
            return false;
        }

        std::vector<XMFLOAT3> positions;
        std::vector<XMFLOAT3> normals;
        std::vector<XMFLOAT2> texCoords;

        std::map<std::string, Material*> materialMap;
        std::string mtlFilename;

        std::vector<OBJMesh> meshes;
        OBJMesh currentMesh;
        currentMesh.materialIndex = -1;

        std::string line;
        int lineNumber = 0;

        while (std::getline(file, line)) {
            lineNumber++;

            if (line.empty() || line[0] == '#') continue;

            std::istringstream iss(line);
            std::string prefix;
            iss >> prefix;

            if (prefix == "v") {
                XMFLOAT3 pos;
                iss >> pos.x >> pos.y >> pos.z;
                positions.push_back(pos);
            }
            else if (prefix == "vn") {
                XMFLOAT3 normal;
                iss >> normal.x >> normal.y >> normal.z;
                normals.push_back(normal);
            }
            else if (prefix == "vt") {
                XMFLOAT2 tex;
                iss >> tex.x >> tex.y;
                texCoords.push_back(tex);
            }
            else if (prefix == "mtllib") {
                iss >> mtlFilename;
                std::string mtlPath = GetDirectoryPath(filename) + mtlFilename;
                LoadMaterials(mtlPath, materialMap, device);
            }
            else if (prefix == "usemtl") {
                if (!currentMesh.vertices.empty()) {
                    OptimizeMesh(currentMesh);
                    meshes.push_back(currentMesh);
                }

                currentMesh = OBJMesh();
                std::string mtlName;
                iss >> mtlName;

                auto it = materialMap.find(mtlName);
                if (it != materialMap.end()) {
                    int materialIndex = -1;
                    for (size_t i = 0; i < outMaterials.size(); i++) {
                        if (outMaterials[i] == it->second) {
                            materialIndex = static_cast<int>(i);
                            break;
                        }
                    }
                    if (materialIndex == -1) {
                        materialIndex = static_cast<int>(outMaterials.size());
                        outMaterials.push_back(it->second);
                    }
                    currentMesh.materialIndex = materialIndex;
                }
            }
            else if (prefix == "o" || prefix == "g") {
                if (!currentMesh.vertices.empty()) {
                    OptimizeMesh(currentMesh);
                    meshes.push_back(currentMesh);
                }
                currentMesh = OBJMesh();
                currentMesh.materialIndex = -1;
                iss >> currentMesh.name;
            }
            else if (prefix == "f") {
                std::vector<std::string> faceVertices;
                std::string vertexStr;

                while (iss >> vertexStr) {
                    faceVertices.push_back(vertexStr);
                }

                if (faceVertices.size() < 3) continue;

                for (size_t i = 1; i < faceVertices.size() - 1; i++) {
                    ParseFaceVertex(faceVertices[0], positions, normals, texCoords,
                        currentMesh.vertices, currentMesh.materialIndex);
                    ParseFaceVertex(faceVertices[i], positions, normals, texCoords,
                        currentMesh.vertices, currentMesh.materialIndex);
                    ParseFaceVertex(faceVertices[i + 1], positions, normals, texCoords,
                        currentMesh.vertices, currentMesh.materialIndex);
                }
            }
        }

        if (!currentMesh.vertices.empty()) {
            OptimizeMesh(currentMesh);
            meshes.push_back(currentMesh);
        }

        file.close();

        std::cout << "Parsed " << lineNumber << " lines" << std::endl;
        std::cout << "Found " << positions.size() << " positions" << std::endl;
        std::cout << "Found " << normals.size() << " normals" << std::endl;
        std::cout << "Found " << texCoords.size() << " texcoords" << std::endl;
        std::cout << "Found " << meshes.size() << " meshes" << std::endl;

        outVertices.clear();
        outIndices.clear();
        outMeshes = meshes;

        UINT vertexOffset = 0;
        for (auto& mesh : meshes) {
            for (const auto& v : mesh.vertices) {
                outVertices.push_back(v);
            }

            for (UINT idx : mesh.indices) {
                outIndices.push_back(vertexOffset + idx);
            }

            vertexOffset += static_cast<UINT>(mesh.vertices.size());
        }

        std::cout << "Total vertices: " << outVertices.size() << std::endl;
        std::cout << "Total indices: " << outIndices.size() << std::endl;
        std::cout << "Total materials: " << outMaterials.size() << std::endl;
        std::cout << "=== OBJLoader End ===\n" << std::endl;

        return !outVertices.empty();
    }

private:
    static std::string GetDirectoryPath(const std::string& filename) {
        size_t pos = filename.find_last_of("/\\");
        if (pos != std::string::npos) {
            return filename.substr(0, pos + 1);
        }
        return "";
    }

    static void LoadMaterials(const std::string& filename,
        std::map<std::string, Material*>& materialMap,
        ID3D11Device* device) {
        std::cout << "Loading MTL file: " << filename << std::endl;

        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open MTL file: " << filename << std::endl;
            return;
        }

        std::string line;
        Material* currentMaterial = nullptr;
        std::string currentMaterialName;

        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            std::istringstream iss(line);
            std::string prefix;
            iss >> prefix;

            if (prefix == "newmtl") {
                if (currentMaterial) {
                    materialMap[currentMaterialName] = currentMaterial;
                }
                currentMaterial = new Material();
                iss >> currentMaterialName;
                currentMaterial->name = currentMaterialName;
                std::cout << "  Material: " << currentMaterialName << std::endl;
            }
            else if (prefix == "Ns") {
                if (currentMaterial) {
                    iss >> currentMaterial->shininess;
                }
            }
            else if (prefix == "Kd") {
                if (currentMaterial) {
                    float r, g, b;
                    iss >> r >> g >> b;
                    currentMaterial->diffuseColor = XMFLOAT4(r, g, b, 1.0f);
                }
            }
            else if (prefix == "Ks") {
                if (currentMaterial) {
                    float r, g, b;
                    iss >> r >> g >> b;
                    currentMaterial->specularColor = XMFLOAT4(r, g, b, 1.0f);
                }
            }
            else if (prefix == "map_Kd") {
                if (currentMaterial && device) {
                    std::string texFile;
                    iss >> texFile;

                    std::string basePath = GetDirectoryPath(filename);
                    std::string texPath = basePath + texFile;

                    std::vector<std::string> extensions = { ".tga", ".png", ".jpg", ".jpeg", ".bmp" };
                    bool loaded = false;

                    for (const auto& ext : extensions) {
                        std::string testPath = texPath;
                        if (testPath.find(ext) == std::string::npos) {
                            testPath = texPath + ext;
                        }

                        DWORD fileAttr = GetFileAttributesA(testPath.c_str());
                        if (fileAttr != INVALID_FILE_ATTRIBUTES) {
                            currentMaterial->diffuseTexture = TextureLoader::CreateTextureFromFile(device, testPath);
                            if (currentMaterial->diffuseTexture) {
                                std::cout << "    Loaded diffuse texture: " << texFile << ext << std::endl;
                                loaded = true;
                                break;
                            }
                        }
                    }

                    if (!loaded) {
                        currentMaterial->diffuseTexture = TextureLoader::CreateTextureFromFile(device, texPath);
                        if (currentMaterial->diffuseTexture) {
                            std::cout << "    Loaded diffuse texture: " << texFile << std::endl;
                        }
                    }
                }
            }
        }

        if (currentMaterial) {
            materialMap[currentMaterialName] = currentMaterial;
        }

        file.close();
        std::cout << "Loaded " << materialMap.size() << " materials" << std::endl;
    }

    static void ParseFaceVertex(const std::string& vertexStr,
        const std::vector<XMFLOAT3>& positions,
        const std::vector<XMFLOAT3>& normals,
        const std::vector<XMFLOAT2>& texCoords,
        std::vector<OBJVertex>& outVertices,
        int materialIndex) {

        OBJVertex vertex = {};
        vertex.materialIndex = materialIndex;

        std::stringstream ss(vertexStr);
        std::string token;
        std::vector<std::string> tokens;

        while (std::getline(ss, token, '/')) {
            tokens.push_back(token);
        }

        if (tokens.size() >= 1 && !tokens[0].empty()) {
            int idx = std::stoi(tokens[0]) - 1;
            if (idx >= 0 && idx < static_cast<int>(positions.size())) {
                vertex.pos = positions[idx];
            }
        }

        if (tokens.size() >= 2 && !tokens[1].empty()) {
            int idx = std::stoi(tokens[1]) - 1;
            if (idx >= 0 && idx < static_cast<int>(texCoords.size())) {
                vertex.texCoord = texCoords[idx];
            }
        }

        if (tokens.size() >= 3 && !tokens[2].empty()) {
            int idx = std::stoi(tokens[2]) - 1;
            if (idx >= 0 && idx < static_cast<int>(normals.size())) {
                vertex.normal = normals[idx];
            }
        }

        outVertices.push_back(vertex);
    }

    static void OptimizeMesh(OBJMesh& mesh) {
        std::unordered_map<OBJVertex, UINT, OBJVertexHash> vertexMap;
        std::vector<OBJVertex> optimizedVertices;
        std::vector<UINT> optimizedIndices;

        for (const auto& vertex : mesh.vertices) {
            auto it = vertexMap.find(vertex);
            if (it != vertexMap.end()) {
                optimizedIndices.push_back(it->second);
            }
            else {
                UINT newIndex = static_cast<UINT>(optimizedVertices.size());
                vertexMap[vertex] = newIndex;
                optimizedVertices.push_back(vertex);
                optimizedIndices.push_back(newIndex);
            }
        }

        mesh.vertices = optimizedVertices;
        mesh.indices = optimizedIndices;
    }
};