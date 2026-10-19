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
// 6주차: 인스턴싱 — 같은 메쉬를 쓰는 노드(셀 270개의 메쉬 2종 등)를 메쉬마다 그리기 한 번으로 묶는다.
//        노드마다 다른 것(행렬·법선 행렬·강조색)은 '인스턴스 속성'(꼭짓점 속성 3~10, 인스턴스마다 한 벌)으로 넘긴다.
//        겉껍데기(투명도가 바뀜)와 반투명 재질은 묶지 않고 전처럼 따로 그린다. SetInstancing(false) = 전처럼(비교용).
// 7주차(UI 다듬기): 마우스를 올린 부품을 흰빛으로 살짝 밝게(SetHover). 선택 강조가 있으면 선택이 먼저다.
// 7주차 3차: 보기 모드 색(SetTints) — 노드마다 색 한 벌(고전압 부품 주황, 재고 품절 빨강·부족 노랑 등).
//        순서: 선택 > 마우스(색이 있으면 그 색을 밝게) > 보기 모드 색 > 없음. 인스턴싱 묶음에도 같은 값.
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
        int meshNodes = 0;         // 메쉬를 그리는 노드 수(= 인스턴싱 없이 그릴 때의 그리기 호출 수)
        int uniqueMeshes = 0;      // 서로 다른 메쉬 수
        int instanceGroups = 0;    // 인스턴싱 묶음 수(두 노드 이상이 쓰는 메쉬)
        int instancedUses = 0;     // 묶음이 대신 그리는 '노드의 메쉬' 수(인스턴싱을 끄면 이만큼 하나씩 그린다)
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
        int instanced = -1;        // bool: 이번 그리기는 인스턴싱(행렬·강조를 인스턴스 속성에서 읽는다)
        int instHighlight = -1;    // float: 인스턴스 강조 세기에 곱할 값(1 = 그대로, 0 = 강조 없음)
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
    // 마우스를 올린 부품(과 그 아래)을 살짝 밝게. -1 = 없음. 인스턴싱 묶음에도 그대로 들어간다.
    void SetHover(int partNode);
    int Hovered() const { return m_hoverNode; }
    // 보기 모드 색: tints[노드] = rgb 색 + a 세기(0 = 없음). 빈 배열 = 모두 없음(기본 보기).
    void SetTints(const std::vector<glm::vec4>& tints);
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

    // 6주차: 인스턴싱 켬/끔(I 키). 끄면 5주차와 똑같이 노드마다 따로 그린다(전후 비교용).
    void SetInstancing(bool on) { m_instancing = on; }
    bool Instancing() const { return m_instancing; }
    // 그리기 호출 수 세기: 화면 한 장 그리기 전에 0으로, 그린 뒤 읽는다(Draw·DrawHighlightGhost·DrawPart).
    void ResetDrawCalls() const { m_drawCalls = 0; }
    int DrawCalls() const { return m_drawCalls; }
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

    struct InstanceGroup           // 같은 메쉬를 쓰는 노드 묶음 = 그리기 한 번
    {
        int mesh = -1;
        int first = 0;             // m_instanceNodes 안 시작 위치(묶음 안은 노드 번호 순)
        int count = 0;
        unsigned int vao = 0;      // 메쉬 꼭짓점(0~2) + 인스턴스 속성(3~10)
        mutable int boundFirst = 0;   // 인스턴스 속성이 지금 가리키는 시작 위치
    };
    enum class GroupMode { Normal, NoHighlight, Ghost };

    void BuildInstances();         // Load 끝에서: 묶음·인스턴스 버퍼·VAO
    void WriteHighlights();        // 인스턴스 데이터의 강조 값을 m_highlighted 대로
    void PointInstanceAttributes(int firstInstance) const;   // 묶인 VAO 의 속성 3~10 을 firstInstance 부터로
    // 노드 번호 [nodeBegin, nodeEnd) 안의 인스턴스만 묶음마다 한 번에 그린다.
    // Normal = 강조 그대로, NoHighlight = 강조 없음(③ 그림), Ghost = 엑스레이(흰 바탕 + 강조색 100%, α 0.30)
    void DrawGroups(const DrawUniforms& u, int nodeBegin, int nodeEnd, GroupMode mode) const;
    bool Grouped(int nodeIndex, int meshIndex) const   // 이 노드의 이 메쉬는 묶음이 대신 그리는지
    {
        return m_instancing && !m_nodes[nodeIndex].isShell && m_meshGrouped[meshIndex];
    }

    void DrawNode(int nodeIndex, const DrawUniforms& u, bool transparentPass) const;
    // 노드 하나를 투명도 alpha 로 그린다. useHighlight = 이 노드의 강조(선택·마우스)를 칠할지(③ 그림은 false)
    void DrawNodeWith(int nodeIndex, const DrawUniforms& u, bool transparentPass, float alpha, bool useHighlight) const;
    glm::vec4 NodeHighlight(int nodeIndex) const;   // 이 노드의 강조: rgb = 색, a = 세기(선택 0.65, 마우스 0.22, 없음 0)
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
    std::vector<char> m_hovered;               // 노드 번호 → 마우스를 올린 부품 아래면 1
    std::vector<glm::vec4> m_tint;             // 노드 번호 → 보기 모드 색(비었으면 없음)

    // 인스턴싱(6주차)
    std::vector<InstanceGroup> m_groups;
    std::vector<int> m_instanceNodes;          // 묶음마다 이어 붙인 노드 번호
    std::vector<char> m_meshGrouped;           // 메쉬 번호 → 묶음에 들어갔는지
    std::vector<char> m_nodeAllGrouped;        // 노드 번호 → 메쉬가 모두 묶음으로 그려지는지(따로 그릴 것 없음)
    std::vector<float> m_instanceData;         // 인스턴스마다 29개: 행렬 16 + 법선 행렬 9 + 강조 4
    unsigned int m_instanceVbo = 0;
    mutable bool m_instanceDirty = false;      // 강조가 바뀌어 GPU 에 다시 올려야 함
    bool m_instancing = true;
    mutable int m_drawCalls = 0;
    int m_highlightNode = -1;
    int m_hoverNode = -1;
    glm::vec3 m_highlightColor{ 0.2f, 0.65f, 1.0f };
    std::unordered_map<std::string, int> m_partIndex;
    glm::vec3 m_min{ 0.0f }, m_max{ 0.0f };
    Stats m_stats;
};
