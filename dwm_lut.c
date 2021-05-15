#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <MinHook.h>
#include <psapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

#define RELEASE_IF_NOT_NULL(x) { if (x != NULL) { x->lpVtbl->Release(x); } }
#define STRINGIFY(x) #x
#define _STRINGIFY(x) STRINGIFY(x)

#define LUT_SIZE 65

const unsigned char COverlayContext_Present_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xb1, 0x20, 0x2c, 0x00, 0x00, 0x45, 0x8b, 0xd0, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xf6, 0x0f, 0x85};
const int IOverlaySwapChain_IDXGISwapChain_offset = -0x118;

const unsigned char COverlayContext_IsCandidateDirectFlipCompatbile_bytes[] = {0x48, 0x89, 0x7c, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x40};
const unsigned char COverlayContext_OverlaysEnabled_bytes[] = {0x75, 0x04, 0x32, 0xc0, 0xc3, 0xcc, 0x83, 0x79, 0x30, 0x01, 0x0f, 0x97, 0xc0, 0xc3};

char shaders[] = _STRINGIFY((
        static const float bayerMatrix[8][8] = {
                { 0,32, 8,40, 2,34,10,42},
                {48,16,56,24,50,18,58,26},
                {12,44, 4,36,14,46, 6,38},
                {60,28,52,20,62,30,54,22},
                { 3,35,11,43, 1,33, 9,41},
                {51,19,59,27,49,17,57,25},
                {15,47, 7,39,13,45, 5,37},
                {63,31,55,23,61,29,53,21},
        };

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

        int doDithering;

        VS_OUTPUT VS(VS_INPUT input) {
            VS_OUTPUT output;
            output.pos = float4(input.pos, 0, 1);
            output.tex = input.tex;
            return output;
        }

        float4 PS(VS_OUTPUT input) : SV_TARGET {
            float4 sample = backBufferTex.Sample(backBufferSmp, input.tex);

            float2 tex;
            tex.x = sample.r / (LUT_SIZE * LUT_SIZE) * (LUT_SIZE - 1) + 0.5 / (LUT_SIZE * LUT_SIZE);
            tex.y = sample.g / LUT_SIZE * (LUT_SIZE - 1) + 0.5 / LUT_SIZE;

            float blue = sample.b * (LUT_SIZE - 1);
            float2 tex1 = float2(tex.x + floor(blue) / LUT_SIZE, tex.y);
            float2 tex2 = float2(tex.x + ceil(blue) / LUT_SIZE, tex.y);

            float3 res1 = lutTex.Sample(lutSmp, tex1).rgb;
            float3 res2 = lutTex.Sample(lutSmp, tex2).rgb;
            float3 res = lerp(res1, res2, frac(blue));

            if (doDithering) {
                int2 pos = int2(input.pos.xy);
                res += (1/64.0 * (bayerMatrix[pos.x % 8][pos.y % 8] - 63/2.0))/255;
                res = round(res * 255)/255;
            }

            return float4(res, sample.a);
        }
));

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
ID3D11ShaderResourceView *lutTextureView;

ID3D11Buffer *constantBuffer;

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

float lut[LUT_SIZE][LUT_SIZE][LUT_SIZE][4];

bool parseCubeLut(char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) return false;

    for (int b = 0; b < LUT_SIZE; b++) {
        for (int g = 0; g < LUT_SIZE; g++) {
            for (int r = 0; r < LUT_SIZE; r++) {
                char line[256];
                bool gotLine = false;

                while (!gotLine) {
                    if (!fgets(line, sizeof(line), file)) {
                        fclose(file);
                        return false;
                    }
                    if (line[0] >= ',' && line[0] <= '9') {
                        float red, green, blue;

                        if (sscanf(line, "%f%f%f", &red, &green, &blue) != 3) {
                            fclose(file);
                            return false;
                        }

                        lut[g][b][r][0] = red;
                        lut[g][b][r][1] = green;
                        lut[g][b][r][2] = blue;
                        lut[g][b][r][3] = 1;

                        gotLine = true;
                    }
                }
            }
        }
    }
    fclose(file);
    return true;
}

void InitializeStuff(IDXGISwapChain *swapChain) {
    swapChain->lpVtbl->GetDevice(swapChain, &IID_ID3D11Device, (void **) &device);
    device->lpVtbl->GetImmediateContext(device, &deviceContext);
    {
        ID3DBlob *vsBlob;
        D3DCompile(shaders + 1, sizeof(shaders) - 3, NULL, NULL, NULL, "VS", "vs_5_0", 0, 0, &vsBlob, NULL);
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
        D3DCompile(shaders + 1, sizeof(shaders) - 3, NULL, NULL, NULL, "PS", "ps_5_0", 0, 0, &psBlob, NULL);
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
    }
    {
        D3D11_TEXTURE2D_DESC desc;
        desc.Width = LUT_SIZE * LUT_SIZE;
        desc.Height = LUT_SIZE;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        D3D11_SUBRESOURCE_DATA initData;
        initData.pSysMem = lut;
        initData.SysMemPitch = sizeof(lut[0]);

        ID3D11Texture2D *tex;
        device->lpVtbl->CreateTexture2D(device, &desc, &initData, &tex);
        device->lpVtbl->CreateShaderResourceView(device, (ID3D11Resource *) tex, NULL, &lutTextureView);
        tex->lpVtbl->Release(tex);
    }
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = 16;
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        device->lpVtbl->CreateBuffer(device, &bufferDesc, NULL, &constantBuffer);
    }
}

void UninitializeStuff() {
    RELEASE_IF_NOT_NULL(device)
    RELEASE_IF_NOT_NULL(deviceContext)
    RELEASE_IF_NOT_NULL(vertexShader)
    RELEASE_IF_NOT_NULL(pixelShader)
    RELEASE_IF_NOT_NULL(inputLayout)
    RELEASE_IF_NOT_NULL(vertexBuffer)
    RELEASE_IF_NOT_NULL(samplerState)
    RELEASE_IF_NOT_NULL(texture)
    RELEASE_IF_NOT_NULL(textureView)
    RELEASE_IF_NOT_NULL(lutSamplerState)
    RELEASE_IF_NOT_NULL(lutTextureView)
    RELEASE_IF_NOT_NULL(constantBuffer)
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
            if (backBufferDesc.Format != texDesc.Format) {
                D3D11_MAPPED_SUBRESOURCE mappedResource;
                deviceContext->lpVtbl->Map(deviceContext, (ID3D11Resource *) constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
                int value = texDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
                memcpy(mappedResource.pData, &value, sizeof(value));
                deviceContext->lpVtbl->Unmap(deviceContext, (ID3D11Resource *) constantBuffer, 0);
            }

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

    deviceContext->lpVtbl->PSSetConstantBuffers(deviceContext, 0, 1, &constantBuffer);

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
    IDXGISwapChain *swapChain = *(IDXGISwapChain **) ((unsigned char *) overlaySwapChain + IOverlaySwapChain_IDXGISwapChain_offset);
    ApplyLUT(swapChain, rectVec->start, rectVec->end - rectVec->start);
    return COverlayContext_Present_orig(this, overlaySwapChain, a3, rectVec, a5, a6);
}

bool COverlayContext_IsCandidateDirectFlipCompatbile_hook(void) {
    return false;
}

bool COverlayContext_OverlaysEnabled_hook(void) {
    return false;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH: {
            HMODULE dwmcore = GetModuleHandle("dwmcore.dll");
            MODULEINFO moduleInfo;
            GetModuleInformation(GetCurrentProcess(), dwmcore, &moduleInfo, sizeof(moduleInfo));

            void *COverlayContext_IsCandidateDirectFlipCompatbile_orig = 0;
            void *COverlayContext_OverlaysEnabled_orig = 0;

            for (int i = 0; i <= moduleInfo.SizeOfImage - sizeof(COverlayContext_Present_bytes); i++) {
                unsigned char *address = (unsigned char *) dwmcore + i;
                if (!COverlayContext_Present_orig && !memcmp(address, COverlayContext_Present_bytes, sizeof(COverlayContext_Present_bytes))) {
                    COverlayContext_Present_orig = (COverlayContext_Present_t *) address;
                } else if (!COverlayContext_IsCandidateDirectFlipCompatbile_orig && !memcmp(address, COverlayContext_IsCandidateDirectFlipCompatbile_bytes, sizeof(COverlayContext_IsCandidateDirectFlipCompatbile_bytes))) {
                    static int found = 0;
                    found++;
                    if (found == 2) {
                        COverlayContext_IsCandidateDirectFlipCompatbile_orig = address - 0xa;
                    }
                } else if (!COverlayContext_OverlaysEnabled_orig && !memcmp(address, COverlayContext_OverlaysEnabled_bytes, sizeof(COverlayContext_OverlaysEnabled_bytes))) {
                    COverlayContext_OverlaysEnabled_orig = address - 0x7;
                }
                if (COverlayContext_Present_orig && COverlayContext_IsCandidateDirectFlipCompatbile_orig && COverlayContext_OverlaysEnabled_orig) {
                    break;
                }
            }

            char lutPath[MAX_PATH];
            ExpandEnvironmentStringsA("%SYSTEMROOT%\\Temp\\lut.cube", lutPath, sizeof(lutPath));
            bool parsedLut = parseCubeLut(lutPath);

            if (COverlayContext_Present_orig && COverlayContext_IsCandidateDirectFlipCompatbile_orig && COverlayContext_OverlaysEnabled_orig && parsedLut) {
                MH_Initialize();
                MH_CreateHook((PVOID) COverlayContext_Present_orig, (PVOID) COverlayContext_Present_hook, (PVOID *) &COverlayContext_Present_orig);
                MH_CreateHook((PVOID) COverlayContext_IsCandidateDirectFlipCompatbile_orig, (PVOID) COverlayContext_IsCandidateDirectFlipCompatbile_hook, NULL);
                MH_CreateHook((PVOID) COverlayContext_OverlaysEnabled_orig, (PVOID) COverlayContext_OverlaysEnabled_hook, NULL);
                MH_EnableHook(MH_ALL_HOOKS);
                break;
            }
            return FALSE;
        }
        case DLL_PROCESS_DETACH:
            MH_Uninitialize();
            Sleep(20);
            UninitializeStuff();
            break;
        default:
            break;
    }
    return TRUE;
}
