// Model.cpp
#include "pch.h"

// Windows 헤더의 min/max 매크로가 std::min/std::max, Assimp, GLM 과 부딪히지 않게 이 파일에서만 지운다.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <glad/glad.h>
#include "Model.h"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace
{
    // 인스턴스 한 개의 데이터(float 개수): 월드 행렬 16 + 법선 행렬 9 + 강조(rgb, 세기) 4
    constexpr int kInstanceFloats = 29;
    constexpr int kHighlightOffset = 25;
    constexpr GLuint kInstanceAttrib = 3;   // 꼭짓점 속성 3~6 행렬, 7~9 법선 행렬, 10 강조

    glm::mat4 ToGlm(const aiMatrix4x4& m)
    {
        // aiMatrix4x4 는 행 우선, GLM 은 열 우선 → 전치
        return glm::transpose(glm::make_mat4(&m.a1));
    }

    bool Contains(const std::string& s, const char* part)
    {
        return s.find(part) != std::string::npos;
    }

    // Blender 가 복제할 때 붙이는 ".001" 같은 꼬리를 뗀다(데이터 설계의 이름 규칙).
    std::string StripNumberSuffix(const std::string& s)
    {
        const size_t dot = s.rfind('.');
        if (dot != std::string::npos && dot + 4 == s.size() &&
            std::isdigit(static_cast<unsigned char>(s[dot + 1])) &&
            std::isdigit(static_cast<unsigned char>(s[dot + 2])) &&
            std::isdigit(static_cast<unsigned char>(s[dot + 3])))
            return s.substr(0, dot);
        return s;
    }

    bool IsPartName(const std::string& name)
    {
        return name.rfind("EV-", 0) == 0 && !Contains(name, "_mesh") && !Contains(name, "__shell");
    }

    unsigned int UploadTexture(const unsigned char* rgba, int width, int height)
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // 색 텍스처는 sRGB 로 올려서 셰이더에서는 선형 값으로 읽는다.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }

    // GLB 안에 들어 있는 텍스처("*0" 같은 이름) 또는 같은 폴더의 그림 파일을 읽는다.
    unsigned int LoadTexture(const aiScene* scene, const std::string& key, const std::string& folder)
    {
        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = nullptr;
        std::vector<unsigned char> raw;

        if (const aiTexture* embedded = scene->GetEmbeddedTexture(key.c_str()))
        {
            if (embedded->mHeight == 0)   // PNG·JPG 압축 데이터
            {
                pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(embedded->pcData),
                                               static_cast<int>(embedded->mWidth), &width, &height, &channels, 4);
            }
            else                          // 압축 안 된 BGRA 픽셀
            {
                width = static_cast<int>(embedded->mWidth);
                height = static_cast<int>(embedded->mHeight);
                raw.resize(static_cast<size_t>(width) * height * 4);
                for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
                {
                    raw[i * 4 + 0] = embedded->pcData[i].r;
                    raw[i * 4 + 1] = embedded->pcData[i].g;
                    raw[i * 4 + 2] = embedded->pcData[i].b;
                    raw[i * 4 + 3] = embedded->pcData[i].a;
                }
            }
        }
        else
        {
            pixels = stbi_load((folder + key).c_str(), &width, &height, &channels, 4);
        }

        unsigned int tex = 0;
        if (pixels)
        {
            tex = UploadTexture(pixels, width, height);
            stbi_image_free(pixels);
        }
        else if (!raw.empty())
        {
            tex = UploadTexture(raw.data(), width, height);
        }
        return tex;
    }
}

bool Model::Load(const std::string& path, std::string* error)
{
    Release();
    const auto start = std::chrono::steady_clock::now();

    // 노드 계층(품번)을 지켜야 하므로 PreTransformVertices·OptimizeGraph 는 쓰지 않는다.
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_SortByPType);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        if (error) *error = std::string("Assimp: ") + importer.GetErrorString();
        return false;
    }

    std::string folder;
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos)
        folder = path.substr(0, slash + 1);

    // 1) 재질과 텍스처. Assimp 는 glTF 의 V 좌표를 뒤집으므로 그림도 뒤집어 읽는다(OpenGL 방식).
    stbi_set_flip_vertically_on_load(1);
    std::unordered_map<std::string, unsigned int> textureCache;
    m_materials.resize(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
    {
        const aiMaterial* am = scene->mMaterials[i];
        Material& mat = m_materials[i];

        aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
        if (am->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS || am->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            mat.baseColor = glm::vec4(color.r, color.g, color.b, color.a);

        const aiTextureType types[] = { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE };
        for (aiTextureType type : types)
        {
            aiString texPath;
            if (am->GetTextureCount(type) == 0 || am->GetTexture(type, 0, &texPath) != AI_SUCCESS)
                continue;
            const std::string key = texPath.C_Str();
            auto found = textureCache.find(key);
            if (found == textureCache.end())
            {
                const unsigned int tex = LoadTexture(scene, key, folder);
                found = textureCache.emplace(key, tex).first;
                if (tex)
                    m_textures.push_back(tex);
            }
            mat.texture = found->second;
            aiUVTransform uv;
            if (am->Get(AI_MATKEY_UVTRANSFORM(type, 0), uv) == AI_SUCCESS)
                mat.uvScale = glm::vec2(uv.mScaling.x, uv.mScaling.y);   // 타이어 무늬 반복(KHR_texture_transform)
            break;
        }

        aiString alphaMode;
        if (am->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS && std::strcmp(alphaMode.C_Str(), "BLEND") == 0)
            mat.blend = true;
    }
    stbi_set_flip_vertically_on_load(0);

    // 2) 메쉬를 GPU 로: 꼭짓점 = 위치(3) + 법선(3) + UV(2)
    m_meshes.resize(scene->mNumMeshes);
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        const aiMesh* am = scene->mMeshes[i];
        Mesh& mesh = m_meshes[i];
        mesh.material = static_cast<int>(am->mMaterialIndex);
        if (!(am->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) || am->mNumVertices == 0)
            continue;

        std::vector<float> vertices(static_cast<size_t>(am->mNumVertices) * 8);
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (unsigned int v = 0; v < am->mNumVertices; ++v)
        {
            float* out = &vertices[static_cast<size_t>(v) * 8];
            const aiVector3D& p = am->mVertices[v];
            out[0] = p.x; out[1] = p.y; out[2] = p.z;
            lo = glm::min(lo, glm::vec3(p.x, p.y, p.z));
            hi = glm::max(hi, glm::vec3(p.x, p.y, p.z));
            if (am->HasNormals())
            {
                out[3] = am->mNormals[v].x; out[4] = am->mNormals[v].y; out[5] = am->mNormals[v].z;
            }
            else
            {
                out[3] = 0.0f; out[4] = 1.0f; out[5] = 0.0f;
            }
            if (am->HasTextureCoords(0))
            {
                out[6] = am->mTextureCoords[0][v].x; out[7] = am->mTextureCoords[0][v].y;
            }
            else
            {
                out[6] = out[7] = 0.0f;
            }
        }
        mesh.boundsMin = lo;
        mesh.boundsMax = hi;

        std::vector<unsigned int> indices;
        indices.reserve(static_cast<size_t>(am->mNumFaces) * 3);
        for (unsigned int f = 0; f < am->mNumFaces; ++f)
        {
            const aiFace& face = am->mFaces[f];
            if (face.mNumIndices == 3)
                indices.insert(indices.end(), face.mIndices, face.mIndices + 3);
        }
        mesh.indexCount = static_cast<int>(indices.size());

        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ebo);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);
        const GLsizei stride = 8 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
        glBindVertexArray(0);
    }

    // 3) 노드 계층을 평평한 배열로(부모가 항상 자식보다 앞에 온다)
    std::vector<std::pair<const aiNode*, int>> stack{ { scene->mRootNode, -1 } };
    while (!stack.empty())
    {
        const auto [src, parent] = stack.back();
        stack.pop_back();

        Node node;
        node.name = src->mName.C_Str();
        node.parent = parent;
        node.world = (parent >= 0 ? m_nodes[parent].world : glm::mat4(1.0f)) * ToGlm(src->mTransformation);
        node.meshes.assign(src->mMeshes, src->mMeshes + src->mNumMeshes);
        node.isShell = Contains(node.name, "__shell");
        node.isPart = IsPartName(node.name);
        const int parentPart = parent >= 0 ? m_nodes[parent].part : -1;
        const int index = static_cast<int>(m_nodes.size());
        node.part = node.isPart ? index : parentPart;
        node.level = node.isPart ? (parentPart >= 0 ? m_nodes[parentPart].level + 1 : 0) : -1;
        m_nodes.push_back(node);

        for (int c = static_cast<int>(src->mNumChildren) - 1; c >= 0; --c)
            stack.push_back({ src->mChildren[c], index });
    }

    // 4) 부품 목록, 그릴 노드, 경계 상자, 통계
    m_min = glm::vec3(std::numeric_limits<float>::max());
    m_max = glm::vec3(-std::numeric_limits<float>::max());
    std::set<int> usedMeshes;
    m_normalMatrices.resize(m_nodes.size(), glm::mat3(1.0f));
    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
    {
        Node& node = m_nodes[i];
        if (node.isPart)
        {
            m_partIndex[StripNumberSuffix(node.name)] = i;
            ++m_stats.parts;
        }
        if (node.meshes.empty())
            continue;

        m_normalMatrices[i] = glm::inverseTranspose(glm::mat3(node.world));
        m_drawNodes.push_back(i);
        ++m_stats.meshNodes;
        bool hasBlend = false;
        for (int mi : node.meshes)
        {
            const Mesh& mesh = m_meshes[mi];
            if (mesh.indexCount == 0)
                continue;
            usedMeshes.insert(mi);
            m_stats.triangles += mesh.indexCount / 3;
            hasBlend = hasBlend || m_materials[mesh.material].blend;
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 local((corner & 1) ? mesh.boundsMax.x : mesh.boundsMin.x,
                                      (corner & 2) ? mesh.boundsMax.y : mesh.boundsMin.y,
                                      (corner & 4) ? mesh.boundsMax.z : mesh.boundsMin.z);
                const glm::vec3 world = glm::vec3(node.world * glm::vec4(local, 1.0f));
                node.boundsMin = node.hasBounds ? glm::min(node.boundsMin, world) : world;
                node.boundsMax = node.hasBounds ? glm::max(node.boundsMax, world) : world;
                node.hasBounds = true;
            }
        }
        if (hasBlend)
            m_blendNodes.push_back(i);
        if (node.isShell)
            m_shellNodes.push_back(i);
    }

    // 자식의 경계 상자를 부모에 더한다. 부모가 항상 앞에 있으므로 뒤에서부터 한 번 훑으면 된다.
    for (int i = static_cast<int>(m_nodes.size()) - 1; i > 0; --i)
    {
        const Node& child = m_nodes[i];
        if (!child.hasBounds || child.parent < 0)
            continue;
        Node& parent = m_nodes[child.parent];
        parent.boundsMin = parent.hasBounds ? glm::min(parent.boundsMin, child.boundsMin) : child.boundsMin;
        parent.boundsMax = parent.hasBounds ? glm::max(parent.boundsMax, child.boundsMax) : child.boundsMax;
        parent.hasBounds = true;
    }
    if (!m_nodes.empty() && m_nodes[0].hasBounds)
    {
        m_min = m_nodes[0].boundsMin;
        m_max = m_nodes[0].boundsMax;
    }
    else
    {
        m_min = glm::vec3(-1.0f);
        m_max = glm::vec3(1.0f);
    }

    // 노드 i 아래 전체가 차지하는 번호 범위 [i, m_subtreeEnd[i]).
    // 노드를 '부모 먼저, 자식은 바로 뒤에 이어서'(깊이 우선) 넣었으므로 한 부품의 아래는 번호가 이어져 있다.
    m_subtreeEnd.resize(m_nodes.size());
    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
        m_subtreeEnd[i] = i + 1;
    for (int i = static_cast<int>(m_nodes.size()) - 1; i > 0; --i)
    {
        const int parent = m_nodes[i].parent;
        if (parent >= 0)
            m_subtreeEnd[parent] = std::max(m_subtreeEnd[parent], m_subtreeEnd[i]);
    }

    // 겉껍데기 주인(바로 위 부품)과 투명도 표
    std::set<int> owners;
    for (int nodeIndex : m_shellNodes)
    {
        if (m_nodes[nodeIndex].part >= 0)
            owners.insert(m_nodes[nodeIndex].part);
    }
    m_shellOwners.assign(owners.begin(), owners.end());
    m_shellAlpha.assign(m_nodes.size(), 1.0f);
    m_highlighted.assign(m_nodes.size(), 0);
    m_hovered.assign(m_nodes.size(), 0);
    m_highlightNode = -1;
    BuildInstances();   // 6주차: 같은 메쉬를 쓰는 노드를 묶는다
    m_stats.uniqueMeshes = static_cast<int>(usedMeshes.size());
    m_stats.textures = static_cast<int>(m_textures.size());
    m_stats.loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (glGetError() != GL_NO_ERROR && error)
        *error = "OpenGL error while uploading the model";
    return true;
}

void Model::Release()
{
    for (Mesh& mesh : m_meshes)
    {
        if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
        if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
    }
    for (InstanceGroup& g : m_groups)
    {
        if (g.vao) glDeleteVertexArrays(1, &g.vao);
    }
    if (m_instanceVbo)
        glDeleteBuffers(1, &m_instanceVbo);
    m_instanceVbo = 0;
    m_groups.clear();
    m_instanceNodes.clear();
    m_meshGrouped.clear();
    m_nodeAllGrouped.clear();
    m_instanceData.clear();
    m_instanceDirty = false;
    if (!m_textures.empty())
        glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
    m_nodes.clear();
    m_normalMatrices.clear();
    m_meshes.clear();
    m_materials.clear();
    m_textures.clear();
    m_drawNodes.clear();
    m_blendNodes.clear();
    m_shellNodes.clear();
    m_shellOwners.clear();
    m_subtreeEnd.clear();
    m_shellAlpha.clear();
    m_highlighted.clear();
    m_highlightNode = -1;
    m_hovered.clear();
    m_hoverNode = -1;
    m_tint.clear();
    m_partIndex.clear();
    m_stats = Stats();
}

void Model::SetHighlight(int partNode, const glm::vec3& color)
{
    m_highlightNode = (partNode >= 0 && partNode < static_cast<int>(m_nodes.size())) ? partNode : -1;
    m_highlightColor = color;
    std::fill(m_highlighted.begin(), m_highlighted.end(), static_cast<char>(0));
    if (m_highlightNode >= 0)
    {
        // 부모가 항상 자식보다 앞에 있으므로, 앞에서부터 '부모가 강조면 나도 강조'로 한 번 훑으면 된다.
        m_highlighted[m_highlightNode] = 1;
        for (int i = m_highlightNode + 1; i < static_cast<int>(m_nodes.size()); ++i)
        {
            const int parent = m_nodes[i].parent;
            if (parent >= 0 && m_highlighted[parent])
                m_highlighted[i] = 1;
        }
    }
    WriteHighlights();   // 인스턴스 데이터에도(그릴 때 GPU 로 한 번에 올린다)
}

void Model::SetHover(int partNode)
{
    const int node = (partNode >= 0 && partNode < static_cast<int>(m_nodes.size())) ? partNode : -1;
    if (node == m_hoverNode)
        return;
    m_hoverNode = node;
    std::fill(m_hovered.begin(), m_hovered.end(), static_cast<char>(0));
    if (m_hoverNode >= 0)
    {
        m_hovered[m_hoverNode] = 1;
        for (int i = m_hoverNode + 1; i < static_cast<int>(m_nodes.size()); ++i)
        {
            const int parent = m_nodes[i].parent;
            if (parent >= 0 && m_hovered[parent])
                m_hovered[i] = 1;
        }
    }
    WriteHighlights();
}

void Model::SetTints(const std::vector<glm::vec4>& tints)
{
    if (tints.size() == m_nodes.size())
        m_tint = tints;
    else
        m_tint.clear();   // 크기가 다르면(빈 배열 포함) 색 없음
    WriteHighlights();    // 인스턴싱 묶음에도
}

glm::vec4 Model::NodeHighlight(int nodeIndex) const
{
    if (!m_highlighted.empty() && m_highlighted[nodeIndex])
        return glm::vec4(m_highlightColor, 0.65f);                 // 선택(DrawHighlightGhost·인스턴스와 같은 세기)
    const glm::vec4 tint = m_tint.empty() ? glm::vec4(0.0f) : m_tint[nodeIndex];
    if (!m_hovered.empty() && m_hovered[nodeIndex])
    {
        if (tint.a > 0.0f)   // 보기 모드 색이 있는 부품: 그 색을 조금 밝고 진하게
            return glm::vec4(glm::mix(glm::vec3(tint), glm::vec3(1.0f), 0.35f), std::min(tint.a + 0.12f, 0.8f));
        return glm::vec4(1.0f, 1.0f, 1.0f, 0.22f);                  // 마우스: 흰빛으로 살짝
    }
    if (tint.a > 0.0f)
        return tint;                                                 // 보기 모드 색
    return glm::vec4(m_highlightColor, 0.0f);
}

void Model::BuildInstances()
{
    // 1) 메쉬마다 그 메쉬를 쓰는 노드(겉껍데기·반투명 재질 제외). m_drawNodes 가 번호 순이라 노드 번호 순으로 모인다.
    std::vector<std::vector<int>> users(m_meshes.size());
    for (int nodeIndex : m_drawNodes)
    {
        const Node& node = m_nodes[nodeIndex];
        if (node.isShell)
            continue;
        for (int mi : node.meshes)
        {
            const Mesh& mesh = m_meshes[mi];
            if (mesh.indexCount > 0 && !m_materials[mesh.material].blend)
                users[mi].push_back(nodeIndex);
        }
    }

    // 2) 두 노드 이상이 쓰는 메쉬 = 묶음 하나(= 그리기 한 번)
    m_meshGrouped.assign(m_meshes.size(), 0);
    for (size_t mi = 0; mi < m_meshes.size(); ++mi)
    {
        if (users[mi].size() < 2)
            continue;
        InstanceGroup g;
        g.mesh = static_cast<int>(mi);
        g.first = static_cast<int>(m_instanceNodes.size());
        g.count = static_cast<int>(users[mi].size());
        g.boundFirst = g.first;
        m_instanceNodes.insert(m_instanceNodes.end(), users[mi].begin(), users[mi].end());
        m_groups.push_back(g);
        m_meshGrouped[mi] = 1;
    }
    m_stats.instanceGroups = static_cast<int>(m_groups.size());
    m_stats.instancedUses = static_cast<int>(m_instanceNodes.size());

    // 3) 메쉬가 모두 묶음으로 그려지는 노드(인스턴싱 켰을 때 따로 그릴 것이 없다)
    m_nodeAllGrouped.assign(m_nodes.size(), 0);
    for (int nodeIndex : m_drawNodes)
    {
        const Node& node = m_nodes[nodeIndex];
        bool all = !node.isShell;
        for (int mi : node.meshes)
        {
            if (m_meshes[mi].indexCount > 0 && !m_meshGrouped[mi])
                all = false;
        }
        m_nodeAllGrouped[nodeIndex] = all ? 1 : 0;
    }
    if (m_groups.empty())
        return;

    // 4) 인스턴스 데이터: 노드의 월드 행렬·법선 행렬(바뀌지 않음) + 강조(선택할 때마다 바뀜)
    m_instanceData.assign(m_instanceNodes.size() * kInstanceFloats, 0.0f);
    for (size_t k = 0; k < m_instanceNodes.size(); ++k)
    {
        float* out = &m_instanceData[k * kInstanceFloats];
        const int nodeIndex = m_instanceNodes[k];
        std::memcpy(out, glm::value_ptr(m_nodes[nodeIndex].world), 16 * sizeof(float));
        std::memcpy(out + 16, glm::value_ptr(m_normalMatrices[nodeIndex]), 9 * sizeof(float));
    }
    WriteHighlights();
    glGenBuffers(1, &m_instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_instanceData.size() * sizeof(float)), m_instanceData.data(), GL_DYNAMIC_DRAW);
    m_instanceDirty = false;

    // 5) 묶음마다 VAO: 메쉬의 꼭짓점·인덱스 + 인스턴스 버퍼(속성 3~10, 인스턴스마다 한 칸씩 = divisor 1)
    const GLsizei stride = 8 * sizeof(float);
    for (InstanceGroup& g : m_groups)
    {
        const Mesh& mesh = m_meshes[g.mesh];
        glGenVertexArrays(1, &g.vao);
        glBindVertexArray(g.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        for (GLuint loc = kInstanceAttrib; loc <= kInstanceAttrib + 7; ++loc)
        {
            glEnableVertexAttribArray(loc);
            glVertexAttribDivisor(loc, 1);
        }
        PointInstanceAttributes(g.first);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Model::WriteHighlights()
{
    if (m_instanceData.empty())
        return;
    for (size_t k = 0; k < m_instanceNodes.size(); ++k)
    {
        float* h = &m_instanceData[k * kInstanceFloats + kHighlightOffset];
        const glm::vec4 value = NodeHighlight(m_instanceNodes[k]);   // DrawNodeWith 와 같은 값
        h[0] = value.r;
        h[1] = value.g;
        h[2] = value.b;
        h[3] = value.a;
    }
    m_instanceDirty = true;
}

void Model::PointInstanceAttributes(int firstInstance) const
{
    // GL_ARRAY_BUFFER 에 인스턴스 버퍼가, 그 묶음의 VAO 가 연결된 상태에서 부른다.
    const GLsizei stride = kInstanceFloats * sizeof(float);
    const size_t base = static_cast<size_t>(firstInstance) * static_cast<size_t>(stride);
    for (GLuint c = 0; c < 4; ++c)   // 행렬: 열 4개
        glVertexAttribPointer(kInstanceAttrib + c, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(base + c * 4 * sizeof(float)));
    for (GLuint c = 0; c < 3; ++c)   // 법선 행렬: 열 3개
        glVertexAttribPointer(kInstanceAttrib + 4 + c, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(base + (16 + c * 3) * sizeof(float)));
    glVertexAttribPointer(kInstanceAttrib + 7, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(base + kHighlightOffset * sizeof(float)));
}

void Model::DrawGroups(const DrawUniforms& u, int nodeBegin, int nodeEnd, GroupMode mode) const
{
    if (!m_instancing || m_groups.empty() || nodeBegin >= nodeEnd)
        return;
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    if (m_instanceDirty)
    {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(m_instanceData.size() * sizeof(float)), m_instanceData.data());
        m_instanceDirty = false;
    }
    glUniform1i(u.instanced, 1);
    // 엑스레이는 강조 세기 0.65 를 1 로(= DrawHighlightGhost 와 같은 색), ③ 그림은 강조 없음
    glUniform1f(u.instHighlight, mode == GroupMode::NoHighlight ? 0.0f : (mode == GroupMode::Ghost ? 1.0f / 0.65f : 1.0f));
    glUniform1f(u.alpha, mode == GroupMode::Ghost ? 0.30f : 1.0f);
    if (mode == GroupMode::Ghost)
    {
        const glm::vec4 white(1.0f);
        glUniform4fv(u.baseColor, 1, glm::value_ptr(white));
        glUniform1i(u.hasTexture, 0);
    }

    for (const InstanceGroup& g : m_groups)
    {
        // 이 묶음 안에서 노드 번호가 [nodeBegin, nodeEnd) 인 인스턴스(번호 순이라 이어져 있다)
        const auto groupBegin = m_instanceNodes.begin() + g.first;
        const auto groupEnd = groupBegin + g.count;
        const auto lo = std::lower_bound(groupBegin, groupEnd, nodeBegin);
        const auto hi = std::lower_bound(lo, groupEnd, nodeEnd);
        const int count = static_cast<int>(hi - lo);
        if (count <= 0)
            continue;
        const Mesh& mesh = m_meshes[g.mesh];
        if (mode != GroupMode::Ghost)
        {
            const Material& mat = m_materials[mesh.material];
            glUniform4fv(u.baseColor, 1, glm::value_ptr(mat.baseColor));
            glUniform1i(u.hasTexture, mat.texture ? 1 : 0);
            glUniform2fv(u.uvScale, 1, glm::value_ptr(mat.uvScale));
            if (mat.texture)
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mat.texture);
            }
        }
        glBindVertexArray(g.vao);
        const int first = static_cast<int>(lo - m_instanceNodes.begin());
        if (first != g.boundFirst)
        {
            PointInstanceAttributes(first);   // 일부만 그릴 때(③ 그림·엑스레이): 그 시작 위치로
            g.boundFirst = first;
        }
        glDrawElementsInstanced(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr, count);
        ++m_drawCalls;
    }
    glUniform1i(u.instanced, 0);
}

bool Model::UnderClosedShell(int partNode) const
{
    // 위로 올라가며(모듈 → 배터리팩 → 차량) 겉껍데기가 있는 부품의 투명도를 본다.
    // 하나라도 불투명하면 그 안은 밖에서 안 보이는 상태다(예: 닫힌 차체 안의 배터리 모듈).
    for (int part = partNode; part >= 0;)
    {
        const int parent = m_nodes[part].parent;
        part = parent >= 0 ? m_nodes[parent].part : -1;
        if (part >= 0 && std::binary_search(m_shellOwners.begin(), m_shellOwners.end(), part) &&
            ShellAlpha(part) >= 0.999f)
            return true;
    }
    return false;
}

void Model::DrawHighlightGhost(const DrawUniforms& u) const
{
    // 닫힌 차 안의 부품까지 비추면 차 옆면에 강조색 네모가 스티커처럼 보인다 → 겉껍데기가 흐려진 뒤에만 비춘다.
    if (m_highlightNode < 0 || m_nodes[m_highlightNode].level == 0 || UnderClosedShell(m_highlightNode))
        return;
    // 깊이가 '더 먼' 곳에서만 그린다 = 다른 물체 뒤에 가려진 부분. 보이는 부분은 이미 물들어 있다.
    glDepthFunc(GL_GREATER);
    glDepthMask(GL_FALSE);
    const glm::vec4 white(1.0f);
    for (int nodeIndex : m_drawNodes)
    {
        if (!m_highlighted[nodeIndex])
            continue;
        glUniformMatrix4fv(u.model, 1, GL_FALSE, glm::value_ptr(m_nodes[nodeIndex].world));
        glUniformMatrix3fv(u.normalMatrix, 1, GL_FALSE, glm::value_ptr(m_normalMatrices[nodeIndex]));
        glUniform1f(u.alpha, 0.30f);
        glUniform4f(u.highlight, m_highlightColor.r, m_highlightColor.g, m_highlightColor.b, 1.0f);
        glUniform4fv(u.baseColor, 1, glm::value_ptr(white));
        glUniform1i(u.hasTexture, 0);
        for (int mi : m_nodes[nodeIndex].meshes)
        {
            const Mesh& mesh = m_meshes[mi];
            if (mesh.indexCount == 0 || Grouped(nodeIndex, mi))
                continue;
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
            ++m_drawCalls;
        }
    }
    // 묶음(인스턴싱)에 들어간 메쉬: 고른 부품 아래(노드 번호 구간)만 한 번에
    DrawGroups(u, m_highlightNode, m_subtreeEnd[m_highlightNode], GroupMode::Ghost);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
}

void Model::DrawIds(int locModel, int locIdColor, const std::vector<char>& skip) const
{
    for (int nodeIndex : m_drawNodes)
    {
        if (NodeAlpha(nodeIndex) < 0.999f)   // 흐려진 겉껍데기는 건너뛴다 → 뒤의 부품이 잡힌다
            continue;
        if (nodeIndex < static_cast<int>(skip.size()) && skip[nodeIndex])
            continue;
        const unsigned int id = static_cast<unsigned int>(nodeIndex) + 1;   // 0 은 배경
        glUniform3f(locIdColor, static_cast<float>(id & 0xFF) / 255.0f,
                    static_cast<float>((id >> 8) & 0xFF) / 255.0f,
                    static_cast<float>((id >> 16) & 0xFF) / 255.0f);
        glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(m_nodes[nodeIndex].world));
        for (int mi : m_nodes[nodeIndex].meshes)
        {
            const Mesh& mesh = m_meshes[mi];
            if (mesh.indexCount == 0)
                continue;
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        }
    }
}

void Model::SetShellAlpha(int partNode, float alpha)
{
    if (partNode >= 0 && partNode < static_cast<int>(m_shellAlpha.size()))
        m_shellAlpha[partNode] = std::min(std::max(alpha, 0.0f), 1.0f);
}

float Model::ShellAlpha(int partNode) const
{
    return (partNode >= 0 && partNode < static_cast<int>(m_shellAlpha.size())) ? m_shellAlpha[partNode] : 1.0f;
}

float Model::NodeAlpha(int nodeIndex) const
{
    const Node& node = m_nodes[nodeIndex];
    return node.isShell ? ShellAlpha(node.part) : 1.0f;
}

float Model::Radius() const
{
    return glm::length(m_max - m_min) * 0.5f;
}

int Model::FindPart(const std::string& partNumber) const
{
    const auto found = m_partIndex.find(partNumber);
    return found == m_partIndex.end() ? -1 : found->second;
}

void Model::Draw(const DrawUniforms& u, bool transparentPass, const glm::vec3& eye) const
{
    if (!transparentPass)
    {
        for (int nodeIndex : m_drawNodes)
        {
            if (m_instancing && m_nodeAllGrouped[nodeIndex])
                continue;   // 이 노드의 메쉬는 모두 아래 묶음이 그린다
            DrawNode(nodeIndex, u, false);
        }
        DrawGroups(u, 0, static_cast<int>(m_nodes.size()), GroupMode::Normal);
        return;
    }

    // 반투명은 카메라에서 먼 것부터 그려야 뒤쪽이 비쳐 보인다.
    std::vector<std::pair<float, int>> order;
    order.reserve(m_blendNodes.size() + m_shellNodes.size());
    auto add = [&](int nodeIndex) {
        const glm::vec3 d = glm::vec3(m_nodes[nodeIndex].world[3]) - eye;
        order.push_back({ glm::dot(d, d), nodeIndex });
    };
    for (int nodeIndex : m_blendNodes)
    {
        if (NodeAlpha(nodeIndex) >= 0.999f)   // 흐려진 겉껍데기는 아래에서 한 번만
            add(nodeIndex);
    }
    for (int nodeIndex : m_shellNodes)
    {
        if (NodeAlpha(nodeIndex) < 0.999f)
            add(nodeIndex);
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& item : order)
    {
        // 흐려진 겉껍데기는 깊이를 남기지 않아 앞·뒷면과 속 부품이 함께 비친다.
        // (깊이를 안 남기므로 커서 줌도 껍데기를 지나 속 부품을 잡는다)
        glDepthMask(NodeAlpha(item.second) < 0.999f ? GL_FALSE : GL_TRUE);
        DrawNode(item.second, u, true);
    }
    glDepthMask(GL_TRUE);
}

void Model::DrawPart(const DrawUniforms& u, int partNode, bool transparentPass, const glm::vec3& eye, float shellAlpha) const
{
    if (partNode < 0 || partNode >= static_cast<int>(m_nodes.size()))
        return;
    // 이 부품과 그 아래 = 노드 번호 [partNode, m_subtreeEnd[partNode]). m_drawNodes 는 번호 순이라 그 구간만 고른다.
    const auto first = std::lower_bound(m_drawNodes.begin(), m_drawNodes.end(), partNode);
    const auto last = std::lower_bound(first, m_drawNodes.end(), m_subtreeEnd[partNode]);
    auto alphaOf = [&](int nodeIndex) { return m_nodes[nodeIndex].isShell ? shellAlpha : 1.0f; };

    if (!transparentPass)
    {
        for (auto it = first; it != last; ++it)
        {
            if (m_instancing && m_nodeAllGrouped[*it])
                continue;
            DrawNodeWith(*it, u, false, alphaOf(*it), false);
        }
        DrawGroups(u, partNode, m_subtreeEnd[partNode], GroupMode::NoHighlight);   // 강조색 없이
        return;
    }

    // 반투명(유리창 등)과 흐린 겉껍데기: 카메라에서 먼 것부터
    std::vector<std::pair<float, int>> order;
    for (auto it = first; it != last; ++it)
    {
        const int nodeIndex = *it;
        if (alphaOf(nodeIndex) < 0.999f || std::binary_search(m_blendNodes.begin(), m_blendNodes.end(), nodeIndex))
        {
            const glm::vec3 d = glm::vec3(m_nodes[nodeIndex].world[3]) - eye;
            order.push_back({ glm::dot(d, d), nodeIndex });
        }
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& item : order)
    {
        const float alpha = alphaOf(item.second);
        glDepthMask(alpha < 0.999f ? GL_FALSE : GL_TRUE);
        DrawNodeWith(item.second, u, true, alpha, false);
    }
    glDepthMask(GL_TRUE);
}

void Model::DrawNode(int nodeIndex, const DrawUniforms& u, bool transparentPass) const
{
    DrawNodeWith(nodeIndex, u, transparentPass, NodeAlpha(nodeIndex), true);
}

void Model::DrawNodeWith(int nodeIndex, const DrawUniforms& u, bool transparentPass, float alpha, bool useHighlight) const
{
    const Node& node = m_nodes[nodeIndex];
    const bool fadeThis = alpha < 0.999f;
    bool matricesSet = false;
    for (int mi : node.meshes)
    {
        const Mesh& mesh = m_meshes[mi];
        if (mesh.indexCount == 0)
            continue;
        const Material& mat = m_materials[mesh.material];
        if ((fadeThis || mat.blend) != transparentPass)
            continue;
        if (Grouped(nodeIndex, mi))
            continue;   // 인스턴싱 묶음이 그린다

        if (!matricesSet)
        {
            glUniformMatrix4fv(u.model, 1, GL_FALSE, glm::value_ptr(node.world));
            glUniformMatrix3fv(u.normalMatrix, 1, GL_FALSE, glm::value_ptr(m_normalMatrices[nodeIndex]));
            glUniform1f(u.alpha, alpha);
            const glm::vec4 h = useHighlight ? NodeHighlight(nodeIndex) : glm::vec4(m_highlightColor, 0.0f);
            glUniform4f(u.highlight, h.r, h.g, h.b, h.a);
            matricesSet = true;
        }
        glUniform4fv(u.baseColor, 1, glm::value_ptr(mat.baseColor));
        glUniform1i(u.hasTexture, mat.texture ? 1 : 0);
        glUniform2fv(u.uvScale, 1, glm::value_ptr(mat.uvScale));
        if (mat.texture)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mat.texture);
        }
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        ++m_drawCalls;
    }
}
