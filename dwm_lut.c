#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <MinHook.h>
#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

#define COVERLAYCONTXET_PRESENT_ADDRESS 0xE35CC
#define IOVERLAYSWAPCHAIN_IDXGISWAPCHAIN_OFFSET (-0x118)

#define STRINGIFY(x) #x

char shaders[] = STRINGIFY(
        static const float lutSize = 65;

        struct VS_INPUT {
            float2 pos: POSITION;
            float2 tex: TEXCOORD;
        };

        struct VS_OUTPUT {
            float4 pos: SV_POSITION;
            float2 tex: TEXCOORD;
        };

        Texture2D backBufferTex : register(t0);
        SamplerState backBufferSmp : register(s0);

        Texture2D lutTex : register(t1);
        SamplerState lutSmp : register(s1);

        VS_OUTPUT VS(VS_INPUT input) {
            VS_OUTPUT output;
            output.pos = float4(input.pos, 0, 1);
            output.tex = input.tex;
            return output;
        }

        float4 PS(VS_OUTPUT input) : SV_TARGET {
            float4 sample = backBufferTex.Sample(backBufferSmp, input.tex);

            float2 tex;
            tex.x = sample.r / (lutSize * lutSize) * (lutSize - 1) + 0.5 / (lutSize * lutSize);
            tex.y = sample.g / lutSize * (lutSize - 1) + 0.5 / lutSize;

            float blue = sample.b * (lutSize - 1);
            float2 tex1 = float2(tex.x + floor(blue) / lutSize, tex.y);
            float2 tex2 = float2(tex.x + ceil(blue) / lutSize, tex.y);

            float4 res1 = lutTex.Sample(lutSmp, tex1);
            float4 res2 = lutTex.Sample(lutSmp, tex2);

            return lerp(res1, res2, frac(blue));
        }
);

ID3D11Device *device;
ID3D11DeviceContext *deviceContext;
ID3D11VertexShader *vertexShader;
ID3D11PixelShader *pixelShader;
ID3D11InputLayout *inputLayout;

ID3D11Buffer *vertexBuffer;
UINT numVerts;
UINT stride;
UINT offset;

D3D11_TEXTURE2D_DESC backBufferDesc;

ID3D11SamplerState *samplerState;
ID3D11Texture2D *texture;
ID3D11ShaderResourceView *textureView;

ID3D11SamplerState *lutSamplerState;
ID3D11ShaderResourceView  *lutTextureView;

HRESULT WINAPI D3DX11CreateShaderResourceViewFromFileA(ID3D11Device* pDevice, LPCSTR pSrcFile, void* pLoadInfo, void* pPump, ID3D11ShaderResourceView** ppShaderResourceView, HRESULT* pHResult);

void DrawRectangle(struct tagRECT *rect) {
    float width = backBufferDesc.Width;
    float height = backBufferDesc.Height;

    float texLeft = rect->left / width;
    float texTop = rect->top / height;
    float texRight = rect->right / width;
    float texBottom = rect->bottom / height;

    float left = texLeft * 2 - 1;
    float top = texTop * -2 + 1;
    float right = texRight * 2 - 1;
    float bottom = texBottom * -2 + 1;

    float vertexData[] = {
            left, bottom, texLeft, texBottom,
            left, top, texLeft, texTop,
            right, bottom, texRight, texBottom,
            right, top, texRight, texTop
    };

    D3D11_MAPPED_SUBRESOURCE resource;
    deviceContext->lpVtbl->Map(deviceContext, (ID3D11Resource *) vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
    memcpy(resource.pData, vertexData, stride * numVerts);
    deviceContext->lpVtbl->Unmap(deviceContext, (ID3D11Resource *) vertexBuffer, 0);

    deviceContext->lpVtbl->IASetVertexBuffers(deviceContext, 0, 1, &vertexBuffer, &stride, &offset);

    deviceContext->lpVtbl->Draw(deviceContext, numVerts, 0);
}

void InitializeStuff(IDXGISwapChain* swapChain) {
    swapChain->lpVtbl->GetDevice(swapChain, &IID_ID3D11Device, (void **) &device);
    device->lpVtbl->GetImmediateContext(device, &deviceContext);
    {
        ID3DBlob *vsBlob;
        D3DCompile(shaders, sizeof(shaders), NULL, NULL, NULL, "VS", "vs_5_0", 0, 0, &vsBlob, NULL);
        device->lpVtbl->CreateVertexShader(device, vsBlob->lpVtbl->GetBufferPointer(vsBlob), vsBlob->lpVtbl->GetBufferSize(vsBlob), NULL, &vertexShader);
        D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
                {
                        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0},
                        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
                };

        device->lpVtbl->CreateInputLayout(device, inputElementDesc, ARRAYSIZE(inputElementDesc), vsBlob->lpVtbl->GetBufferPointer(vsBlob), vsBlob->lpVtbl->GetBufferSize(vsBlob), &inputLayout);
        vsBlob->lpVtbl->Release(vsBlob);
    }
    {
        ID3DBlob *psBlob;
        D3DCompile(shaders, sizeof(shaders), NULL, NULL, NULL, "PS", "ps_5_0", 0, 0, (ID3DBlob **) &psBlob, NULL);
        device->lpVtbl->CreatePixelShader(device, psBlob->lpVtbl->GetBufferPointer(psBlob), psBlob->lpVtbl->GetBufferSize(psBlob), NULL, &pixelShader);
        psBlob->lpVtbl->Release(psBlob);
    }
    {
        stride = 4 * sizeof(float);
        numVerts = 4;
        offset = 0;

        D3D11_BUFFER_DESC vertexBufferDesc = {};
        vertexBufferDesc.ByteWidth = stride * numVerts;
        vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        device->lpVtbl->CreateBuffer(device, &vertexBufferDesc, NULL, &vertexBuffer);
    }
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

        device->lpVtbl->CreateSamplerState(device, &samplerDesc, &samplerState);
    }
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

        device->lpVtbl->CreateSamplerState(device, &samplerDesc, &lutSamplerState);
        if (FAILED(D3DX11CreateShaderResourceViewFromFileA(device, "C:\\lut.png", NULL, NULL, &lutTextureView, NULL))) {
            exit(1);
        }
    }
}

void ApplyLUT(IDXGISwapChain *swapChain, struct tagRECT *rects, unsigned int numRects) {
    if (!device) {
        InitializeStuff(swapChain);
    }

    ID3D11Texture2D *backBuffer;
    ID3D11RenderTargetView *renderTargetView;

    swapChain->lpVtbl->GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, (void **) &backBuffer);
    device->lpVtbl->CreateRenderTargetView(device, (ID3D11Resource *) backBuffer, NULL, &renderTargetView);

    {
        D3D11_TEXTURE2D_DESC texDesc;
        backBuffer->lpVtbl->GetDesc(backBuffer, &texDesc);

        if (backBufferDesc.Width != texDesc.Width || backBufferDesc.Height != texDesc.Height || backBufferDesc.Format != texDesc.Format) {
            backBufferDesc = texDesc;

            texDesc.Usage = D3D11_USAGE_DEFAULT;
            texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            texDesc.CPUAccessFlags = 0;
            texDesc.MiscFlags = 0;

            if (texture != NULL) {
                texture->lpVtbl->Release(texture);
                textureView->lpVtbl->Release(textureView);
            }

            device->lpVtbl->CreateTexture2D(device, &texDesc, NULL, &texture);
            device->lpVtbl->CreateShaderResourceView(device, (ID3D11Resource *) texture, NULL, &textureView);
        }
    }

    deviceContext->lpVtbl->RSSetViewports(deviceContext, 1, &(D3D11_VIEWPORT) {0, 0, backBufferDesc.Width, backBufferDesc.Height, 0.0f, 1.0f});

    deviceContext->lpVtbl->OMSetRenderTargets(deviceContext, 1, &renderTargetView, NULL);
    renderTargetView->lpVtbl->Release(renderTargetView);

    deviceContext->lpVtbl->IASetPrimitiveTopology(deviceContext, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    deviceContext->lpVtbl->IASetInputLayout(deviceContext, inputLayout);

    deviceContext->lpVtbl->VSSetShader(deviceContext, vertexShader, NULL, 0);
    deviceContext->lpVtbl->PSSetShader(deviceContext, pixelShader, NULL, 0);

    deviceContext->lpVtbl->PSSetShaderResources(deviceContext, 0, 1, &textureView);
    deviceContext->lpVtbl->PSSetSamplers(deviceContext, 0, 1, &samplerState);

    deviceContext->lpVtbl->PSSetShaderResources(deviceContext, 1, 1, &lutTextureView);
    deviceContext->lpVtbl->PSSetSamplers(deviceContext, 1, 1, &lutSamplerState);

    for (int i = 0; i < numRects; i++) {
        D3D11_BOX sourceRegion;
        sourceRegion.left = rects[i].left;
        sourceRegion.right = rects[i].right;
        sourceRegion.top = rects[i].top;
        sourceRegion.bottom = rects[i].bottom;
        sourceRegion.front = 0;
        sourceRegion.back = 1;

        deviceContext->lpVtbl->CopySubresourceRegion(deviceContext, (ID3D11Resource *) texture, 0, rects[i].left, rects[i].top, 0, (ID3D11Resource *) backBuffer, 0, &sourceRegion);
        DrawRectangle(&rects[i]);
    }

    backBuffer->lpVtbl->Release(backBuffer);
}

typedef struct rectVec {
    struct tagRECT *start;
    struct tagRECT *end;
    struct tagRECT *cap;
} rectVec;

typedef long(COverlayContext_Present_t)(void *, void *, unsigned int, rectVec *, unsigned int, bool);

COverlayContext_Present_t *COverlayContext_Present_orig;

long COverlayContext_Present_hook(void *this, void *overlaySwapChain, unsigned int a3, rectVec *rectVec, unsigned int a5, bool a6) {
    IDXGISwapChain *swapChain = *(IDXGISwapChain **) ((char *) overlaySwapChain + IOVERLAYSWAPCHAIN_IDXGISWAPCHAIN_OFFSET);
    ApplyLUT(swapChain, rectVec->start, rectVec->end - rectVec->start);
    return COverlayContext_Present_orig(this, overlaySwapChain, a3, rectVec, a5, a6);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            COverlayContext_Present_orig = (COverlayContext_Present_t *) ((char *) GetModuleHandle("dwmcore.dll") + COVERLAYCONTXET_PRESENT_ADDRESS);
            MH_Initialize();
            MH_CreateHook((PVOID) COverlayContext_Present_orig, (PVOID) COverlayContext_Present_hook, (PVOID *) &COverlayContext_Present_orig);
            MH_EnableHook(MH_ALL_HOOKS);
            break;
        case DLL_PROCESS_DETACH:
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            break;
        default:
            break;
    }
    return TRUE;
}
