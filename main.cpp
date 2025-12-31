// ---------------------------------------------------------------
// Minimal DX12 Triangle – full source
// ---------------------------------------------------------------
#include <windows.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <d3dcompiler.h>          // For D3DCompile()
#pragma comment(lib,"d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------
// Global DX12 objects
// ---------------------------------------------------------------
static ComPtr<ID3D12Device>          g_device;
static ComPtr<IDXGISwapChain3>       g_swapChain;
static ComPtr<ID3D12CommandQueue>    g_commandQueue;
static ComPtr<ID3D12CommandAllocator> g_commandAllocator;
static ComPtr<ID3D12GraphicsCommandList> g_commandList;

static ComPtr<ID3D12DescriptorHeap>  g_rtvHeap;

// Sync objects
static ComPtr<ID3D12Fence>           g_fence;
static UINT64                       g_fenceValue = 0;
static HANDLE                       g_fenceEvent = nullptr;

// Render targets
static ComPtr<ID3D12Resource>        g_renderTargets[2];
static UINT                         g_frameIndex = 0;

// ---------------------------------
// Root signature & PSO
// ---------------------------------
static ComPtr<ID3D12RootSignature>   g_rootSig;
static ComPtr<ID3D12PipelineState>   g_pipelineState;

// ---------------------------------
// Vertex buffer
// ---------------------------------
struct Vertex { float pos[3]; float col[4]; };
static ComPtr<ID3D12Resource>        g_vertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW     g_vbView;

// ---------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------
inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

ComPtr<ID3DBlob> CompileShader(const char* src, const char* entry, const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> byteCode, errors;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr,
                            nullptr, entry, target, flags, 0,
                            &byteCode, &errors);
    if (FAILED(hr))
    {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        throw std::runtime_error("Shader compilation failed");
    }
    return byteCode;
}

// ---------------------------------------------------------------
// Device / SwapChain creation
// ---------------------------------------------------------------
void InitD3D12(HWND hwnd)
{
    // Factory
    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    // Device
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&g_device)));

    // Command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue)));

    // Swap chain
    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.BufferCount       = 2;
    swapDesc.Width             = 800;
    swapDesc.Height            = 600;
    swapDesc.Format            = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count  = 1;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        g_commandQueue.Get(),
        hwnd,
        &swapDesc,
        nullptr,
        nullptr,
        &swapChain1));

    ThrowIfFailed(swapChain1.As(&g_swapChain));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    // RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = swapDesc.BufferCount;
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap)));

    // Render target views
    UINT rtvSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; ++i)
    {
        ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])));
        g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvSize;
    }

    // Command allocator & list
    ThrowIfFailed(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&g_commandAllocator)));
    ThrowIfFailed(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              g_commandAllocator.Get(), nullptr,
                                              IID_PPV_ARGS(&g_commandList)));
    g_commandList->Close();

    // Fence
    ThrowIfFailed(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
    g_fenceValue = 1;
    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!g_fenceEvent) throw std::runtime_error("Failed to create fence event");

    // ----------------------------------------------------------------
    // New objects – root signature, PSO and vertex buffer
    // ----------------------------------------------------------------

    /* Root signature */
    {
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 0;
        desc.pParameters   = nullptr;
        desc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signatureBlob, errorBlob;
        HRESULT hr = D3D12SerializeRootSignature(&desc,
                                                 D3D_ROOT_SIGNATURE_VERSION_1,
                                                 &signatureBlob, &errorBlob);
        if (FAILED(hr))
            throw std::runtime_error("Failed to serialize root signature");

        ThrowIfFailed(g_device->CreateRootSignature(
            0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
            IID_PPV_ARGS(&g_rootSig)));
    }

    /* Shaders */
    const char* vsSource = R"(
struct VSInput
{
    float3 pos : POSITION;
    float4 col : COLOR0;
};

struct PSOutput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};

PSOutput main(VSInput input)
{
    PSOutput output;
    output.pos = float4(input.pos, 1.0);
    output.col = input.col;
    return output;
}
)";

    const char* psSource = R"(
struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.col;
}
)";

    auto vsBlob = CompileShader(vsSource, "main", "vs_5_0");
    auto psBlob = CompileShader(psSource, "main", "ps_5_0");

    /* PSO */
    {
        D3D12_INPUT_ELEMENT_DESC inputLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
              offsetof(Vertex, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
              offsetof(Vertex, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.InputLayout       = { inputLayout, _countof(inputLayout) };
        psoDesc.pRootSignature    = g_rootSig.Get();
        psoDesc.VS                = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psoDesc.PS                = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        psoDesc.RasterizerState   = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState        = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask        = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets  = 1;
        psoDesc.RTVFormats[0]     = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count  = 1;

        ThrowIfFailed(g_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_pipelineState)));
    }

    /* Vertex buffer */
    {
        Vertex vertices[] =
        {
            { { 0.0f,  0.5f, 0.0f }, {1.f, 0.f, 0.f, 1.f} },
            { {-0.5f,-0.5f, 0.0f }, {0.f, 1.f, 0.f, 1.f} },
            { { 0.5f,-0.5f, 0.0f }, {0.f, 0.f, 1.f, 1.f} }
        };
        const UINT vbSize = sizeof(vertices);

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type                 = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension         = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width             = vbSize;
        bufferDesc.Height            = 1;
        bufferDesc.DepthOrArraySize  = 1;
        bufferDesc.MipLevels         = 1;
        bufferDesc.SampleDesc.Count  = 1;
        bufferDesc.Layout            = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ThrowIfFailed(g_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&g_vertexBuffer)));

        // Copy data
        UINT8* pData;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(g_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
        memcpy(pData, vertices, vbSize);
        g_vertexBuffer->Unmap(0, nullptr);

        // View
        g_vbView.BufferLocation = g_vertexBuffer->GetGPUVirtualAddress();
        g_vbView.StrideInBytes  = sizeof(Vertex);
        g_vbView.SizeInElements = _countof(vertices);
    }
}

// ---------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------
void Render()
{
    const float clearColor[] = { 0.2f, 0.4f, 0.6f, 1.0f };

    ThrowIfFailed(g_commandAllocator->Reset());
    ThrowIfFailed(g_commandList->Reset(g_commandAllocator.Get(), g_pipelineState.Get()));

    // Viewport & scissor
    D3D12_VIEWPORT vp{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f};
    D3D12_RECT   scissor{0, 0, 800, 600};
    g_commandList->RSSetViewports(1, &vp);
    g_commandList->RSSetScissorRects(1, &scissor);

    // Render target
    UINT rtvSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += g_frameIndex * rtvSize;
    g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Clear
    g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // Draw triangle
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_commandList->IASetVertexBuffers(0, 1, &g_vbView);
    g_commandList->DrawInstanced(3, 1, 0, 0);

    ThrowIfFailed(g_commandList->Close());

    ID3D12CommandList* lists[] = { g_commandList.Get() };
    g_commandQueue->ExecuteCommandLists(_countof(lists), lists);

    ThrowIfFailed(g_swapChain->Present(1, 0));

    // GPU‑CPU sync
    ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), g_fenceValue));
    if (g_fence->GetCompletedValue() < g_fenceValue)
    {
        ThrowIfFailed(g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent));
        WaitForSingleObject(g_fenceEvent, INFINITE);
    }
    ++g_fenceValue;
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

// ---------------------------------------------------------------
// Win32 window
// ---------------------------------------------------------------
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"DX12WindowClass";
    WNDCLASS wc{};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"Triangle DX12",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    InitD3D12(hwnd);

    MSG msg{};
    while (true)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) goto cleanup;
        }
        Render();
    }

cleanup:
    CloseHandle(g_fenceEvent);
    return 0;
}
