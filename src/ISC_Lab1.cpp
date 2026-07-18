#include "ISC_Lab1.h"

void ISC_Lab1::Initialize()
{
    // TODO: set up resources
}

void ISC_Lab1::Update()
{
    // TODO: per-frame logic
}

void ISC_Lab1::Render(kor::CommandBuffer& commandBuffer)
{
    commandBuffer
        .BeginRendering()
        .EndRendering();
}

void ISC_Lab1::RenderUI(ImGuiContext* context)
{
    // TODO: define the scene UI
}
