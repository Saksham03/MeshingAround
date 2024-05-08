//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx_render.h"
#include "D3D12MeshletRender.h"

const wchar_t* D3D12MeshletRender::c_meshFilename = L"..\\Assets\\Dragon_LOD0.bin";
//const wchar_t* D3D12MeshletRender::c_meshFilename = L"..\\Assets\\wahoo.bin";
const wchar_t* D3D12MeshletRender::c_meshObjFilename = L"..\\Assets\\wahoo.obj";

const wchar_t* D3D12MeshletRender::c_meshShaderFilename = L"MeshletMS.cso";
const wchar_t* D3D12MeshletRender::c_pixelShaderFilename = L"MeshletPS.cso";

const wchar_t* D3D12MeshletRender::c_tessAmpShaderFilename = L"TessAS.cso";
const wchar_t* D3D12MeshletRender::c_tessMeshShaderFilename = L"TessMS.cso";


// Ray casting intersection tests for determining meshlet picking.

bool RayIntersectSphere(FXMVECTOR o, FXMVECTOR d, FXMVECTOR s)
{
    XMVECTOR l = o - s;
    XMVECTOR r = XMVectorSplatW(s);
    XMVECTOR a = XMVector3Dot(d, d);
    XMVECTOR b = 2.0 * XMVector3Dot(l, d);
    XMVECTOR c = XMVector3Dot(l, l) - (r * r);

    XMVECTOR disc = b * b - 4 * a * c;
    return !XMVector4Less(disc, g_XMZero);
}

XMVECTOR RayIntersectTriangle(FXMVECTOR o, FXMVECTOR d, FXMVECTOR p0, GXMVECTOR p1, HXMVECTOR p2)
{
    XMVECTOR edge1, edge2, h, s, q;
    XMVECTOR a, f, u, v;
    edge1 = p1 - p0;
    edge2 = p2 - p0;

    h = XMVector3Cross(d, edge2);
    a = XMVector3Dot(edge1, h);
    if (XMVector4Less(XMVectorAbs(a), g_XMEpsilon))
        return g_XMQNaN;

    f = g_XMOne / a;
    s = o - p0;
    u = f * XMVector3Dot(s, h);
    if (XMVector4Less(u, g_XMZero) || XMVector4Greater(u, g_XMOne))
        return g_XMQNaN;

    q = XMVector3Cross(s, edge1);
    v = f * XMVector3Dot(d, q);
    if (XMVector4Less(v, g_XMZero) || XMVector4Greater(u + v, g_XMOne))
        return g_XMQNaN;

    // At this stage we can compute t to find out where the intersection point is on the line.
    XMVECTOR t = f * XMVector3Dot(edge2, q);
    if (XMVector4Greater(t, g_XMEpsilon) && XMVector4Less(t, XMVectorReciprocal(g_XMEpsilon))) // ray intersection
    {
        return t;
    }

    return g_XMQNaN; // This means that there is a line intersection but not a ray intersection.
}

D3D12MeshletRender::D3D12MeshletRender(UINT width, UINT height, std::wstring name)
    : DXSample(width, height, name)
    , m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
    , m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height))
    , m_rtvDescriptorSize(0)
    , m_dsvDescriptorSize(0)
    , m_constantBufferData{}
    , m_cbvDataBegin(nullptr)
    , m_frameIndex(0)
    , m_frameCounter(0)
    , m_highlightedIndex(-1)
    , m_fenceEvent{}
    , m_fenceValues{}
{ }

void D3D12MeshletRender::OnInit()
{
    m_camera.Init({ 0, 75, 150 });
    m_camera.SetMoveSpeed(150.0f);

    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12MeshletRender::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter, true);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_5 };
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))
        || (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_5))
    {
        OutputDebugStringA("ERROR: Shader Model 6.5 is not supported\n");
        throw std::exception("Shader Model 6.5 is not supported");
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {};
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features)))
        || (features.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED))
    {
        OutputDebugStringA("ERROR: Mesh Shaders aren't supported!\n");
        throw std::exception("Mesh Shaders aren't supported!");
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
        ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

        m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV and a command allocator for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
        }
    }

    // Create the depth stencil view.
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
        depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        const CD3DX12_HEAP_PROPERTIES depthStencilHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC depthStencilTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &depthStencilHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthStencilTextureDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&m_depthStencil)
        ));

        NAME_D3D12_OBJECT(m_depthStencil);

        m_device->CreateDepthStencilView(m_depthStencil.Get(), &depthStencilDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // Create the constant buffer.
    {
        const UINT64 constantBufferSize = sizeof(SceneConstantBuffer) * FrameCount;

        const CD3DX12_HEAP_PROPERTIES constantBufferHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &constantBufferHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &constantBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_constantBuffer)));

        // Describe and create a constant buffer view.
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = constantBufferSize;

        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_cbvDataBegin)));
    }
}

// Load the sample assets.
void D3D12MeshletRender::LoadAssets()
{
    // Create the pipeline state, which includes compiling and loading shaders.
    {
        struct 
        { 
            byte* data; 
            uint32_t size; 
        } meshShader, pixelShader;

        ReadDataFromFile(GetAssetFullPath(c_meshShaderFilename).c_str(), &meshShader.data, &meshShader.size);
        ReadDataFromFile(GetAssetFullPath(c_pixelShaderFilename).c_str(), &pixelShader.data, &pixelShader.size);

        // Pull root signature from the precompiled mesh shader.
        ThrowIfFailed(m_device->CreateRootSignature(0, meshShader.data, meshShader.size, IID_PPV_ARGS(&m_meshletRootSignature)));

        D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature        = m_meshletRootSignature.Get();
        psoDesc.MS                    = { meshShader.data, meshShader.size };
        psoDesc.PS                    = { pixelShader.data, pixelShader.size };
        psoDesc.NumRenderTargets      = 1;
        psoDesc.RTVFormats[0]         = m_renderTargets[0]->GetDesc().Format;
        psoDesc.DSVFormat             = m_depthStencil->GetDesc().Format;
        psoDesc.RasterizerState       = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);    // CW front; cull back
        psoDesc.BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT);         // Opaque
        psoDesc.DepthStencilState     = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // Less-equal depth test w/ writes; no stencil
        psoDesc.SampleMask            = UINT_MAX;
        psoDesc.SampleDesc            = DefaultSampleDesc();

        auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);

        D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
        streamDesc.pPipelineStateSubobjectStream = &psoStream;
        streamDesc.SizeInBytes                   = sizeof(psoStream);

        ThrowIfFailed(m_device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_meshletPipelineState)));
    }

    // Create the second pipeline state for the tessellation pipeline
    {
        struct
        {
            byte* data;
            uint32_t size;
        } tessAmpShader, tessMeshShader, pixelShader;

        ReadDataFromFile(GetAssetFullPath(c_tessAmpShaderFilename).c_str(), &tessAmpShader.data, &tessAmpShader.size);
        ReadDataFromFile(GetAssetFullPath(c_tessMeshShaderFilename).c_str(), &tessMeshShader.data, &tessMeshShader.size);
        ReadDataFromFile(GetAssetFullPath(c_pixelShaderFilename).c_str(), &pixelShader.data, &pixelShader.size);

        // Pull root signature from the precompiled mesh shader.
        ThrowIfFailed(m_device->CreateRootSignature(0, tessAmpShader.data, tessAmpShader.size, IID_PPV_ARGS(&m_tessRootSignature)));

        D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_tessRootSignature.Get();
        psoDesc.AS = { tessAmpShader.data, tessAmpShader.size };
        psoDesc.MS = { tessMeshShader.data, tessMeshShader.size };
        psoDesc.PS = { pixelShader.data, pixelShader.size };
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = m_renderTargets[0]->GetDesc().Format;
        psoDesc.DSVFormat = m_depthStencil->GetDesc().Format;
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);    // CW front; cull back
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);         // Opaque
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // Less-equal depth test w/ writes; no stencil
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.SampleDesc = DefaultSampleDesc();
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);

        D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
        streamDesc.pPipelineStateSubobjectStream = &psoStream;
        streamDesc.SizeInBytes = sizeof(psoStream);

        ThrowIfFailed(m_device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_tessPipelineState)));
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), m_meshletPipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());

    m_model.LoadFromFile(c_meshFilename);
    //m_model.CreateMeshletsFromFile(c_meshObjFilename);
    m_model.UploadGpuResources(m_device.Get(), m_commandQueue.Get(), m_commandAllocators[m_frameIndex].Get(), m_commandList.Get());

#ifdef _DEBUG
    // Mesh shader file expects a certain vertex layout; assert our mesh conforms to that layout.
    const D3D12_INPUT_ELEMENT_DESC c_elementDescs[2] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
    };

    for (auto& mesh : m_model)
    {
        assert(mesh.LayoutDesc.NumElements == 2);

        for (uint32_t i = 0; i < _countof(c_elementDescs); ++i)
            assert(std::memcmp(&mesh.LayoutElems[i], &c_elementDescs[i], sizeof(D3D12_INPUT_ELEMENT_DESC)) == 0);
    }
#endif
    
    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValues[m_frameIndex]++;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForGpu();
    }
}

// Update frame-based values.
void D3D12MeshletRender::OnUpdate()
{
    m_timer.Tick(NULL);

    if (m_frameCounter++ % 30 == 0)
    {
        // Update window text with FPS value.
        wchar_t fps[64];
        swprintf_s(fps, L"%ufps", m_timer.GetFramesPerSecond());
        SetCustomWindowText(fps);
    }

    m_camera.Update(static_cast<float>(m_timer.GetElapsedSeconds()));

    XMMATRIX world = XMMATRIX(g_XMIdentityR0, g_XMIdentityR1, g_XMIdentityR2, g_XMIdentityR3);
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX proj = m_camera.GetProjectionMatrix(XM_PI / 3.0f, m_aspectRatio);
    
    XMStoreFloat4x4(&m_constantBufferData.World, XMMatrixTranspose(world));
    XMStoreFloat4x4(&m_constantBufferData.WorldView, XMMatrixTranspose(world * view));
    XMStoreFloat4x4(&m_constantBufferData.WorldViewProj, XMMatrixTranspose(world * view * proj));
    m_constantBufferData.DrawMeshlets = true;

    raycastToPickClickedMeshlet();
    m_constantBufferData.HighlightedIndex = m_highlightedIndex;

    memcpy(m_cbvDataBegin + sizeof(SceneConstantBuffer) * m_frameIndex, &m_constantBufferData, sizeof(m_constantBufferData));
}

// Render the scene.
void D3D12MeshletRender::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));

    MoveToNextFrame();
}

void D3D12MeshletRender::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    CloseHandle(m_fenceEvent);
}

void D3D12MeshletRender::OnKeyDown(UINT8 key)
{
    m_camera.OnKeyDown(key);
}

void D3D12MeshletRender::OnKeyUp(UINT8 key)
{
    m_camera.OnKeyUp(key);
}

void D3D12MeshletRender::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_meshletPipelineState.Get()));

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_meshletRootSignature.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    const auto toRenderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &toRenderTargetBarrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + sizeof(SceneConstantBuffer) * m_frameIndex);

    for (auto& mesh : m_model)
    {
        
        auto meshTessFlagsBarrier = CD3DX12_RESOURCE_BARRIER::Transition(mesh.TessFlagsResource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        m_commandList->ResourceBarrier(1, &meshTessFlagsBarrier);
        m_commandList->CopyResource(mesh.TessFlagsResource.Get(), mesh.tessFlagsUpload.Get());
        meshTessFlagsBarrier = CD3DX12_RESOURCE_BARRIER::Transition(mesh.TessFlagsResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
        m_commandList->ResourceBarrier(1, &meshTessFlagsBarrier);
        
        m_commandList->SetGraphicsRoot32BitConstant(1, mesh.IndexSize, 0);
        m_commandList->SetGraphicsRootShaderResourceView(2, mesh.VertexResources[0]->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(3, mesh.MeshletResource->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(4, mesh.UniqueVertexIndexResource->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(5, mesh.PrimitiveIndexResource->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(6, mesh.TessFlagsResource->GetGPUVirtualAddress());

        for (auto& subset : mesh.MeshletSubsets)
        {
            m_commandList->SetGraphicsRoot32BitConstant(1, subset.Offset, 1);
            m_commandList->DispatchMesh(subset.Count, 1, 1);
        }
    }

    m_commandList->SetPipelineState(m_tessPipelineState.Get());
    int totaltricount = 0;
    /*for (auto& mesh : m_model)
    {
                       
        m_commandList->SetGraphicsRoot32BitConstant(1, mesh.IndexSize, 0);
        m_commandList->SetGraphicsRootShaderResourceView(2, mesh.VertexResources[0]->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(3, mesh.MeshletResource->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(4, mesh.UniqueVertexIndexResource->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(5, mesh.PrimitiveIndexResource->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(6, mesh.tessFlagsUpload->GetGPUVirtualAddress());
        
        for(int i = 0; i < mesh.Meshlets.size(); i++)
        {            
            if (mesh.TessellateMeshletFlags[i] == 1u)
            {
                m_commandList->SetGraphicsRoot32BitConstant(1, i, 1);
                m_commandList->DispatchMesh(mesh.Meshlets[i].PrimCount, 1, 1);
                totaltricount += mesh.Meshlets[i].PrimCount;
            }
        }
    }*/
    
    // Indicate that the back buffer will now be used to present.
    const auto toPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &toPresentBarrier);

    ThrowIfFailed(m_commandList->Close());
}

// Wait for pending GPU work to complete.
void D3D12MeshletRender::WaitForGpu()
{
    // Schedule a Signal command in the queue.
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

    // Wait until the fence has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void D3D12MeshletRender::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}


void D3D12MeshletRender::raycastToPickClickedMeshlet()
{
    POINT point;
    if (!GetCursorPos(&point) || !ScreenToClient(Win32Application::GetHwnd(), &point) || !(GetKeyState(RI_MOUSE_LEFT_BUTTON_DOWN) & 0x8000))
        return;    

    // Grab the world, view, & proj matrices
    XMMATRIX world = XMMATRIX(g_XMIdentityR0, g_XMIdentityR1, g_XMIdentityR2, g_XMIdentityR3);
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX proj = m_camera.GetProjectionMatrix(XM_PI / 3.0f, m_aspectRatio);

    XMVECTOR sampleSS = XMVectorSet(float(point.x), float(point.y), 1, 1); //pixel coordinates of the mouse click
    XMVECTOR sampleWS = XMVector3Unproject(sampleSS, 0, 0, float(m_width), float(m_height), 0, 1, proj, view, XMMatrixIdentity()); //transformed into world-space
    XMVECTOR rayOrigin = XMVector3Transform(g_XMZero, XMMatrixInverse(nullptr, view)); //to go to world-space from camera space, transform the origin by the inverse of camera's view
    XMVECTOR rayDirection = XMVector3Normalize(sampleWS - rayOrigin);

    XMVECTOR minT = g_XMFltMax;

    auto& mesh = m_model.GetMesh(0);
    for (uint32_t i = 0; i < static_cast<uint32_t>(mesh.Meshlets.size()); ++i)
    {
        auto& meshlet = mesh.Meshlets[i];
        auto& cull = mesh.CullingData[i];

        // Quick narrow-phase test against the meshlet's sphere bounds.
        if (!RayIntersectSphere(rayOrigin, rayDirection, XMLoadFloat4(&cull.BoundingSphere)))
        {
            continue;
        }
        // Test each triangle of the meshlet.
        const uint8_t* vbMem = mesh.Vertices[0].data();
        uint32_t stride = mesh.VertexStrides[0];

        for (uint32_t j = 0; j < meshlet.PrimCount; ++j)
        {
            uint32_t i0, i1, i2;            
            mesh.GetPrimitive(meshlet.PrimOffset + j, i0, i1, i2);

            uint32_t v0 = mesh.GetVertexIndex(meshlet.VertOffset + i0);
            uint32_t v1 = mesh.GetVertexIndex(meshlet.VertOffset + i1);
            uint32_t v2 = mesh.GetVertexIndex(meshlet.VertOffset + i2);

            XMVECTOR p0 = XMLoadFloat3((XMFLOAT3*)(vbMem + v0 * stride));
            XMVECTOR p1 = XMLoadFloat3((XMFLOAT3*)(vbMem + v1 * stride));
            XMVECTOR p2 = XMLoadFloat3((XMFLOAT3*)(vbMem + v2 * stride));

            XMVECTOR t = RayIntersectTriangle(rayOrigin, rayDirection, p0, p1, p2);
            if (XMVector4Less(t, minT))
            {
                minT = t;
                m_highlightedIndex = i;
                mesh.TessellateMeshletFlags[i] = 1u;
                {
                    uint8_t* memory = nullptr;
                    mesh.tessFlagsUpload->Map(0, nullptr, reinterpret_cast<void**>(&memory));
                    std::memcpy(memory, mesh.TessellateMeshletFlags.data(), mesh.TessellateMeshletFlags.size() * sizeof(mesh.TessellateMeshletFlags[0]));
                    mesh.tessFlagsUpload->Unmap(0, nullptr);
                }
            }
        }
    }
}
