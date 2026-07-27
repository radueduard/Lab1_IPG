#pragma once

#include <koral.h>

#include <variant>
#include <vector>

class ISC_Lab1 final : public kor::Scene
{
public:
    void Initialize() override;
    void Render(kor::CommandBuffer& commandBuffer) override;
    void RenderUI() override;

    static constexpr glm::uvec2 CANVAS_SIZE { 1280, 720 };

    struct Circle
    {
        glm::vec4 color;
        glm::ivec2 center;
        float radius;
        glm::uint antiAliasing;
    };

    struct Rectangle
    {
        glm::vec4 color;
        glm::ivec2 corner0;
        glm::ivec2 corner1;
        glm::uint antiAliasing;
    };

    struct Line
    {
        glm::vec4 color;
        glm::ivec2 start;
        glm::ivec2 end;
        float thickness;
        glm::uint antiAliasing;
    };

    struct Triangle
    {
        glm::vec4 color;
        glm::ivec2 vertex0;
        glm::ivec2 vertex1;
        glm::ivec2 vertex2;
        glm::uint antiAliasing;
    };

    static_assert(offsetof(Circle, center) == 16 && offsetof(Circle, antiAliasing) == 28);
    static_assert(offsetof(Rectangle, corner1) == 24 && offsetof(Rectangle, antiAliasing) == 32);
    static_assert(offsetof(Line, thickness) == 32 && offsetof(Line, antiAliasing) == 36);
    static_assert(offsetof(Triangle, vertex2) == 32 && offsetof(Triangle, antiAliasing) == 40);

    kor::Resource<kor::Image> renderTarget;
    kor::Resource<kor::ImageView> renderTargetView;

    kor::Resource<kor::ComputePipeline> circlePipeline;
    kor::Resource<kor::ComputePipeline> rectanglePipeline;
    kor::Resource<kor::ComputePipeline> linePipeline;
    kor::Resource<kor::ComputePipeline> trianglePipeline;

    kor::Resource<kor::DescriptorSet> descriptorSet;

    std::vector<std::variant<Circle, Rectangle, Line, Triangle>> shapes;

private:
    int selectedShape = -1;
    bool debugOutlines = false;

    // Read from the docked window a frame after it exists, so the default layout can be
    // built against whatever node the engine's dockspace put it in.
    ImGuiID shapesDockId = 0;
    ImGuiID propertiesDockId = 0;
    bool dockLayoutBuilt = false;
};
