#include "../dxvk/dxvk_include.h"

#include "d3d9_vr.h"

#include "d3d9_include.h"
#include "d3d9_surface.h"

#include "d3d9_device.h"

#include "L4D2VR/game.h"
#include "L4D2VR/vr.h"
#include "openvr.h"

namespace dxvk {

    class D3D9VR final : public ComObjectClamp<IDirect3DVR9>
    {
    public:

        D3D9VR(IDirect3DDevice9 *pDevice)
            : m_device(static_cast<D3D9DeviceEx *>(pDevice))
        {}

        HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID riid,
            void **ppvObject)
        {
            if (ppvObject == nullptr)
                return E_POINTER;

            *ppvObject = nullptr;

            if (riid == __uuidof(IUnknown) ||
                riid == __uuidof(IDirect3DVR9)) {
                *ppvObject = ref(this);
                return S_OK;
            }

            Logger::warn("D3D9VR::QueryInterface: Unknown interface query");
            Logger::warn(str::format(riid));
            return E_NOINTERFACE;
        }

        HRESULT STDMETHODCALLTYPE GetVRDesc(
            IDirect3DSurface9 *pSurface,
            vr::VRVulkanTextureData_t *pDesc)
        {
            if (unlikely(pSurface == nullptr || pDesc == nullptr))
                return D3DERR_INVALIDCALL;

            D3D9Surface *surface = static_cast<D3D9Surface *>(pSurface);

            const auto *tex = surface->GetCommonTexture();

            const auto &desc = tex->Desc();
            const auto &image = desc->MultiSample != D3DMULTISAMPLE_NONE ? const_cast<D3D9CommonTexture*>(tex)->GetResolveImage() : tex->GetImage();
            const auto &device = tex->Device()->GetDXVKDevice();

            // I don't know why the image randomly is a uint64_t in OpenVR.
            pDesc->m_nImage = uint64_t(image->handle());
            pDesc->m_pDevice = device->handle();
            pDesc->m_pPhysicalDevice = device->adapter()->handle();
            pDesc->m_pInstance = device->instance()->handle();
            pDesc->m_pQueue = device->queues().graphics.queueHandle;
            pDesc->m_nQueueFamilyIndex = device->queues().graphics.queueIndex;

            pDesc->m_nWidth = desc->Width;
            pDesc->m_nHeight = desc->Height;
            pDesc->m_nFormat = tex->GetFormatMapping().FormatColor;
            pDesc->m_nSampleCount = uint32_t(image->info().sampleCount);

            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE TransferSurface(
            IDirect3DSurface9 *pSurface,
            BOOL waitResourceIdle)
        {
            if (unlikely(pSurface == nullptr))
                return D3DERR_INVALIDCALL;

            auto *tex = static_cast<D3D9Surface *>(pSurface)->GetCommonTexture();
            const auto &image = tex->GetImage();

            VkImageSubresourceRange subresources = {
              VK_IMAGE_ASPECT_COLOR_BIT,
              0, image->info().mipLevels,
              0, image->info().numLayers
            };

            m_device->TransformImage(
                tex, &subresources,
                image->info().layout,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            // This wait may need to be on all Faces and Mip Levels (2 loops). Update: DxvkImage inherits from DxvkPagedResorce
            if (waitResourceIdle)
                m_device->WaitForResource(static_cast<const DxvkPagedResource&>(*image), tex->GetMappingBufferSequenceNumber(0u), D3DLOCK_READONLY);

            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE LockDevice()
        {
            m_lock = m_device->LockDevice();
            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE UnlockDevice()
        {
            m_lock = D3D9DeviceLock();
            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE WaitDeviceIdle()
        {
            m_device->Flush();

            // Not clear if we need all here, perhaps...
            m_device->SynchronizeCsThread(DxvkCsThread::SynchronizeAll);
            m_device->GetDXVKDevice()->waitForIdle();

            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE GetBackBufferData(SharedTextureHolder* backBufferData)
        {
            HRESULT res = m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBufferData->m_Surface);

            GetVRDesc(backBufferData->m_Surface, &backBufferData->m_VulkanData);
            backBufferData->m_VRTexture.handle = &backBufferData->m_VulkanData;
            backBufferData->m_VRTexture.eColorSpace = vr::ColorSpace_Auto;
            backBufferData->m_VRTexture.eType = vr::TextureType_Vulkan;

            return res;
        }

        void RenderTextureToRenderTargetWithAlpha(LPDIRECT3DTEXTURE9 texture, float width, float height)
        {
            if (!texture || !m_device)
                return;

            m_device->SetPixelShader(NULL);
            m_device->SetTexture(0, texture);
            m_device->SetFVF(FVF_CUSTOM);

            Vertex v[] =
            {
                { 0,       0,        0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
                { width,   0,        0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
                { 0,       height,   0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
                { width,   height,   0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
            };

            m_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex));
        }

    private:
        D3D9DeviceEx *m_device;
        D3D9DeviceLock m_lock;
    };

}

HRESULT __stdcall Direct3DCreateVRImpl(IDirect3DDevice9 *pDevice, IDirect3DVR9 **pInterface) {
    if (pInterface == nullptr)
        return D3DERR_INVALIDCALL;

    *pInterface = new dxvk::D3D9VR(pDevice);

    return D3D_OK;
}