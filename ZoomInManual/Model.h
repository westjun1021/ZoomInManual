// Model.h
// GLB 모델을 Assimp 로 읽어 GPU 에 올리고 그린다. 순수 OpenGL + Assimp 코드(MFC 와 상관없음).
//
// 노드 하나 = Blender 객체 하나.
//   - 이름이 품번인 빈 객체(예: EV-BM-12S@03)가 '부품'이고, 그 아래 메쉬 노드가 모양이다.
//   - 이름에 __shell 이 들어간 메쉬는 겉껍데기. 바로 위 부품(Node::part)의 투명도로 그린다.
//   - 같은 메쉬 번호를 여러 노드가 쓰면 인스턴싱 대상(셀 270개 등).
// 3주차: 노드마다 아래 메쉬 전체의 경계 상자(시맨틱 줌의 경계구), 부품별 겉껍데기 투명도.
// 4주차: 클릭 식별용 ID 그리기(DrawIds), 선택한 부품 강조색(SetHighlight).
// 5주차(새 안): 화면 ③ 부품 이미지용으로 부품 하나만 그리기(DrawPart).
//              닫힌 차체·커버 안에 있는 부품은 엑스레이 강조를 그리지 않는다(차 옆면에 주황 네모가 비치던 것).
#pragma once
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

class Model
{
public:
    struct Node
    {
        std::string name;          // 예: EV-BM-12S@03, EV-BP-001__shell.001, EV-BM-12S@03_mesh.012
        int parent = -1;
        glm::mat4 world{ 1.0f };   // 월드 행렬(부모 행렬을 모두 곱한 것)
        std::vector<int> meshes;   // 이 노드가 그리는 메쉬 번호
        bool isPart = false;       // 품번 빈 객체인지
        bool isShell = false;      // 겉껍데기 메쉬인지
        int part = -1;             // 이 노드가 속한 가장 가까운 부품 노드 번호(부품이면 자기 자신)
        int level = -1;            // 부품 단계: EV-VEH-001 = 0(L0), 그 아래 = 1(L1) …  부품이 아니면 -1
        bool hasBounds = false;    // 아래에 메쉬가 하나라도 있는지
        glm::vec3 boundsMin{ 0.0f }, boundsMax{ 0.0f };   // 이 노드와 그 아래 메쉬 전체의 월드 경계 상자
    };

    struct Stats
    {
        int parts = 0;             // 품번 빈 객체 수
        int meshNodes = 0;         // 메쉬를 그리는 노드 수(= 그리기 호출 수)
        int uniqueMeshes = 0;      // 서로 다른 메쉬 수
        long long triangles = 0;   // 화면에 그리는 삼각형 수
        int textures = 0;
        double loadSeconds = 0.0;
    };

    struct DrawUniforms            // GLRenderer 셰이더의 uniform 위치
    {
        int model = -1;
        int normalMatrix = -1;
        int baseColor = -1;
        int hasTexture = -1;
        int uvScale = -1;
        int alpha = -1;
        int highlight = -1;        // vec4: 강조색(rgb) + 세기(a)
    };

    // OpenGL 컨텍스트가 현재 스레드에 연결된 상태에서 호출한다. path 는 UTF-8.
    bool Load(const std::string& path, std::string* error);
    void Release();
    bool Loaded() const { return !m_nodes.empty(); }

    // transparentPass = false 면 불투명 재질만, true 면 반투명(BLEND) 재질과 흐려진 겉껍데기를 먼 것부터 그린다.
    void Draw(const DrawUniforms& u, bool transparentPass, const glm::vec3& eye) const;

    // 클릭 식별: 메쉬 노드마다 색 = 노드 번호 + 1 (R + G·256 + B·65536, 0 = 배경).
    // 흐려진 겉껍데기와 skip[노드] 가 1 인 노드는 그리지 않는다(뒤의 속 부품이 클릭되도록).
    void DrawIds(int locModel, int locIdColor, const std::vector<char>& skip) const;

    // 선택한 부품(노드 번호)과 그 아래 전체를 color 쪽으로 물들인다. partNode = -1 이면 강조 없음.
    void SetHighlight(int partNode, const glm::vec3& color);
    int Highlighted() const { return m_highlightNode; }
    // 선택한 부품 중 다른 물체에 가려진 부분을 강조색으로 옅게 비춰 그린다(엑스레이). Draw 두 번 다음에 부른다.
    // 블렌딩은 켜 둔 상태로 부른다. 차량 전체(L0)를 골랐을 때와, 위 부품의 겉껍데기(차체·배터리 커버)가
    // 아직 불투명할 때(= 닫힌 차 안의 부품)는 그리지 않는다.
    void DrawHighlightGhost(const DrawUniforms& u) const;

    // 화면 ③ 부품 이미지: partNode 와 그 아래 메쉬만 그린다(강조색 없음, 겉껍데기 투명도 = shellAlpha).
    // transparentPass 는 Draw 와 같다: false = 불투명 재질, true = 반투명 재질·흐린 겉껍데기를 먼 것부터.
    void DrawPart(const DrawUniforms& u, int partNode, bool transparentPass, const glm::vec3& eye, float shellAlpha) const;

    // 겉껍데기 투명도(부품 노드 번호마다 하나, 1 = 불투명). 시맨틱 줌이 매 프레임 넣는다.
    void SetShellAlpha(int partNode, float alpha);
    float ShellAlpha(int partNode) const;
    const std::vector<int>& ShellOwners() const { return m_shellOwners; }   // 겉껍데기를 가진 부품 노드

    const std::vector<Node>& Nodes() const { return m_nodes; }
    const Stats& GetStats() const { return m_stats; }
    int FindPart(const std::string& partNumber) const;   // "EV-BM-12S@03" → 노드 번호, 없으면 -1

    glm::vec3 BoundsMin() const { return m_min; }
    glm::vec3 BoundsMax() const { return m_max; }
    glm::vec3 Center() const { return (m_min + m_max) * 0.5f; }
    float Radius() const;

private:
    struct Mesh
    {
        unsigned int vao = 0, vbo = 0, ebo = 0;
        int indexCount = 0;
        int material = 0;
        glm::vec3 boundsMin{ 0.0f }, boundsMax{ 0.0f };
    };

    struct Material
    {
        glm::vec4 baseColor{ 1.0f };
        unsigned int texture = 0;
        glm::vec2 uvScale{ 1.0f };
        bool blend = false;        // glTF alphaMode = BLEND (유리창, 물 등)
    };

    void DrawNode(int nodeIndex, const DrawUniforms& u, bool transparentPass) const;
    // 노드 하나를 투명도 alpha, 강조(lit) 여부를 정해서 그린다(DrawNode·DrawPart 공용)
    void DrawNodeWith(int nodeIndex, const DrawUniforms& u, bool transparentPass, float alpha, bool lit) const;
    float NodeAlpha(int nodeIndex) const;      // 겉껍데기면 그 부품의 투명도, 아니면 1
    bool UnderClosedShell(int partNode) const; // 위 부품 중 겉껍데기가 아직 불투명한 것이 있는지

    std::vector<Node> m_nodes;
    std::vector<glm::mat3> m_normalMatrices;   // 노드별 법선 행렬
    std::vector<Mesh> m_meshes;
    std::vector<Material> m_materials;
    std::vector<unsigned int> m_textures;
    std::vector<int> m_drawNodes;              // 메쉬가 있는 노드
    std::vector<int> m_blendNodes;             // 반투명 메쉬가 있는 노드
    std::vector<int> m_shellNodes;             // 겉껍데기 메쉬 노드
    std::vector<int> m_shellOwners;            // 겉껍데기를 가진 부품 노드(EV-VEH-001, EV-BP-001), 번호 순
    std::vector<int> m_subtreeEnd;             // 노드 i 와 그 아래 전체 = 노드 번호 [i, m_subtreeEnd[i])
    std::vector<float> m_shellAlpha;           // 노드 번호 → 그 부품 겉껍데기의 투명도
    std::vector<char> m_highlighted;           // 노드 번호 → 강조할지(선택한 부품 아래면 1)
    int m_highlightNode = -1;
    glm::vec3 m_highlightColor{ 0.2f, 0.65f, 1.0f };
    std::unordered_map<std::string, int> m_partIndex;
    glm::vec3 m_min{ 0.0f }, m_max{ 0.0f };
    Stats m_stats;
};
