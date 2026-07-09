#include "../dxvk/dxvk_include.h"

#include "d3d9_vr.h"

#include "d3d9_include.h"
#include "d3d9_surface.h"

#include "d3d9_device.h"

#include "L4D2VR/game.h"
#include "L4D2VR/vr.h"
#include "openvr.h"

void DumpRenderStates(IDirect3DDevice9* device)
{
    for (int i = 0; i < D3DRS_BLENDOPALPHA + 1; i++)
    {
        DWORD value = 0;

        if (SUCCEEDED(device->GetRenderState(
            (D3DRENDERSTATETYPE)i,
            &value)))
        {
            printf("RS[%d] = 0x%08X\n", i, value);
        }
    }
}

void DumpShaderStates(IDirect3DDevice9* device)
{
    printf("==== Shader State Dump ====\n");

    // Pixel Shader
    IDirect3DPixelShader9* ps = nullptr;
    HRESULT hr = device->GetPixelShader(&ps);

    if (SUCCEEDED(hr))
    {
        printf("Pixel Shader: %p\n", ps);

        if (ps)
        {
            UINT size = 0;
            ps->GetFunction(nullptr, &size);

            printf("Pixel Shader bytecode size: %u bytes\n", size);

            ps->Release();
        }
        else
        {
            printf("Pixel Shader: NULL (fixed function)\n");
        }
    }

    // Vertex Shader
    IDirect3DVertexShader9* vs = nullptr;
    hr = device->GetVertexShader(&vs);

    if (SUCCEEDED(hr))
    {
        printf("Vertex Shader: %p\n", vs);

        if (vs)
        {
            UINT size = 0;
            vs->GetFunction(nullptr, &size);

            printf("Vertex Shader bytecode size: %u bytes\n", size);

            vs->Release();
        }
        else
        {
            printf("Vertex Shader: NULL\n");
        }
    }

    // Vertex Declaration
    IDirect3DVertexDeclaration9* decl = nullptr;
    hr = device->GetVertexDeclaration(&decl);

    if (SUCCEEDED(hr))
    {
        printf("Vertex Declaration: %p\n", decl);

        if (decl)
        {
            D3DVERTEXELEMENT9 elements[64];
            UINT count = 0;

            if (SUCCEEDED(decl->GetDeclaration(elements, &count)))
            {
                printf("Vertex Elements: %u\n", count);

                for (UINT i = 0; i < count; i++)
                {
                    printf(
                        "  [%u] Stream=%u Offset=%u Type=%u Method=%u Usage=%u UsageIndex=%u\n",
                        i,
                        elements[i].Stream,
                        elements[i].Offset,
                        elements[i].Type,
                        elements[i].Method,
                        elements[i].Usage,
                        elements[i].UsageIndex
                    );
                }
            }

            decl->Release();
        }
    }

    // FVF
    DWORD fvf = 0;
    if (SUCCEEDED(device->GetFVF(&fvf)))
    {
        printf("FVF: 0x%08X\n", fvf);
    }

    printf("===========================\n");
}

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

        //The way this handles states is risky, what ever changed settings last will effect this
        void RenderTextureToRenderTargetWithAlpha(LPDIRECT3DTEXTURE9 texture, float width, float height)
        {
            if (!texture || !m_device)
                return;

            m_device->SetPixelShader(NULL);
            m_device->SetTexture(0, texture);

            m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);

            m_device->SetFVF(FVF_CUSTOM);

            static Vertex v[4] =
            {
                { 0.0f, 0.0f, 0.0f, 1.0f, 0xFFFF0000, 0.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f, 0xFFFF0000, 1.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f, 0xFFFF0000, 0.0f, 1.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f, 0xFFFF0000, 1.0f, 1.0f },
            };

            v[1].x = width;
            v[2].y = height;
            v[3].x = width;
            v[3].y = height;

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