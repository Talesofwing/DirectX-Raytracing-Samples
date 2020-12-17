#pragma once

#define HLSL
#include "RaytracingHlslCompat.h"

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
ByteAddressBuffer Indices : register(t1, space0);
StructuredBuffer<Vertex> Vertices : register(t2, space0);

ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);
ConstantBuffer<CubeConstantBuffer> g_cubeCB : register(b1);

uint3 Load3x16BitIndices (uint offsetBytes) {
    uint3 indices;

    const uint dwordAlignedOffset = offsetBytes & ~3;
    const uint2 four16BitIndices = Indices.Load2 (dwordAlignedOffset);

    if (dwordAlignedOffset == offsetBytes) {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    } else {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload {
    float4 color;
};

float3 HitWorldPosition () {
    return WorldRayOrigin () + RayTCurrent () * WorldRayDirection ();
}

float3 HitAttribute (float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr) {
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

inline void GenerateCameraRay (uint2 index, out float3 origin, out float3 direction) {
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions ().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul (float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize (world.xyz - origin);
}

float4 CalculateDiffuseLighting (float3 hitPosition, float3 normal) {
    float3 pixelToLight = normalize (g_sceneCB.lightPosition.xyz - hitPosition);

    float fNDotL = max (0.0f, dot (pixelToLight, normal));

    return g_cubeCB.albedo * g_sceneCB.lightDiffuseColor * fNDotL;
}

[shader ("raygeneration")]
void MyRaygenShader () {
    float3 rayDir;
    float3 origin;

    GenerateCameraRay (DispatchRaysIndex ().xy, origin, rayDir);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = {float4(0, 0, 0, 0)};
    TraceRay (Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    RenderTarget[DispatchRaysIndex ().xy] = payload.color;
}

[shader ("closesthit")]
void MyClosestHitShader (inout RayPayload payload, in MyAttributes attr) {
    float3 hitPosition = HitWorldPosition ();

    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = PrimitiveIndex () * triangleIndexStride;

    const uint3 indices = Load3x16BitIndices (baseIndex);

    float3 vertexNormals[3] = {
        Vertices[indices[0]].normal,
        Vertices[indices[1]].normal,
        Vertices[indices[2]].normal
    };

    float3 triangleNormal = HitAttribute (vertexNormals, attr);

    float4 diffuseColor = CalculateDiffuseLighting (hitPosition, triangleNormal);
    float4 color = g_sceneCB.lightAmbientColor + diffuseColor;

    payload.color = color;
}

[shader ("miss")]
void MyMissShader (inout RayPayload payload) {
    float4 background = float4(0.0f, 0.2f, 0.4f, 1.0f);
    payload.color = background;
}