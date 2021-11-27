#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <MinHook.h>
#include <psapi.h>
#include <stdbool.h>
#include <stdio.h>

#define BAYER_SIZE 32
#define DITHER_GAMMA 2.2
#define LUT_FOLDER "%SYSTEMROOT%\\Temp\\luts"
#define MAX_LUTS 32

#define RELEASE_IF_NOT_NULL(x) { if (x != NULL) { x->lpVtbl->Release(x); } }
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

const unsigned char COverlayContext_Present_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xb1, 0x20, 0x2c, 0x00, 0x00, 0x45, 0x8b, 0xd0, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xf6, 0x0f, 0x85};
const int IOverlaySwapChain_IDXGISwapChain_offset = -0x118;

const unsigned char COverlayContext_IsCandidateDirectFlipCompatbile_bytes[] = {0x48, 0x89, 0x7c, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x40};
const unsigned char COverlayContext_OverlaysEnabled_bytes[] = {0x75, 0x04, 0x32, 0xc0, 0xc3, 0xcc, 0x83, 0x79, 0x30, 0x01, 0x0f, 0x97, 0xc0, 0xc3};

const int COverlayContext_DeviceClipBox_offset = -0x120;

const int IOverlaySwapChain_HardwareProtected_offset = -0xbc;

const unsigned char COverlayContext_Present_bytes_w11[] = {0x48, 0x33, 0xc4, 0x48, 0x89, 0x44, 0x24, 0x50, 0x48, 0x8b, 0xb1, 0xa0, 0x2b, 0x00, 0x00, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xf6};
const int IOverlaySwapChain_IDXGISwapChain_offset_w11 = -0x148;

const unsigned char COverlayContext_IsCandidateDirectFlipCompatbile_bytes_w11[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x68};
const unsigned char COverlayContext_OverlaysEnabled_bytes_w11[] = {0x74, 0x09, 0x83, 0x79, 0x2c, 0x01, 0x0f, 0x97, 0xc0, 0xc3, 0xcc, 0x32, 0xc0, 0xc3};

const int COverlayContext_DeviceClipBox_offset_w11 = 0x462c;

const int IOverlaySwapChain_HardwareProtected_offset_w11 = -0xec;

bool isWindows11;

#pragma push_macro("bool")
#undef bool
char shaders[] = STRINGIFY((
        struct VS_INPUT {
            float2 pos: POSITION;
            float2 tex: TEXCOORD;
        };

        struct VS_OUTPUT {
            float4 pos: SV_POSITION;
            float2 tex: TEXCOORD;
        };

        Texture2D backBufferTex : register(t0);
        Texture3D lutTex : register(t1);
        SamplerState smp : register(s0);

        Texture2D bayerTex : register(t2);
        SamplerState bayerSmp : register(s1);

        int lutSize : register(b0);
        bool hdr : register(b0);

        static float3x3 bt709_to_bt2020 = {
        2939026994.L / 4684425795.L, 9255011753.L / 28106554770.L,   173911579.L /  4015222110.L,
          76515593.L / 1107360270.L, 6109575001.L /  6644161620.L,    75493061.L /  6644161620.L,
          12225392.L /  745840075.L, 1772384008.L / 20137682025.L, 18035212433.L / 20137682025.L,
        };

        static float3x3 bt2020_to_bt709 = {
         2785571537.L /  1677558947.L,  -985802650.L /  1677558947.L,  -122209940.L /  1677558947.L,
        -4638020506.L / 37238079773.L, 42187016744.L / 37238079773.L,  -310916465.L / 37238079773.L,
          -97469024.L /  5369968309.L, -3780738464.L / 37589778163.L, 42052799795.L / 37589778163.L,
        };

        static float m1 = 1305 / 8192.;
        static float m2 = 2523 /   32.;
        static float c1 =  107 /  128.;
        static float c2 = 2413 /  128.;
        static float c3 = 2392 /  128.;

        float3 SampleLut(float3 index) {
            float3 tex = index / lutSize + 0.5 / lutSize;
            return lutTex.Sample(smp, tex).rgb;
        }

        // adapted from https://doi.org/10.2312/egp.20211031
        void barycentricWeight(float3 r, out float4 bary, out int3 vert2, out int3 vert3) {
            vert2 = int3(0,0,0); vert3 = int3(1,1,1);
            int3 c = r.xyz >= r.yzx;
            bool c_xy = c.x; bool c_yz = c.y; bool c_zx = c.z;
            bool c_yx =!c.x; bool c_zy =!c.y; bool c_xz =!c.z;
            bool cond;  float3 s = float3(0,0,0);
        #define ORDER(X, Y, Z)                   \
            cond = c_ ## X ## Y && c_ ## Y ## Z; \
            s = cond ? r.X ## Y ## Z : s;        \
            vert2.X = cond ? 1 : vert2.X;        \
            vert3.Z = cond ? 0 : vert3.Z;
            ORDER(x,y,z)   ORDER(x,z,y)   ORDER(z,x,y)
            ORDER(z,y,x)   ORDER(y,z,x)   ORDER(y,x,z)
            bary = float4(1 - s.x, s.z, s.x - s.y, s.y - s.z);
        }

        float3 LutTransformTetrahedral(float3 rgb) {
            float3 lutIndex = rgb * (lutSize - 1);
            float4 bary; int3 vert2; int3 vert3;
            barycentricWeight(frac(lutIndex), bary, vert2, vert3);

            float3 base = floor(lutIndex);
            return bary.x * SampleLut(base) +
                   bary.y * SampleLut(base + 1) +
                   bary.z * SampleLut(base + vert2) +
                   bary.w * SampleLut(base + vert3);
        }

        float3 pq_eotf(float3 e) {
            return pow(max((pow(e, 1 / m2) - c1), 0) / (c2 - c3 * pow(e, 1 / m2)), 1 / m1);
        }

        float3 pq_inv_eotf(float3 y) {
            return pow((c1 + c2 * pow(y, m1)) / (1 + c3 * pow(y, m1)), m2);
        }

        float3 OrderedDither(float3 rgb, float2 pos) {
            float3 low = floor(rgb * 255) / 255;
            float3 high = low + 1.0 / 255;

            float3 rgb_linear = pow(rgb, DITHER_GAMMA);
            float3 low_linear = pow(low, DITHER_GAMMA);
            float3 high_linear = pow(high, DITHER_GAMMA);

            float noise = bayerTex.Sample(bayerSmp, pos / BAYER_SIZE).x;
            float3 threshold = lerp(low_linear, high_linear, noise);

            return lerp(low, high, rgb_linear > threshold);
       }

        VS_OUTPUT VS(VS_INPUT input) {
            VS_OUTPUT output;
            output.pos = float4(input.pos, 0, 1);
            output.tex = input.tex;
            return output;
        }

        float4 PS(VS_OUTPUT input) : SV_TARGET {
            float3 sample = backBufferTex.Sample(smp, input.tex).rgb;

            if (hdr) {
                float3 hdr10_sample = pq_inv_eotf(max(mul(bt709_to_bt2020, sample * 80 / 10000), 0));

                float3 hdr10_res = LutTransformTetrahedral(hdr10_sample);

                float3 scrgb_res = mul(bt2020_to_bt709, pq_eotf(hdr10_res)) * 10000 / 80;

                return float4(scrgb_res, 1);
            }
            else {
                float3 res = LutTransformTetrahedral(sample);

                res = OrderedDither(res, input.pos.xy);

                return float4(res, 1);
            }
        }
));
#pragma pop_macro("bool")

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
D3D11_TEXTURE2D_DESC textureDesc[2];

ID3D11SamplerState *samplerState;
ID3D11Texture2D *texture[2];
ID3D11ShaderResourceView *textureView[2];

ID3D11SamplerState *bayerSamplerState;
ID3D11ShaderResourceView *bayerTextureView;

ID3D11Buffer *constantBuffer;

typedef struct lutData {
    int left;
    int top;
    int size;
    bool isHdr;
    ID3D11ShaderResourceView *textureView;
    float *rawLut;
} lutData;

void DrawRectangle(struct tagRECT *rect, int index) {
    float width = backBufferDesc.Width;
    float height = backBufferDesc.Height;

    float screenLeft = rect->left / width;
    float screenTop = rect->top / height;
    float screenRight = rect->right / width;
    float screenBottom = rect->bottom / height;

    float left = screenLeft * 2 - 1;
    float top = screenTop * -2 + 1;
    float right = screenRight * 2 - 1;
    float bottom = screenBottom * -2 + 1;

    width = textureDesc[index].Width;
    height = textureDesc[index].Height;
    float texLeft = rect->left / width;
    float texTop = rect->top / height;
    float texRight = rect->right / width;
    float texBottom = rect->bottom / height;

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

int numLuts;

lutData luts[MAX_LUTS];

bool AddLUT(char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) return false;

    char line[256];
    int lutSize;

    while (1) {
        if (!fgets(line, sizeof(line), file)) {
            fclose(file);
            return false;
        }
        if (sscanf(line, "LUT_3D_SIZE%d", &lutSize) == 1) {
            break;
        };
    }

    float (*rawLut)[lutSize][lutSize][4] = malloc(lutSize * lutSize * lutSize * 4 * sizeof(float));

    for (int b = 0; b < lutSize; b++) {
        for (int g = 0; g < lutSize; g++) {
            for (int r = 0; r < lutSize; r++) {
                while (1) {
                    if (!fgets(line, sizeof(line), file)) {
                        fclose(file);
                        free(rawLut);
                        return false;
                    }
                    if (line[0] >= ',' && line[0] <= '9') {
                        float red, green, blue;

                        if (sscanf(line, "%f%f%f", &red, &green, &blue) != 3) {
                            fclose(file);
                            free(rawLut);
                            return false;
                        }

                        rawLut[b][g][r][0] = red;
                        rawLut[b][g][r][1] = green;
                        rawLut[b][g][r][2] = blue;
                        rawLut[b][g][r][3] = 1;

                        break;
                    }
                }
            }
        }
    }
    fclose(file);
    luts[numLuts].size = lutSize;
    luts[numLuts++].rawLut = (float *) rawLut;
    return true;
}

void AddLUTs(char *folder) {
    WIN32_FIND_DATAA findData;

    char path[MAX_PATH];
    strcpy(path, folder);
    strcat(path, "\\*");
    HANDLE hFind = FindFirstFileA(path, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char filePath[MAX_PATH];
            char *fileName = findData.cFileName;

            strcpy(filePath, folder);
            strcat(filePath, "\\");
            strcat(filePath, fileName);

            if (sscanf(findData.cFileName, "%d_%d", &luts[numLuts].left, &luts[numLuts].top) == 2) {
                luts[numLuts].isHdr = strstr(fileName, "hdr") != NULL;
                AddLUT(filePath);
            }
        }
    } while (FindNextFile(hFind, &findData) != 0 && numLuts < MAX_LUTS);
    FindClose(hFind);
}

int lutActiveTargetIndex;
int trackedLutActiveTargets;
void *lutActiveTargets[MAX_LUTS];

bool IsLUTActiveTarget(void *address) {
    for (int i = 0; i < trackedLutActiveTargets; i++) {
        if (lutActiveTargets[i] == address) {
            return true;
        }
    }
    return false;
}

void AddLUTActiveTarget(void *address) {
    if (!IsLUTActiveTarget(address)) {
        lutActiveTargets[lutActiveTargetIndex++] = address;
        trackedLutActiveTargets = max(trackedLutActiveTargets, lutActiveTargetIndex);
        lutActiveTargetIndex %= MAX_LUTS;
    }
}

void RemoveLUTActiveTarget(void *address) {
    for (int i = 0; i < trackedLutActiveTargets; i++) {
        if (lutActiveTargets[i] == address) {
            lutActiveTargets[i] = NULL;
            return;
        }
    }
}

lutData *GetLUTDataFromCOverlayContext(void *context, bool hdr) {
    int left, top;
    if (isWindows11) {
        float *rect = (float *) ((unsigned char *) context + COverlayContext_DeviceClipBox_offset_w11);
        left = (int) rect[0];
        top = (int) rect[1];
    } else {
        int *rect = (int *) ((unsigned char *) context + COverlayContext_DeviceClipBox_offset);
        left = rect[0];
        top = rect[1];
    }

    for (int i = 0; i < numLuts; i++) {
        if (luts[i].left == left && luts[i].top == top && luts[i].isHdr == hdr) {
            return &luts[i];
        }
    }
    return NULL;
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
    for (int i = 0; i < numLuts; i++) {
        lutData *lut = &luts[i];

        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width = lut->size;
        desc.Height = lut->size;
        desc.Depth = lut->size;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData;
        initData.pSysMem = lut->rawLut;
        initData.SysMemPitch = lut->size * 4 * sizeof(float);
        initData.SysMemSlicePitch = lut->size * lut->size * 4 * sizeof(float);

        ID3D11Texture3D *tex;
        device->lpVtbl->CreateTexture3D(device, &desc, &initData, &tex);
        device->lpVtbl->CreateShaderResourceView(device, (ID3D11Resource *) tex, NULL, &luts[i].textureView);
        tex->lpVtbl->Release(tex);
        free(lut->rawLut);
        lut->rawLut = NULL;
    }
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

        device->lpVtbl->CreateSamplerState(device, &samplerDesc, &bayerSamplerState);
    }
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = BAYER_SIZE;
        desc.Height = BAYER_SIZE;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        float preCalculatedBayer[BAYER_SIZE][BAYER_SIZE];

        // adapted from https://bisqwit.iki.fi/story/howto/dither/jy/
        const unsigned dim = BAYER_SIZE;
        unsigned M = 0;
        for (unsigned i = dim; i >>= 1;) {
            M++;
        }
        for (unsigned y = 0; y < dim; ++y) {
            for (unsigned x = 0; x < dim; ++x) {
                unsigned v = 0, mask = M - 1, xc = x ^ y, yc = y;
                for (unsigned bit = 0; bit < 2 * M; --mask) {
                    v |= ((yc >> mask) & 1) << bit++;
                    v |= ((xc >> mask) & 1) << bit++;
                }
                preCalculatedBayer[x][y] = (v + 0.5f) / (dim * dim);
            }
        }

        D3D11_SUBRESOURCE_DATA initData;
        initData.pSysMem = preCalculatedBayer;
        initData.SysMemPitch = sizeof(preCalculatedBayer[0]);

        ID3D11Texture2D *tex;
        device->lpVtbl->CreateTexture2D(device, &desc, &initData, &tex);
        device->lpVtbl->CreateShaderResourceView(device, (ID3D11Resource *) tex, NULL, &bayerTextureView);
        tex->lpVtbl->Release(tex);
    }
    {
        D3D11_BUFFER_DESC constantBufferDesc = {};
        constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantBufferDesc.ByteWidth = 16;
        constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        device->lpVtbl->CreateBuffer(device, &constantBufferDesc, NULL, &constantBuffer);
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
    for (int i = 0; i < 2; i++) {
        RELEASE_IF_NOT_NULL(texture[i])
        RELEASE_IF_NOT_NULL(textureView[i])
    }
    RELEASE_IF_NOT_NULL(bayerSamplerState)
    RELEASE_IF_NOT_NULL(bayerTextureView)
    RELEASE_IF_NOT_NULL(constantBuffer)
    for (int i = 0; i < numLuts; i++) {
        RELEASE_IF_NOT_NULL(luts[i].textureView)
    }
}

bool ApplyLUT(void *cOverlayContext, IDXGISwapChain *swapChain, struct tagRECT *rects, int numRects) {
    if (!device) {
        InitializeStuff(swapChain);
    }

    ID3D11Texture2D *backBuffer;
    ID3D11RenderTargetView *renderTargetView;

    swapChain->lpVtbl->GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, (void **) &backBuffer);

    D3D11_TEXTURE2D_DESC newBackBufferDesc;
    backBuffer->lpVtbl->GetDesc(backBuffer, &newBackBufferDesc);

    int index = -1;
    if (newBackBufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        index = 0;
    } else if (newBackBufferDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        index = 1;
    }

    lutData *lut;
    if (index == -1 || !(lut = GetLUTDataFromCOverlayContext(cOverlayContext, index == 1))) {
        backBuffer->lpVtbl->Release(backBuffer);
        return false;
    }

    D3D11_TEXTURE2D_DESC oldTextureDesc = textureDesc[index];
    if (newBackBufferDesc.Width > oldTextureDesc.Width || newBackBufferDesc.Height > oldTextureDesc.Height) {
        if (texture[index] != NULL) {
            texture[index]->lpVtbl->Release(texture[index]);
            textureView[index]->lpVtbl->Release(textureView[index]);
        }

        UINT newWidth = max(newBackBufferDesc.Width, oldTextureDesc.Width);
        UINT newHeight = max(newBackBufferDesc.Height, oldTextureDesc.Height);

        D3D11_TEXTURE2D_DESC newTextureDesc;

        newTextureDesc = newBackBufferDesc;
        newTextureDesc.Width = newWidth;
        newTextureDesc.Height = newHeight;
        newTextureDesc.Usage = D3D11_USAGE_DEFAULT;
        newTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        newTextureDesc.CPUAccessFlags = 0;
        newTextureDesc.MiscFlags = 0;

        textureDesc[index] = newTextureDesc;

        device->lpVtbl->CreateTexture2D(device, &textureDesc[index], NULL, &texture[index]);
        device->lpVtbl->CreateShaderResourceView(device, (ID3D11Resource *) texture[index], NULL, &textureView[index]);
    }

    backBufferDesc = newBackBufferDesc;

    device->lpVtbl->CreateRenderTargetView(device, (ID3D11Resource *) backBuffer, NULL, &renderTargetView);

    deviceContext->lpVtbl->RSSetViewports(deviceContext, 1, &(D3D11_VIEWPORT) {0, 0, backBufferDesc.Width, backBufferDesc.Height, 0.0f, 1.0f});

    deviceContext->lpVtbl->OMSetRenderTargets(deviceContext, 1, &renderTargetView, NULL);
    renderTargetView->lpVtbl->Release(renderTargetView);

    deviceContext->lpVtbl->IASetPrimitiveTopology(deviceContext, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    deviceContext->lpVtbl->IASetInputLayout(deviceContext, inputLayout);

    deviceContext->lpVtbl->VSSetShader(deviceContext, vertexShader, NULL, 0);
    deviceContext->lpVtbl->PSSetShader(deviceContext, pixelShader, NULL, 0);

    deviceContext->lpVtbl->PSSetShaderResources(deviceContext, 0, 1, &textureView[index]);
    deviceContext->lpVtbl->PSSetShaderResources(deviceContext, 1, 1, &lut->textureView);
    deviceContext->lpVtbl->PSSetSamplers(deviceContext, 0, 1, &samplerState);

    deviceContext->lpVtbl->PSSetShaderResources(deviceContext, 2, 1, &bayerTextureView);
    deviceContext->lpVtbl->PSSetSamplers(deviceContext, 1, 1, &bayerSamplerState);

    int constantData[4] = {lut->size, index == 1};

    D3D11_MAPPED_SUBRESOURCE resource;
    deviceContext->lpVtbl->Map(deviceContext, (ID3D11Resource *) constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
    memcpy(resource.pData, constantData, sizeof(constantData));
    deviceContext->lpVtbl->Unmap(deviceContext, (ID3D11Resource *) constantBuffer, 0);

    deviceContext->lpVtbl->PSSetConstantBuffers(deviceContext, 0, 1, &constantBuffer);

    for (int i = 0; i < numRects; i++) {
        D3D11_BOX sourceRegion;
        sourceRegion.left = rects[i].left;
        sourceRegion.right = rects[i].right;
        sourceRegion.top = rects[i].top;
        sourceRegion.bottom = rects[i].bottom;
        sourceRegion.front = 0;
        sourceRegion.back = 1;

        deviceContext->lpVtbl->CopySubresourceRegion(deviceContext, (ID3D11Resource *) texture[index], 0, rects[i].left, rects[i].top, 0, (ID3D11Resource *) backBuffer, 0, &sourceRegion);
        DrawRectangle(&rects[i], index);
    }

    backBuffer->lpVtbl->Release(backBuffer);
    return true;
}

typedef struct rectVec {
    struct tagRECT *start;
    struct tagRECT *end;
    struct tagRECT *cap;
} rectVec;

typedef long(COverlayContext_Present_t)(void *, void *, unsigned int, rectVec *, unsigned int, bool);

COverlayContext_Present_t *COverlayContext_Present_orig;
COverlayContext_Present_t *COverlayContext_Present_real_orig;

long COverlayContext_Present_hook(void *this, void *overlaySwapChain, unsigned int a3, rectVec *rectVec, unsigned int a5, bool a6) {
    if (__builtin_return_address(0) < (void *) COverlayContext_Present_real_orig) {
        if (isWindows11 && *((bool *) overlaySwapChain + IOverlaySwapChain_HardwareProtected_offset_w11) ||
            !isWindows11 && *((bool *) overlaySwapChain + IOverlaySwapChain_HardwareProtected_offset)) {
            RemoveLUTActiveTarget(this);
        } else {
            IDXGISwapChain *swapChain;
            if (isWindows11) {
                swapChain = *(IDXGISwapChain **) ((unsigned char *) overlaySwapChain + IOverlaySwapChain_IDXGISwapChain_offset_w11);
            } else {
                swapChain = *(IDXGISwapChain **) ((unsigned char *) overlaySwapChain + IOverlaySwapChain_IDXGISwapChain_offset);
            }

            if (ApplyLUT(this, swapChain, rectVec->start, rectVec->end - rectVec->start)) {
                AddLUTActiveTarget(this);
            } else {
                RemoveLUTActiveTarget(this);
            }
        }
    }
    return COverlayContext_Present_orig(this, overlaySwapChain, a3, rectVec, a5, a6);
}

typedef bool(COverlayContext_IsCandidateDirectFlipCompatbile_t)(void *, void *, void *, void *, int, unsigned int, bool, bool);

COverlayContext_IsCandidateDirectFlipCompatbile_t *COverlayContext_IsCandidateDirectFlipCompatbile_orig;

bool COverlayContext_IsCandidateDirectFlipCompatbile_hook(void *this, void *a2, void *a3, void *a4, int a5, unsigned int a6, bool a7, bool a8) {
    if (IsLUTActiveTarget(this)) {
        return false;
    }
    return COverlayContext_IsCandidateDirectFlipCompatbile_orig(this, a2, a3, a4, a5, a6, a7, a8);
}

typedef bool(COverlayContext_OverlaysEnabled_t)(void *);

COverlayContext_OverlaysEnabled_t *COverlayContext_OverlaysEnabled_orig;

bool COverlayContext_OverlaysEnabled_hook(void *this) {
    if (IsLUTActiveTarget(this)) {
        return false;
    }
    return COverlayContext_OverlaysEnabled_orig(this);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH: {
            HMODULE dwmcore = GetModuleHandle("dwmcore.dll");
            MODULEINFO moduleInfo;
            GetModuleInformation(GetCurrentProcess(), dwmcore, &moduleInfo, sizeof(moduleInfo));

            unsigned char *KUSER_SHARED_DATA = (unsigned char *) 0x7FFE0000;
            ULONG NtBuildNumber = *(ULONG *) (KUSER_SHARED_DATA + 0x260);
            isWindows11 = NtBuildNumber >= 22000;

            if (isWindows11) {
                for (size_t i = 0; i <= moduleInfo.SizeOfImage - sizeof(COverlayContext_Present_bytes_w11); i++) {
                    unsigned char *address = (unsigned char *) dwmcore + i;
                    if (!COverlayContext_Present_orig && !memcmp(address, COverlayContext_Present_bytes_w11, sizeof(COverlayContext_Present_bytes_w11))) {
                        COverlayContext_Present_orig = (COverlayContext_Present_t *) (address - 0xf);
                        COverlayContext_Present_real_orig = COverlayContext_Present_orig;
                    } else if (!COverlayContext_IsCandidateDirectFlipCompatbile_orig && !memcmp(address, COverlayContext_IsCandidateDirectFlipCompatbile_bytes_w11, sizeof(COverlayContext_IsCandidateDirectFlipCompatbile_bytes_w11))) {
                        static int found = 0;
                        found++;
                        if (found == 2) {
                            COverlayContext_IsCandidateDirectFlipCompatbile_orig = (COverlayContext_IsCandidateDirectFlipCompatbile_t *) address;
                        }
                    } else if (!COverlayContext_OverlaysEnabled_orig && !memcmp(address, COverlayContext_OverlaysEnabled_bytes_w11, sizeof(COverlayContext_OverlaysEnabled_bytes_w11))) {
                        COverlayContext_OverlaysEnabled_orig = (COverlayContext_OverlaysEnabled_t *) (address - 0x7);
                    }
                    if (COverlayContext_Present_orig && COverlayContext_IsCandidateDirectFlipCompatbile_orig && COverlayContext_OverlaysEnabled_orig) {
                        break;
                    }
                }
            } else {
                for (size_t i = 0; i <= moduleInfo.SizeOfImage - sizeof(COverlayContext_Present_bytes); i++) {
                    unsigned char *address = (unsigned char *) dwmcore + i;
                    if (!COverlayContext_Present_orig && !memcmp(address, COverlayContext_Present_bytes, sizeof(COverlayContext_Present_bytes))) {
                        COverlayContext_Present_orig = (COverlayContext_Present_t *) address;
                        COverlayContext_Present_real_orig = COverlayContext_Present_orig;
                    } else if (!COverlayContext_IsCandidateDirectFlipCompatbile_orig && !memcmp(address, COverlayContext_IsCandidateDirectFlipCompatbile_bytes, sizeof(COverlayContext_IsCandidateDirectFlipCompatbile_bytes))) {
                        static int found = 0;
                        found++;
                        if (found == 2) {
                            COverlayContext_IsCandidateDirectFlipCompatbile_orig = (COverlayContext_IsCandidateDirectFlipCompatbile_t *) (address - 0xa);
                        }
                    } else if (!COverlayContext_OverlaysEnabled_orig && !memcmp(address, COverlayContext_OverlaysEnabled_bytes, sizeof(COverlayContext_OverlaysEnabled_bytes))) {
                        COverlayContext_OverlaysEnabled_orig = (COverlayContext_OverlaysEnabled_t *) (address - 0x7);
                    }
                    if (COverlayContext_Present_orig && COverlayContext_IsCandidateDirectFlipCompatbile_orig && COverlayContext_OverlaysEnabled_orig) {
                        break;
                    }
                }
            }

            char lutFolderPath[MAX_PATH];
            ExpandEnvironmentStringsA(LUT_FOLDER, lutFolderPath, sizeof(lutFolderPath));
            AddLUTs(lutFolderPath);

            if (COverlayContext_Present_orig && COverlayContext_IsCandidateDirectFlipCompatbile_orig && COverlayContext_OverlaysEnabled_orig && numLuts != 0) {
                MH_Initialize();
                MH_CreateHook((PVOID) COverlayContext_Present_orig, (PVOID) COverlayContext_Present_hook, (PVOID *) &COverlayContext_Present_orig);
                MH_CreateHook((PVOID) COverlayContext_IsCandidateDirectFlipCompatbile_orig, (PVOID) COverlayContext_IsCandidateDirectFlipCompatbile_hook, (PVOID *) &COverlayContext_IsCandidateDirectFlipCompatbile_orig);
                MH_CreateHook((PVOID) COverlayContext_OverlaysEnabled_orig, (PVOID) COverlayContext_OverlaysEnabled_hook, (PVOID *) &COverlayContext_OverlaysEnabled_orig);
                MH_EnableHook(MH_ALL_HOOKS);
                break;
            }
            return FALSE;
        }
        case DLL_PROCESS_DETACH:
            MH_Uninitialize();
            Sleep(100);
            UninitializeStuff();
            break;
        default:
            break;
    }
    return TRUE;
}
