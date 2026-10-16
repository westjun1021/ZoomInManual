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
    m_highlightNode = -1;
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
    m_partIndex.clear();
    m_stats = Stats();
}

void Model::SetHighlight(int partNode, const glm::vec3& color)
{
    m_highlightNode = (partNode >= 0 && partNode < static_cast<int>(m_nodes.size())) ? partNode : -1;
    m_highlightColor = color;
    std::fill(m_highlighted.begin(), m_highlighted.end(), static_cast<char>(0));
    if (m_highlightNode < 0)
        return;
    // 부모가 항상 자식보다 앞에 있으므로, 앞에서부터 '부모가 강조면 나도 강조'로 한 번 훑으면 된다.
    m_highlighted[m_highlightNode] = 1;
    for (int i = m_highlightNode + 1; i < static_cast<int>(m_nodes.size()); ++i)
    {
        const int parent = m_nodes[i].parent;
        if (parent >= 0 && m_highlighted[parent])
            m_highlighted[i] = 1;
    }
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
            if (mesh.indexCount == 0)
                continue;
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        }
    }
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
            DrawNode(nodeIndex, u, false);
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
            DrawNodeWith(*it, u, false, alphaOf(*it), false);
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
    const bool lit = !m_highlighted.empty() && m_highlighted[nodeIndex];
    DrawNodeWith(nodeIndex, u, transparentPass, NodeAlpha(nodeIndex), lit);
}

void Model::DrawNodeWith(int nodeIndex, const DrawUniforms& u, bool transparentPass, float alpha, bool lit) const
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

        if (!matricesSet)
        {
            glUniformMatrix4fv(u.model, 1, GL_FALSE, glm::value_ptr(node.world));
            glUniformMatrix3fv(u.normalMatrix, 1, GL_FALSE, glm::value_ptr(m_normalMatrices[nodeIndex]));
            glUniform1f(u.alpha, alpha);
            glUniform4f(u.highlight, m_highlightColor.r, m_highlightColor.g, m_highlightColor.b, lit ? 0.65f : 0.0f);
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
    }
}
