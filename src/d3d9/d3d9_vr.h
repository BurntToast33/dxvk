#pragma once

#include <d3d9.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan.h>
#undef VK_USE_PLATFORM_WIN32_KHR

class IDirect3DVR9;
class D3D9DeviceEx;
class SharedTextureHolder;
inline IDirect3DVR9 *g_D3DVR9;

namespace vr {
    struct VRVulkanTextureData_t;
}

#define FVF_CUSTOM (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

MIDL_INTERFACE("7e272b32-a49c-46c7-b1a4-ef52936bec87")
IDirect3DVR9 : public IUnknown{
    struct Vertex
    {
        float x, y, z, rhw;
        DWORD color;
        float u, v;
    };

  virtual HRESULT STDMETHODCALLTYPE GetVRDesc(IDirect3DSurface9 * pSurface, vr::VRVulkanTextureData_t * pDesc) = 0;
  virtual HRESULT STDMETHODCALLTYPE TransferSurface(IDirect3DSurface9 *pSurface, BOOL waitResourceIdle) = 0;
  virtual HRESULT STDMETHODCALLTYPE LockDevice() = 0;
  virtual HRESULT STDMETHODCALLTYPE UnlockDevice() = 0;
  virtual HRESULT STDMETHODCALLTYPE WaitDeviceIdle() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetBackBufferData(SharedTextureHolder *backBufferData) = 0;
  virtual void RenderTextureToRenderTargetWithAlpha(LPDIRECT3DTEXTURE9 texture, float width, float height) = 0;
};

#ifdef _MSC_VER
struct __declspec(uuid("7e272b32-a49c-46c7-b1a4-ef52936bec87")) IDirect3DVR9;
#else
__CRT_UUID_DECL(IDirect3DVR9, 0x7e272b32, 0xa49c, 0x46c7, 0xb1, 0xa4, 0xef, 0x52, 0x93, 0x6b, 0xec, 0x87);
#endif

HRESULT __stdcall Direct3DCreateVRImpl(IDirect3DDevice9 *pDevice,
    IDirect3DVR9 **pInterface);