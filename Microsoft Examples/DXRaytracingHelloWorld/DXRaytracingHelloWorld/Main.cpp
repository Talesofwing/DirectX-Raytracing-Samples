#include "stdafx.h"
#include "D3D12RaytracingHelloWorld.h"

_Use_decl_annotations_		// �������Y����ע
int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
	D3D12RaytracingHelloWorld sample (1280, 720, L"D3D12 Raytracing - Hello World");
	return Win32Application::Run (&sample, hInstance, nCmdShow);
}