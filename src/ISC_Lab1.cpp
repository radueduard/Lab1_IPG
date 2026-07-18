#include "ISC_Lab1.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/type_ptr.hpp>

#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

namespace
{
    using Shape = decltype(ISC_Lab1::shapes)::value_type;

    constexpr const char* SHAPE_TYPE_NAMES[] = { "Circle", "Rectangle", "Line", "Triangle" };
    static_assert(IM_ARRAYSIZE(SHAPE_TYPE_NAMES) == std::variant_size_v<Shape>);

    constexpr ImVec2 ROW_PADDING { 8.f, 5.f };
    constexpr float ROW_ROUNDING = 4.f;
    constexpr float ROW_SPACING = 4.f;

    // Transparent at rest so the delete button reads as part of the selected row it sits on,
    // then destructive red once the cursor is actually on it.
    constexpr ImVec4 DELETE_IDLE { 0.f, 0.f, 0.f, 0.f };
    constexpr ImVec4 DELETE_HOVERED { 0.78f, 0.24f, 0.24f, 1.f };
    constexpr ImVec4 DELETE_ACTIVE { 0.56f, 0.14f, 0.14f, 1.f };

    constexpr ImWchar TRASH_GLYPH = 0xf1f8;  // ICON_FA_TRASH

    constexpr ImU32 OUTLINE_COLOR = IM_COL32(255, 0, 255, 255);
    constexpr float OUTLINE_THICKNESS = 2.f;

    constexpr glm::ivec2 CENTER { ISC_Lab1::CANVAS_SIZE.x / 2, ISC_Lab1::CANVAS_SIZE.y / 2 };
    constexpr int UNIT = 120;

    Shape MakeShape(const int type)
    {
        switch (type)
        {
        case 1:
            return ISC_Lab1::Rectangle {
                { 0.35f, 0.75f, 0.45f, 1.f },
                CENTER - glm::ivec2 { UNIT, UNIT },
                CENTER + glm::ivec2 { UNIT, UNIT },
                1 };

        case 2:
            return ISC_Lab1::Line {
                { 0.95f, 0.85f, 0.40f, 1.f },
                CENTER - glm::ivec2 { UNIT, UNIT },
                CENTER + glm::ivec2 { UNIT, UNIT },
                4.f,
                1 };

        case 3:
            return ISC_Lab1::Triangle {
                { 0.40f, 0.60f, 0.95f, 1.f },
                CENTER + glm::ivec2 { 0, -UNIT },
                CENTER + glm::ivec2 { UNIT, UNIT },
                CENTER + glm::ivec2 { -UNIT, UNIT },
                1 };

        default:
            return ISC_Lab1::Circle {
                { 0.91f, 0.30f, 0.35f, 1.f },
                CENTER,
                UNIT,
                1 };
        }
    }

    // Traces the shape's boundary with ImGui's CPU rasteriser. Deliberately not the compute
    // shaders' own SDF: a reference that shared their code would reproduce a student's bug and
    // agree with a wrong image. These primitives are an independent second opinion.
    void DrawOutline(const Shape& shape, ImDrawList& drawList, const ImVec2& origin, const ImVec2& scale)
    {
        const auto toScreen = [&](const glm::ivec2 p) {
            return ImVec2 { origin.x + static_cast<float>(p.x) * scale.x,
                            origin.y + static_cast<float>(p.y) * scale.y };
        };

        std::visit([&](const auto& s) {
            using T = std::decay_t<decltype(s)>;

            if constexpr (std::is_same_v<T, ISC_Lab1::Circle>)
            {
                drawList.AddCircle(toScreen(s.center), s.radius * scale.x, OUTLINE_COLOR, 0, OUTLINE_THICKNESS);
            }
            else if constexpr (std::is_same_v<T, ISC_Lab1::Rectangle>)
            {
                // Normalised to match the shader, which does the same rather than trusting order.
                drawList.AddRect(toScreen(glm::min(s.corner0, s.corner1)),
                                 toScreen(glm::max(s.corner0, s.corner1)),
                                 OUTLINE_COLOR, 0.f, 0, OUTLINE_THICKNESS);
            }
            else if constexpr (std::is_same_v<T, ISC_Lab1::Line>)
            {
                // A thick line is a capsule, so the outline is two parallel edges closed by a
                // semicircle at each end - which is exactly what the segment distance produces.
                const ImVec2 a = toScreen(s.start);
                const ImVec2 b = toScreen(s.end);
                const float radius = s.thickness * 0.5f * scale.x;
                const float dx = b.x - a.x;
                const float dy = b.y - a.y;

                if (dx * dx + dy * dy <= 0.f)
                {
                    drawList.AddCircle(a, radius, OUTLINE_COLOR, 0, OUTLINE_THICKNESS);
                    return;
                }

                const float angle = std::atan2(dy, dx);
                drawList.PathArcTo(a, radius, angle + IM_PI * 0.5f, angle + IM_PI * 1.5f);
                drawList.PathArcTo(b, radius, angle - IM_PI * 0.5f, angle + IM_PI * 0.5f);
                drawList.PathStroke(OUTLINE_COLOR, ImDrawFlags_Closed, OUTLINE_THICKNESS);
            }
            else
            {
                drawList.AddTriangle(toScreen(s.vertex0), toScreen(s.vertex1), toScreen(s.vertex2),
                                     OUTLINE_COLOR, OUTLINE_THICKNESS);
            }
        }, shape);
    }
}

void ISC_Lab1::Initialize()
{
    const auto circleShader = kor::Shader::Builder()
        .setLang<kor::Shader::Lang::eSlang>()
        .setEntryPoint("shapes", "drawCircle")
        .getOrBuild();

    const auto rectangleShader = kor::Shader::Builder()
        .setLang<kor::Shader::Lang::eSlang>()
        .setEntryPoint("shapes", "drawRectangle")
        .getOrBuild();

    const auto lineShader = kor::Shader::Builder()
        .setLang<kor::Shader::Lang::eSlang>()
        .setEntryPoint("shapes", "drawLine")
        .getOrBuild();

    const auto triangleShader = kor::Shader::Builder()
        .setLang<kor::Shader::Lang::eSlang>()
        .setEntryPoint("shapes", "drawTriangle")
        .getOrBuild();

    circlePipeline = kor::ComputePipeline::Builder()
        .setComputeShader(circleShader)
        .build();

    rectanglePipeline = kor::ComputePipeline::Builder()
        .setComputeShader(rectangleShader)
        .build();

    linePipeline = kor::ComputePipeline::Builder()
        .setComputeShader(lineShader)
        .build();

    trianglePipeline = kor::ComputePipeline::Builder()
        .setComputeShader(triangleShader)
        .build();

    renderTarget = kor::Image::Builder()
        .setIsPerFrame(true)
        .setFormat(kor::Image::Format::eRGBA8_UNORM)
        .setExtent(CANVAS_SIZE)
        .addUsage(kor::Image::Usage::eStorage)
        .addUsage(kor::Image::Usage::eTransferSrc)
        .addUsage(kor::Image::Usage::eTransferDst)
        .build();

    renderTargetView = kor::ImageView::Builder(renderTarget).build();

    descriptorSet = kor::DescriptorSet::Builder(circlePipeline->getSetLayout(0))
        .write(0, kor::Descriptor(renderTargetView))
        .build();

    shapes.push_back(MakeShape(0));
    shapes.push_back(MakeShape(1));
    selectedShape = 0;
}

void ISC_Lab1::Render(kor::CommandBuffer& commandBuffer)
{
    commandBuffer
        .ClearColorImage(renderTarget)
        .ForEach(shapes, [this](auto& cb, const auto& shape) {
            cb.ImageBarrier({ renderTarget, kor::ResourceAccess::ComputeReadWrite });
            std::visit([&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, Circle>)
                    cb.BindComputePipeline(circlePipeline);
                else if constexpr (std::is_same_v<T, Rectangle>)
                    cb.BindComputePipeline(rectanglePipeline);
                else if constexpr (std::is_same_v<T, Line>)
                    cb.BindComputePipeline(linePipeline);
                else
                    cb.BindComputePipeline(trianglePipeline);
                cb.BindDescriptorSet(0, descriptorSet);
                cb.PushConstants(s);
            }, shape);
            cb.Dispatch(CANVAS_SIZE.x / 8, CANVAS_SIZE.y / 8);
        })
        .Blit(renderTarget);
}

void ISC_Lab1::RenderUI(ImGuiContext* context)
{
    // Split the node the engine's dockspace put "Shapes" in, one frame after that node is
    // known. Skipped entirely when "Properties" already has a dock id, so a layout restored
    // from imgui.ini wins over this default.
    if (!dockLayoutBuilt && shapesDockId != 0)
    {
        dockLayoutBuilt = true;

        if (propertiesDockId == 0)
        {
            ImGuiID top, bottom;
            ImGui::DockBuilderSplitNode(shapesDockId, ImGuiDir_Down, 0.45f, &bottom, &top);
            ImGui::DockBuilderDockWindow("Shapes", top);
            ImGui::DockBuilderDockWindow("Properties", bottom);
            ImGui::DockBuilderFinish(shapesDockId);
        }
    }

    ImGui::Begin("Shapes");
    shapesDockId = ImGui::GetWindowDockID();

    if (ImGui::Button(ICON_FA_PLUS "  Add shape"))
        ImGui::OpenPopup("add-shape");

    if (ImGui::BeginPopup("add-shape"))
    {
        for (int type = 0; type < IM_ARRAYSIZE(SHAPE_TYPE_NAMES); ++type)
            if (ImGui::Selectable(SHAPE_TYPE_NAMES[type]))
            {
                shapes.push_back(MakeShape(type));
                selectedShape = static_cast<int>(shapes.size()) - 1;
            }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    ImGui::Checkbox("Show outlines", &debugOutlines);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Trace the selected shape's expected boundary over the canvas");

    ImGui::Spacing();
    ImGui::TextDisabled("Drag to reorder - lower rows paint on top");
    ImGui::Spacing();

    int dragIndex = -1;
    int dropIndex = -1;
    int deleteIndex = -1;

    const auto dropGap = [&](const int index) {
        ImGui::PushID(index);
        ImGui::InvisibleButton("##gap", { -1.f, ROW_SPACING });

        if (ImGui::BeginDragDropTarget())
        {
            // AcceptBeforeDelivery reports the hover while the button is still held, which is
            // what allows drawing the insertion line before the drop commits.
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SHAPE_ROW",
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                const float y = (min.y + max.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddLine({ min.x, y }, { max.x, y },
                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.f);

                if (payload->IsDelivery())
                {
                    dragIndex = *static_cast<const int*>(payload->Data);
                    dropIndex = index;
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
    };

    // The gaps are the only spacing between rows, so zero the default to keep it predictable.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, 0.f });

    dropGap(0);

    for (int i = 0; i < static_cast<int>(shapes.size()); ++i)
    {
        ImGui::PushID(i);

        char label[32];
        std::snprintf(label, sizeof(label), "%s", SHAPE_TYPE_NAMES[shapes[i].index()]);

        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float rowHeight = ImGui::GetTextLineHeight() + ROW_PADDING.y * 2.f;
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const ImVec2 rowMax { rowMin.x + rowWidth, rowMin.y + rowHeight };

        const bool isSelected = selectedShape == i;
        const bool isHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rowMin, rowMax);

        // Drawn before the Selectable, which is then rendered with transparent highlights, so
        // the rounding is ours — a Selectable's own background is always square.
        if (isSelected || isHovered)
            ImGui::GetWindowDrawList()->AddRectFilled(rowMin, rowMax,
                ImGui::GetColorU32(isSelected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), ROW_ROUNDING);

        ImGui::PushStyleColor(ImGuiCol_Header, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
        // AllowOverlap so the delete button, drawn after, takes the clicks it sits on top of.
        if (ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_AllowOverlap, { rowWidth, rowHeight }))
            selectedShape = i;
        ImGui::PopStyleColor(3);

        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("SHAPE_ROW", &i, sizeof(i));
            ImGui::TextUnformatted(label);
            ImGui::EndDragDropSource();
        }

        // The row is one Selectable, so its contents are positioned by hand over the top of it.
        const ImVec2 afterRow = ImGui::GetCursorScreenPos();

        ImGui::SetCursorScreenPos({ rowMin.x + ROW_PADDING.x, rowMin.y + ROW_PADDING.y });
        ImGui::TextUnformatted(label);

        if (isSelected)
        {
            const float button = rowHeight - ROW_PADDING.y;
            ImGui::SetCursorScreenPos({ rowMax.x - button - ROW_PADDING.x, rowMin.y + (rowHeight - button) * 0.5f });

            // The button carries no label: letting it draw one centres the glyph's *advance*
            // box, and an icon sits on the text baseline, so its ink is not centred within
            // that box - which leaves the trash visibly low. The glyph is drawn separately
            // below, positioned off its ink rectangle instead.
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ROW_ROUNDING);
            ImGui::PushStyleColor(ImGuiCol_Button, DELETE_IDLE);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DELETE_HOVERED);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, DELETE_ACTIVE);

            if (ImGui::Button("##delete", { button, button }))
                deleteIndex = i;

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            if (const ImFontGlyph* glyph = ImGui::GetFont()->FindGlyph(TRASH_GLYPH))
            {
                // Glyph corners are in the atlas's native font size, so rescale to the size
                // actually being rendered at.
                const float glyphScale = ImGui::GetFontSize() / ImGui::GetFont()->FontSize;
                const ImVec2 buttonMin = ImGui::GetItemRectMin();
                const float inkWidth = (glyph->X1 - glyph->X0) * glyphScale;
                const float inkHeight = (glyph->Y1 - glyph->Y0) * glyphScale;

                // Solve for the pen position that lands the ink centred, rather than the pen.
                const ImVec2 pen {
                    buttonMin.x + (button - inkWidth) * 0.5f - glyph->X0 * glyphScale,
                    buttonMin.y + (button - inkHeight) * 0.5f - glyph->Y0 * glyphScale
                };

                ImGui::GetWindowDrawList()->AddText(pen, ImGui::GetColorU32(ImGuiCol_Text), ICON_FA_TRASH);
            }
        }

        ImGui::SetCursorScreenPos(afterRow);

        ImGui::PopID();
        dropGap(i + 1);
    }

    ImGui::PopStyleVar();

    // Dropping a row into either gap touching it would land it where it already is.
    if (dragIndex >= 0 && dropIndex != dragIndex && dropIndex != dragIndex + 1)
    {
        const auto begin = shapes.begin();
        if (dragIndex < dropIndex)
            std::rotate(begin + dragIndex, begin + dragIndex + 1, begin + dropIndex);
        else
            std::rotate(begin + dropIndex, begin + dragIndex, begin + dragIndex + 1);

        // Every index between source and destination shifts by one; the dragged row itself
        // lands just before the gap it was dropped into.
        if (selectedShape == dragIndex)
            selectedShape = dragIndex < dropIndex ? dropIndex - 1 : dropIndex;
        else if (dragIndex < dropIndex && selectedShape > dragIndex && selectedShape < dropIndex)
            --selectedShape;
        else if (dragIndex > dropIndex && selectedShape >= dropIndex && selectedShape < dragIndex)
            ++selectedShape;
    }

    if (deleteIndex >= 0)
    {
        shapes.erase(shapes.begin() + deleteIndex);
        if (shapes.empty())
            selectedShape = -1;
        else if (selectedShape > deleteIndex)
            --selectedShape;
        else if (selectedShape == deleteIndex)
            selectedShape = glm::min(selectedShape, static_cast<int>(shapes.size()) - 1);
    }

    ImGui::End();

    ImGui::Begin("Properties");
    propertiesDockId = ImGui::GetWindowDockID();

    if (selectedShape < 0 || selectedShape >= static_cast<int>(shapes.size()))
    {
        ImGui::TextDisabled("Select a shape to edit it.");
        ImGui::End();
        return;
    }

    std::visit([](auto& s) {
        using T = std::decay_t<decltype(s)>;

        ImGui::ColorEdit4("Color", glm::value_ptr(s.color));

        bool antiAliasing = s.antiAliasing != 0;
        if (ImGui::Checkbox("Anti-aliasing", &antiAliasing))
            s.antiAliasing = antiAliasing;

        // Coordinates are left unclamped: the shaders handle shapes hanging off the canvas.
        if constexpr (std::is_same_v<T, Circle>)
        {
            ImGui::DragInt2("Center", glm::value_ptr(s.center));
            ImGui::DragFloat("Radius", &s.radius, 0.5f, 0.f, 1000.f, "%.1f");
        }
        else if constexpr (std::is_same_v<T, Rectangle>)
        {
            ImGui::DragInt2("Corner A", glm::value_ptr(s.corner0));
            ImGui::DragInt2("Corner B", glm::value_ptr(s.corner1));
        }
        else if constexpr (std::is_same_v<T, Line>)
        {
            ImGui::DragInt2("Start", glm::value_ptr(s.start));
            ImGui::DragInt2("End", glm::value_ptr(s.end));
            ImGui::DragFloat("Thickness", &s.thickness, 0.1f, 0.f, 128.f, "%.1f");
        }
        else
        {
            ImGui::DragInt2("Vertex 1", glm::value_ptr(s.vertex0));
            ImGui::DragInt2("Vertex 2", glm::value_ptr(s.vertex1));
            ImGui::DragInt2("Vertex 3", glm::value_ptr(s.vertex2));
        }
    }, shapes[selectedShape]);

    ImGui::End();

    if (debugOutlines)
    {
        // The canvas is blitted over the whole viewport, so the scale is derived from the
        // viewport rather than assumed to be 1:1 - that keeps the overlay aligned under any
        // display scaling, where hardcoding it would misplace every outline.
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 scale { viewport->Size.x / static_cast<float>(CANVAS_SIZE.x),
                             viewport->Size.y / static_cast<float>(CANVAS_SIZE.y) };

        // Background, not foreground: the outline belongs over the canvas but under the panels.
        DrawOutline(shapes[selectedShape], *ImGui::GetBackgroundDrawList(), viewport->Pos, scale);
    }
}
