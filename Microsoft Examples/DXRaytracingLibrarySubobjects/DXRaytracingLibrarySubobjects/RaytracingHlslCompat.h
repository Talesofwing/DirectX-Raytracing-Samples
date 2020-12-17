#ifndef RAYTRACINGHLSLCOMPAT_H
#define RAYTRACINGHLSLCOMPAT_H

#ifdef HLSL
#include "HlslCompat.h"
#else
using namespace DirectX;

// Shader will use byte encoding to access indices.
typedef UINT16 Index;
#endif

struct SceneConstantBuffer {
	XMMATRIX projectionToWorld;
	XMVECTOR cameraPosition;
	XMVECTOR lightPosition;
	XMVECTOR lightAmbientColor;
	XMVECTOR lightDiffuseColor;
};

struct CubeConstantBuffer {
	XMFLOAT4 albedo;
};

struct Vertex {
	XMFLOAT3 position;
	XMFLOAT3 normal;
};

#endif // RAYTRACINGHLSLCOMPAT_H