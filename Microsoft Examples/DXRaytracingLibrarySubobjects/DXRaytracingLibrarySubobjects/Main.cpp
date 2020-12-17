#include "stdafx.h"
#include "DXRaytracingLibrarySubobjects.h"

_Use_decl_annotations_
int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
	DXRaytracingLibrarySubobjects sample (1280, 720, L"D3D12 Raytracing - Library Subobjects");
	return Win32Application::Run (&sample, hInstance, nCmdShow);
}