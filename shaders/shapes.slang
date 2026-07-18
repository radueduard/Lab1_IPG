// Compute rasterisers for the Lab 1 shapes, one entry point per primitive. Each dispatch
// paints a single shape over whatever the previous ones already left in the render target,
// which is why every entry point ends in the same source-over blend rather than a plain store.
//
// The push-constant structs mirror ISC_Lab1.h field for field: the C++ side pushes the shape
// variant's alternative verbatim, and the offsets its static_asserts pin down (16/24/28 for
// Circle, and so on) are exactly what Slang's uniform layout produces for these declarations.
//
// Every shape is drawn from a signed distance function - negative inside, zero on the
// boundary that ISC_Lab1.cpp's debug overlay traces - so coverage is one shared step.

module shapes;

struct Circle
{
    float4 color;
    int2 center;
    float radius;
    uint antiAliasing;
};

struct Rectangle
{
    float4 color;
    int2 corner0;
    int2 corner1;
    uint antiAliasing;
};

struct Line
{
    float4 color;
    int2 start;
    int2 end;
    float thickness;
    uint antiAliasing;
};

struct Triangle
{
    float4 color;
    int2 vertex0;
    int2 vertex1;
    int2 vertex2;
    uint antiAliasing;
};

// The sample point for pixel (i, j). The half-pixel offset puts it at the pixel's centre, so
// integer shape coordinates land on pixel *edges* - the same convention the ImGui overlay
// uses when it maps a shape coordinate to origin + p * scale. Without it every shape would
// sit half a pixel up and to the left of the outline that is meant to check it.
float2 samplePoint(uint2 pixel)
{
    return float2(pixel) + 0.5f;
}

// Signed distance -> how much of the pixel the shape covers.
//
// Anti-aliased, this is the standard box-filter approximation: distance is in pixels, so a
// boundary through the pixel centre (d = 0) covers half of it and the ramp spans exactly the
// one pixel the edge crosses. Aliased, the pixel is simply in or out by its centre.
float coverageOf(float signedDistance, uint antiAliasing)
{
    if (antiAliasing != 0)
        return saturate(0.5f - signedDistance);

    return signedDistance < 0.f ? 1.f : 0.f;
}

// Source-over blend, so stacking translucent shapes reads the way the layer list implies.
// Colours are stored non-premultiplied, hence weighting the source by its own alpha here.
void blend(RWTexture2D<float4> target, uint2 pixel, float4 color, float coverage)
{
    const float alpha = color.a * coverage;

    if (alpha <= 0.f)
        return;

    const float4 destination = target[pixel];
    const float outAlpha = alpha + destination.a * (1.f - alpha);

    target[pixel] = float4(color.rgb * alpha + destination.rgb * (1.f - alpha), outAlpha);
}

float sdCircle(float2 p, float2 center, float radius)
{
    return length(p - center) - radius;
}

float sdBox(float2 p, float2 corner0, float2 corner1)
{
    // Normalised rather than trusting the order the corners were dragged into - the CPU-side
    // outline does the same, so a "backwards" rectangle still agrees with its own boundary.
    const float2 low = min(corner0, corner1);
    const float2 high = max(corner0, corner1);

    const float2 extent = (high - low) * 0.5f;
    const float2 q = abs(p - (low + extent)) - extent;

    // Outside, only the positive axes contribute; inside, both are negative and the largest
    // (least negative) one is the distance to the nearest edge.
    return length(max(q, 0.f)) + min(max(q.x, q.y), 0.f);
}

// A thick line is a capsule: the segment's distance field pushed out by half the thickness.
// The round caps that fall out of it are the semicircles the debug outline closes its two
// parallel edges with.
float sdSegment(float2 p, float2 a, float2 b, float radius)
{
    const float2 edge = b - a;
    const float lengthSquared = dot(edge, edge);

    // A zero-length segment would divide by zero here; it collapses to a dot, as on the CPU side.
    const float t = lengthSquared > 0.f ? saturate(dot(p - a, edge) / lengthSquared) : 0.f;

    return length(p - (a + edge * t)) - radius;
}

float sdTriangle(float2 p, float2 v0, float2 v1, float2 v2)
{
    const float2 e0 = v1 - v0;
    const float2 e1 = v2 - v1;
    const float2 e2 = v0 - v2;

    const float2 p0 = p - v0;
    const float2 p1 = p - v1;
    const float2 p2 = p - v2;

    // Nearest point on each edge, so the unsigned distance is the smallest of the three.
    const float2 q0 = p0 - e0 * saturate(dot(p0, e0) / dot(e0, e0));
    const float2 q1 = p1 - e1 * saturate(dot(p1, e1) / dot(e1, e1));
    const float2 q2 = p2 - e2 * saturate(dot(p2, e2) / dot(e2, e2));

    // Winding is whatever the vertices were dragged into, so the inside/outside sign is read
    // off the triangle's own orientation instead of assuming one.
    const float winding = sign(e0.x * e2.y - e0.y * e2.x);

    // Each component pairs a squared distance with which side of that edge p fell on; the
    // componentwise min keeps the nearest edge's distance and its sign together.
    const float2 nearest = min(min(float2(dot(q0, q0), winding * (p0.x * e0.y - p0.y * e0.x)),
                                   float2(dot(q1, q1), winding * (p1.x * e1.y - p1.y * e1.x))),
                                   float2(dot(q2, q2), winding * (p2.x * e2.y - p2.y * e2.x)));

    // Three collinear vertices enclose nothing and leave no winding to take a sign from, so
    // the shape collapses to its own outline instead of flooding the canvas at d = 0.
    if (winding == 0.f)
        return sqrt(nearest.x);

    return -sqrt(nearest.x) * sign(nearest.y);
}

// 1280x720 divides exactly by the 8x8 groups ISC_Lab1::Render dispatches, but the check keeps
// a differently sized canvas from writing outside the image.
bool outOfBounds(RWTexture2D<float4> target, uint2 pixel)
{
    uint width, height;
    target.GetDimensions(width, height);

    return pixel.x >= width || pixel.y >= height;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void drawCircle(uint3 threadId: SV_DispatchThreadID,
                uniform Circle circle,
                RWTexture2D<float4> target)
{
    const uint2 pixel = threadId.xy;

    if (outOfBounds(target, pixel))
        return;

    const float d = sdCircle(samplePoint(pixel), float2(circle.center), circle.radius);

    blend(target, pixel, circle.color, coverageOf(d, circle.antiAliasing));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void drawRectangle(uint3 threadId: SV_DispatchThreadID,
                   uniform Rectangle rectangle,
                   RWTexture2D<float4> target)
{
    const uint2 pixel = threadId.xy;

    if (outOfBounds(target, pixel))
        return;

    const float d = sdBox(samplePoint(pixel),
                          float2(rectangle.corner0),
                          float2(rectangle.corner1));

    blend(target, pixel, rectangle.color, coverageOf(d, rectangle.antiAliasing));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void drawLine(uint3 threadId: SV_DispatchThreadID,
              uniform Line segment,
              RWTexture2D<float4> target)
{
    const uint2 pixel = threadId.xy;

    if (outOfBounds(target, pixel))
        return;

    // Thickness is the full width of the stroke, so the field is offset by half of it.
    const float d = sdSegment(samplePoint(pixel),
                              float2(segment.start),
                              float2(segment.end),
                              segment.thickness * 0.5f);

    blend(target, pixel, segment.color, coverageOf(d, segment.antiAliasing));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void drawTriangle(uint3 threadId: SV_DispatchThreadID,
                  uniform Triangle triangle,
                  RWTexture2D<float4> target)
{
    const uint2 pixel = threadId.xy;

    if (outOfBounds(target, pixel))
        return;

    const float d = sdTriangle(samplePoint(pixel),
                               float2(triangle.vertex0),
                               float2(triangle.vertex1),
                               float2(triangle.vertex2));

    blend(target, pixel, triangle.color, coverageOf(d, triangle.antiAliasing));
}
