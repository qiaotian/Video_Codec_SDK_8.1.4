////////////////////////////////////////////////////////////////////////////
//
// Copyright 1993-2017 NVIDIA Corporation.  All rights reserved.
//
// Please refer to the NVIDIA end user license agreement (EULA) associated
// with this source code for terms and conditions that govern your use of
// this software. Any use, reproduction, disclosure, or distribution of
// this software and related documentation outside the terms of the EULA
// is strictly prohibited.
//
////////////////////////////////////////////////////////////////////////////

#include "../common/inc/nvEncodeAPI.h"
#include "../common/inc/nvUtils.h"
#include "NvEncoderPerf.h"
#include "../common/inc/nvFileIO.h"

#define BITSTREAM_BUFFER_SIZE 2 * 1024 * 1024
#define MAX_FRAMES_TO_PRELOAD 60

void CNvEncoderPerf::ConvertYUVpitchToNV12(unsigned char *yuv_luma, unsigned char *yuv_cb, unsigned char *yuv_cr, int width, int height, int index)
{
    uint32_t lockedPitch;
    unsigned char *pInputSurface;
    
    m_pNvHWEncoder->NvEncLockInputBuffer(m_stEncodeBuffer[index].stInputBfr.hInputSurface, (void**)&pInputSurface, &lockedPitch);
    
    unsigned char *pInputSurfaceCh = pInputSurface + (m_stEncodeBuffer[index].stInputBfr.dwHeight*lockedPitch);
    int y;
    int x;
    if (width == 0)
        width = width;
    if (lockedPitch == 0)
        lockedPitch = width;

    for (y = 0; y < height; y++)
    {
        memcpy(pInputSurface + (lockedPitch*y), yuv_luma + (width*y), width);
    }

    for (y = 0; y < height / 2; y++)
    {
        for (x = 0; x < width; x = x + 2)
        {
            pInputSurfaceCh[(y*lockedPitch) + x] = yuv_cb[((width / 2)*y) + (x >> 1)];
            pInputSurfaceCh[(y*lockedPitch) + (x + 1)] = yuv_cr[((width / 2)*y) + (x >> 1)];
        }
    }
    m_pNvHWEncoder->NvEncUnlockInputBuffer(m_stEncodeBuffer[index].stInputBfr.hInputSurface);
}

void CNvEncoderPerf::ConvertYUVpitchToYUV444(unsigned char *yuv_luma, unsigned char *yuv_cb, unsigned char *yuv_cr, int width, int height, int index)
{
    uint32_t lockedPitch;
    unsigned char *pInputSurface;
    
    m_pNvHWEncoder->NvEncLockInputBuffer(m_stEncodeBuffer[index].stInputBfr.hInputSurface, (void**)&pInputSurface, &lockedPitch);
    if (lockedPitch == 0)
        lockedPitch = width;

    unsigned char *pInputSurfaceCb = pInputSurface + (m_stEncodeBuffer[index].stInputBfr.dwHeight*lockedPitch);
    unsigned char *pInputSurfaceCr = pInputSurfaceCb + (m_stEncodeBuffer[index].stInputBfr.dwHeight*lockedPitch);
    for (int h = 0; h < height; h++)
    {
        memcpy(pInputSurface + lockedPitch * h, yuv_luma + width * h, width);
        memcpy(pInputSurfaceCb + lockedPitch * h, yuv_cb + width * h, width);
        memcpy(pInputSurfaceCr + lockedPitch * h, yuv_cr + width * h, width);
    }

    m_pNvHWEncoder->NvEncUnlockInputBuffer(m_stEncodeBuffer[index].stInputBfr.hInputSurface);
}

void CNvEncoderPerf::ConvertYUV10pitchtoP010PL(unsigned short *yuv_luma, unsigned short *yuv_cb, unsigned short *yuv_cr, int width, int height, int index)
{
    uint32_t dstStride;
    unsigned short *nv12_luma;

    m_pNvHWEncoder->NvEncLockInputBuffer(m_stEncodeBuffer[index].stInputBfr.hInputSurface, (void**)&nv12_luma, &dstStride);
    if (dstStride == 0)
        dstStride = width;

    unsigned short *nv12_chroma = (unsigned short *)((unsigned char *)nv12_luma + (m_stEncodeBuffer[index].stInputBfr.dwHeight*dstStride));

    int x, y;

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            nv12_luma[(y*dstStride / 2) + x] = yuv_luma[(width*y) + x] << 6;
        }
    }

    for (y = 0; y < height / 2; y++)
    {
        for (x = 0; x < width; x = x + 2)
        {
            nv12_chroma[(y*dstStride / 2) + x] = yuv_cb[((width / 2)*y) + (x >> 1)] << 6;
            nv12_chroma[(y*dstStride / 2) + (x + 1)] = yuv_cr[((width / 2)*y) + (x >> 1)] << 6;
        }
    }

    m_pNvHWEncoder->NvEncUnlockInputBuffer(m_stEncodeBuffer[index].stInputBfr.hInputSurface);
}

void CNvEncoderPerf::ConvertYUV10pitchtoYUV444(unsigned short *yuv_luma, unsigned short *yuv_cb, unsigned short *yuv_cr, int width, int height, int index)
{
    uint32_t dstStride;
    unsigned short *surf_luma;

    m_pNvHWEncoder->NvEncLockInputBuffer(m_stEncodeBuffer[index].stInputBfr.hInputSurface, (void**)&surf_luma, &dstStride);
    if (dstStride == 0)
        dstStride = width;

    unsigned short *surf_cb = (unsigned short *)((unsigned char *)surf_luma + (m_stEncodeBuffer[index].stInputBfr.dwHeight*dstStride));
    unsigned short *surf_cr = (unsigned short *)((unsigned char *)surf_cb + (m_stEncodeBuffer[index].stInputBfr.dwHeight*dstStride));

    int x, y;

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            surf_luma[(y*dstStride / 2) + x] = yuv_luma[(width*y) + x] << 6;
            surf_cb[(y*dstStride / 2) + x] = yuv_cb[(width*y) + x] << 6;
            surf_cr[(y*dstStride / 2) + x] = yuv_cr[(width*y) + x] << 6;
        }
    }

    m_pNvHWEncoder->NvEncUnlockInputBuffer(m_stEncodeBuffer[index].stInputBfr.hInputSurface);
}

CNvEncoderPerf::CNvEncoderPerf()
{
    m_pNvHWEncoder = new CNvHWEncoder;
    m_pDevice = NULL;
#if defined (NV_WINDOWS)
    m_pD3D = NULL;
#endif
    m_cuContext = NULL;

    m_uEncodeBufferCount = 0;
    memset(&m_stEncoderInput, 0, sizeof(m_stEncoderInput));
    memset(&m_stEOSOutputBfr, 0, sizeof(m_stEOSOutputBfr));

    memset(&m_stEncodeBuffer, 0, sizeof(m_stEncodeBuffer));
}

CNvEncoderPerf::~CNvEncoderPerf()
{
    if (m_pNvHWEncoder)
    {
        delete m_pNvHWEncoder;
        m_pNvHWEncoder = NULL;
    }
}

NVENCSTATUS CNvEncoderPerf::InitCuda(uint32_t deviceID)
{
    CUresult cuResult;
    CUdevice device;
    CUcontext cuContextCurr;
    int  deviceCount = 0;
    int  SMminor = 0, SMmajor = 0;

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
    typedef HMODULE CUDADRIVER;
#else
    typedef void *CUDADRIVER;
#endif
    CUDADRIVER hHandleDriver = 0;

    cuResult = cuInit(0, __CUDA_API_VERSION, hHandleDriver);
    if (cuResult != CUDA_SUCCESS)
    {
        PRINTERR("cuInit error:0x%x\n", cuResult);
        assert(0);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    cuResult = cuDeviceGetCount(&deviceCount);
    if (cuResult != CUDA_SUCCESS)
    {
        PRINTERR("cuDeviceGetCount error:0x%x\n", cuResult);
        assert(0);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    // If dev is negative value, we clamp to 0
    if ((int)deviceID < 0)
        deviceID = 0;

    if (deviceID >(unsigned int)deviceCount - 1)
    {
        PRINTERR("Invalid Device Id = %d\n", deviceID);
        return NV_ENC_ERR_INVALID_ENCODERDEVICE;
    }

    cuResult = cuDeviceGet(&device, deviceID);
    if (cuResult != CUDA_SUCCESS)
    {
        PRINTERR("cuDeviceGet error:0x%x\n", cuResult);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    cuResult = cuDeviceGetAttribute(&SMmajor,
                                    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,
                                    device);
    if (cuResult != CUDA_SUCCESS)
    {
        PRINTERR("cuDeviceGetAttribute error:0x%x\n", cuResult);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    cuResult = cuDeviceGetAttribute(&SMminor,
                                    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,
                                    device);
    if (cuResult != CUDA_SUCCESS)
    {
        PRINTERR("cuDeviceGetAttribute error:0x%x\n", cuResult);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    if (((SMmajor << 4) + SMminor) < 0x30)
    {
        PRINTERR("GPU %d does not have NVENC capabilities exiting\n", deviceID);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    cuResult = cuCtxCreate((CUcontext*)(&m_pDevice), 0, device);
    if (cuResult != CUDA_SUCCESS)
    {
        PRINTERR("cuCtxCreate error:0x%x\n", cuResult);
        assert(0);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    cuResult = cuCtxPopCurrent(&cuContextCurr);
    if (cuResult != CUDA_SUCCESS)
    {
        PRINTERR("cuCtxPopCurrent error:0x%x\n", cuResult);
        assert(0);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }
    return NV_ENC_SUCCESS;
}

#if defined(NV_WINDOWS)
NVENCSTATUS CNvEncoderPerf::InitD3D9(uint32_t deviceID)
{
    D3DPRESENT_PARAMETERS d3dpp;
    D3DADAPTER_IDENTIFIER9 adapterId;
    unsigned int iAdapter = NULL;
    HRESULT hr = S_OK;

    m_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (m_pD3D == NULL)
    {
        assert(m_pD3D);
        return NV_ENC_ERR_OUT_OF_MEMORY;;
    }

    if (deviceID >= m_pD3D->GetAdapterCount())
    {
        PRINTERR("Invalid Device Id = %d. Please use DX10/DX11 to detect headless video devices.\n", deviceID);
        return NV_ENC_ERR_INVALID_ENCODERDEVICE;
    }

    hr = m_pD3D->GetAdapterIdentifier(deviceID, 0, &adapterId);
    if (hr != S_OK)
    {
        PRINTERR("Invalid Device Id = %d\n", deviceID);
        return NV_ENC_ERR_INVALID_ENCODERDEVICE;
    }

    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferWidth = 640;
    d3dpp.BackBufferHeight = 480;
    d3dpp.BackBufferCount = 1;
    d3dpp.SwapEffect = D3DSWAPEFFECT_COPY;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dpp.Flags = D3DPRESENTFLAG_VIDEO;//D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    DWORD dwBehaviorFlags = D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING;

    hr = m_pD3D->CreateDevice(deviceID,
        D3DDEVTYPE_HAL,
        GetDesktopWindow(),
        dwBehaviorFlags,
        &d3dpp,
        (IDirect3DDevice9**)(&m_pDevice));

    if (FAILED(hr))
        return NV_ENC_ERR_OUT_OF_MEMORY;

    return  NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderPerf::InitD3D10(uint32_t deviceID)
{
    HRESULT hr;
    IDXGIFactory * pFactory = NULL;
    IDXGIAdapter * pAdapter;

    if (CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory) != S_OK)
    {
        return NV_ENC_ERR_GENERIC;
    }

    if (pFactory->EnumAdapters(deviceID, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        hr = D3D10CreateDevice(pAdapter, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            D3D10_SDK_VERSION, (ID3D10Device**)(&m_pDevice));
        if (FAILED(hr))
        {
            PRINTERR("Invalid Device Id = %d\n", deviceID);
            return NV_ENC_ERR_OUT_OF_MEMORY;
        }
    }
    else
    {
        PRINTERR("Invalid Device Id = %d\n", deviceID);
        return NV_ENC_ERR_INVALID_ENCODERDEVICE;
    }

    return  NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderPerf::InitD3D11(uint32_t deviceID)
{
    HRESULT hr;
    IDXGIFactory * pFactory = NULL;
    IDXGIAdapter * pAdapter;

    if (CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory) != S_OK)
    {
        return NV_ENC_ERR_GENERIC;
    }

    if (pFactory->EnumAdapters(deviceID, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        hr = D3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0,
            NULL, 0, D3D11_SDK_VERSION, (ID3D11Device**)(&m_pDevice), NULL, NULL);
        if (FAILED(hr))
        {
            PRINTERR("Invalid Device Id = %d\n", deviceID);
            return NV_ENC_ERR_OUT_OF_MEMORY;
        }
    }
    else
    {
        PRINTERR("Invalid Device Id = %d\n", deviceID);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    return  NV_ENC_SUCCESS;
}
#endif

NVENCSTATUS CNvEncoderPerf::AllocateIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight, NV_ENC_BUFFER_FORMAT inputFormat)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    m_EncodeBufferQueue.Initialize(m_stEncodeBuffer, m_uEncodeBufferCount);
    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        nvStatus = m_pNvHWEncoder->NvEncCreateInputBuffer(uInputWidth, uInputHeight, &m_stEncodeBuffer[i].stInputBfr.hInputSurface, inputFormat);
        if (nvStatus != NV_ENC_SUCCESS)
        {
            PRINTERR("Failed to allocate Input Buffer, Please reduce MAX_FRAMES_TO_PRELOAD\n");
            return nvStatus;
        }
        m_stEncodeBuffer[i].stInputBfr.bufferFmt = inputFormat;
        m_stEncodeBuffer[i].stInputBfr.dwWidth = uInputWidth;
        m_stEncodeBuffer[i].stInputBfr.dwHeight = uInputHeight;
        if (!m_stEncoderInput.enableMEOnly)
        {
            nvStatus = m_pNvHWEncoder->NvEncCreateBitstreamBuffer(BITSTREAM_BUFFER_SIZE, &m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
        }
        else
        {
            //Allocate output surface
            uint32_t encodeWidthInMbs = (uInputWidth + 15) >> 4;
            uint32_t encodeHeightInMbs = (uInputHeight + 15) >> 4;
            uint32_t dwSize = encodeWidthInMbs * encodeHeightInMbs * 64;
            nvStatus = m_pNvHWEncoder->NvEncCreateMVBuffer(dwSize, &m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
            if (nvStatus != NV_ENC_SUCCESS)
            {
                PRINTERR("nvEncCreateMVBuffer error:0x%x\n", nvStatus);
                return nvStatus;
            }
            m_stEncodeBuffer[i].stOutputBfr.dwBitstreamBufferSize = dwSize;
        }

        if (nvStatus != NV_ENC_SUCCESS)
        {
            PRINTERR("Failed to allocate Output Buffer, Please reduce MAX_FRAMES_TO_PRELOAD\n");
            return nvStatus;
        }
        m_stEncodeBuffer[i].stOutputBfr.dwBitstreamBufferSize = BITSTREAM_BUFFER_SIZE;

        if (m_stEncoderInput.enableAsyncMode)
        {   
            nvStatus = m_pNvHWEncoder->NvEncRegisterAsyncEvent(&m_stEncodeBuffer[i].stOutputBfr.hOutputEvent);
            if (nvStatus != NV_ENC_SUCCESS)
                return nvStatus;
            m_stEncodeBuffer[i].stOutputBfr.bWaitOnEvent = true;
        }
        else
            m_stEncodeBuffer[i].stOutputBfr.hOutputEvent = NULL;
    }

    m_stEOSOutputBfr.bEOSFlag = TRUE;

    if (m_stEncoderInput.enableAsyncMode)
    {   
        nvStatus = m_pNvHWEncoder->NvEncRegisterAsyncEvent(&m_stEOSOutputBfr.hOutputEvent);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus; 
    }   
    else
        m_stEOSOutputBfr.hOutputEvent = NULL;


    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderPerf::ReleaseIOBuffers()
{
    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        m_pNvHWEncoder->NvEncDestroyInputBuffer(m_stEncodeBuffer[i].stInputBfr.hInputSurface);
        m_stEncodeBuffer[i].stInputBfr.hInputSurface = NULL;
        if (!m_stEncoderInput.enableMEOnly)
        {
            m_pNvHWEncoder->NvEncDestroyBitstreamBuffer(m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
            m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer = NULL;
        }
        else
        {
            m_pNvHWEncoder->NvEncDestroyMVBuffer(m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
            m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer = NULL;
        }
        if (m_stEncoderInput.enableAsyncMode)
        {   
            m_pNvHWEncoder->NvEncUnregisterAsyncEvent(m_stEncodeBuffer[i].stOutputBfr.hOutputEvent);
            nvCloseFile(m_stEncodeBuffer[i].stOutputBfr.hOutputEvent);
            m_stEncodeBuffer[i].stOutputBfr.hOutputEvent = NULL;
        }   

    }

    if (m_stEOSOutputBfr.hOutputEvent)
    {
        if (m_stEncoderInput.enableAsyncMode)
        {
            m_pNvHWEncoder->NvEncUnregisterAsyncEvent(m_stEOSOutputBfr.hOutputEvent);
            nvCloseFile(m_stEOSOutputBfr.hOutputEvent);
            m_stEOSOutputBfr.hOutputEvent = NULL;
        }   
    }

    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderPerf::FlushEncoder()
{
    NVENCSTATUS nvStatus = m_pNvHWEncoder->NvEncFlushEncoderQueue(m_stEOSOutputBfr.hOutputEvent);
    if (nvStatus != NV_ENC_SUCCESS)
    {
        assert(0);
        return nvStatus;
    }

    EncodeBuffer *pEncodeBufer = m_EncodeBufferQueue.GetPending();
    while (pEncodeBufer)
    {
        m_pNvHWEncoder->ProcessOutput(pEncodeBufer);
        pEncodeBufer = m_EncodeBufferQueue.GetPending();
    }

#if defined(NV_WINDOWS)
    if (m_stEncoderInput.enableAsyncMode)
    {   
        if (WaitForSingleObject(m_stEOSOutputBfr.hOutputEvent, 500) != WAIT_OBJECT_0)
        {
            assert(0);
            nvStatus = NV_ENC_ERR_GENERIC;
        }
    }
#endif

    return nvStatus;
}

void CNvEncoderPerf::FlushMEOutput()
{
    NVENCSTATUS nvStatus;

    EncodeBuffer *pEncodeBufer = m_EncodeBufferQueue.GetPending();
    while (pEncodeBufer)
    {
        EncodeBuffer *temp = m_EncodeBufferQueue.GetPending();
        if (temp)
        {
            m_pNvHWEncoder->ProcessOutput(pEncodeBufer);
            pEncodeBufer = temp;
        }
        else
        {
            pEncodeBufer = NULL;
        }
    }
}

NVENCSTATUS CNvEncoderPerf::Deinitialize(uint32_t devicetype)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    ReleaseIOBuffers();

    nvStatus = m_pNvHWEncoder->NvEncDestroyEncoder();

    if (m_pDevice)
    {
        switch (devicetype)
        {
#if defined(NV_WINDOWS)
        case NV_ENC_DX9:
            ((IDirect3DDevice9*)(m_pDevice))->Release();
            break;

        case NV_ENC_DX10:
            ((ID3D10Device*)(m_pDevice))->Release();
            break;

        case NV_ENC_DX11:
            ((ID3D11Device*)(m_pDevice))->Release();
            break;
#endif

        case NV_ENC_CUDA:
            CUresult cuResult = CUDA_SUCCESS;
            cuResult = cuCtxDestroy((CUcontext)m_pDevice);
            if (cuResult != CUDA_SUCCESS)
                PRINTERR("cuCtxDestroy error:0x%x\n", cuResult);
        }

        m_pDevice = NULL;
    }

#if defined (NV_WINDOWS)
    if (m_pD3D)
    {
        m_pD3D->Release();
        m_pD3D = NULL;
    }
#endif

    return nvStatus;
}

NVENCSTATUS loadframe(uint8_t *yuvInput[3], HANDLE hInputYUVFile, uint32_t frmIdx, uint32_t width, uint32_t height, uint32_t &numBytesRead, NV_ENC_BUFFER_FORMAT inputFormat)
{
    uint64_t fileOffset;
    uint32_t result;
    //Set size depending on whether it is YUV 444 or YUV 420
    uint32_t dwInFrameSize = 0;
    int anFrameSize[3] = {};
    switch (inputFormat) {
    default:
    case NV_ENC_BUFFER_FORMAT_NV12: 
        dwInFrameSize = width * height * 3 / 2; 
        anFrameSize[0] = width * height;
        anFrameSize[1] = anFrameSize[2] = width * height / 4;
        break;
    case NV_ENC_BUFFER_FORMAT_YUV444:
        dwInFrameSize = width * height * 3;
        anFrameSize[0] = anFrameSize[1] = anFrameSize[2] = width * height;
        break;
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        dwInFrameSize = width * height * 3;
        anFrameSize[0] = width * height * 2;
        anFrameSize[1] = anFrameSize[2] = width * height / 2;
        break;
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        dwInFrameSize = width * height * 6;
        anFrameSize[0] = anFrameSize[1] = anFrameSize[2] = width * height * 2;
        break;
    }
    fileOffset = (uint64_t)dwInFrameSize * frmIdx;
    result = nvSetFilePointer64(hInputYUVFile, fileOffset, NULL, FILE_BEGIN);
    if (result == INVALID_SET_FILE_POINTER)
    {
        return NV_ENC_ERR_INVALID_PARAM;
    }
    nvReadFile(hInputYUVFile, yuvInput[0], anFrameSize[0], &numBytesRead, NULL);
    nvReadFile(hInputYUVFile, yuvInput[1], anFrameSize[1], &numBytesRead, NULL);
    nvReadFile(hInputYUVFile, yuvInput[2], anFrameSize[2], &numBytesRead, NULL);
    return NV_ENC_SUCCESS;
}

void PrintHelp()
{
    printf("Usage : NvEncoderPerf \n"
        "-i <string>                  Specify input yuv420 file\n"
        "-o <string>                  Specify output bitstream file\n"
        "-size <int int>              Specify input resolution <width height>\n"
        "\n### Optional parameters ###\n"
        "-codec <integer>             Specify the codec \n"
        "                                 0: H264\n"
        "                                 1: HEVC\n"
        "-preset <string>             Specify the preset for encoder settings\n"
        "                                 hq : nvenc HQ \n"
        "                                 hp : nvenc HP \n"
        "                                 lowLatencyHP : nvenc low latency HP \n"
        "                                 lowLatencyHQ : nvenc low latency HQ \n"
        "-startf <integer>            Specify start index for encoding. Default is 0\n"
        "-endf <integer>              Specify end index for encoding. Default is end of file\n"
        "-fps <integer>               Specify encoding frame rate\n"
        "-goplength <integer>         Specify gop length\n"
        "-numB <integer>              Specify number of B frames\n"
        "-bitrate <integer>           Specify the encoding average bitrate\n"
        "-vbvMaxBitrate <integer>     Specify the vbv max bitrate\n"
        "-vbvSize <integer>           Specify the encoding vbv/hrd buffer size\n"
        "-rcmode <integer>            Specify the rate control mode\n"
        "                                 0:  Constant QP mode\n"
        "                                 1:  Variable bitrate mode\n"
        "                                 2:  Constant bitrate mode\n"
        "                                 8:  low-delay CBR, high quality\n"
        "                                 16: CBR, high quality (slower)\n"
        "                                 32: VBR, high quality (slower)\n"
        "-qp <integer>                Specify qp for Constant QP mode\n"
        "-i_qfactor <float>           Specify qscale difference between I-frames and P-frames\n"
        "-b_qfactor <float>           Specify qscale difference between P-frames and B-frames\n" 
        "-i_qoffset <float>           Specify qscale offset between I-frames and P-frames\n"
        "-b_qoffset <float>           Specify qscale offset between P-frames and B-frames\n" 
        "-devicetype <integer>        Specify devicetype used for encoding\n"
        "                                 0:  DX9\n"
        "                                 1:  DX11\n"
        "                                 2:  Cuda\n"
        "                                 3:  DX10\n"
        "-deviceID <integer>          Specify the GPU device on which encoding will take place\n"
        "-inputFormat <integer>       Specify the input format\n"
        "                                 0: YUV 420\n"
        "                                 1: YUV 444\n"
        "                                 2: YUV 420 10-bit\n"
        "                                 3: YUV 444 10-bit\n"
        "-temporalAQ                      1: Enable TemporalAQ\n"
        "-meonly <integer>             Specify Motion estimation only(permissive value 1 and 2) to generates motion vectors and Mode information\n"
        "                                 1: Motion estimation between startf and endf\n"
        "                                 2: Motion estimation for all consecutive frames from startf to endf\n"
        "-help                        Prints Help Information\n\n"
        );
}

int CNvEncoderPerf::EncodeMain(int argc, char *argv[])
{
    HANDLE hInput;
    uint32_t numBytesRead = 0;
    uint8_t *yuv[3] = { 0 };
    unsigned long long lStart, lEnd, lFreq;
    int numFramesEncoded = 0;
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    bool bError = false;
    double elapsedTime = 0.0f;
    bool eof = false;
    EncodeConfig encodeConfig;
    uint32_t chromaFormatIDC = 0;
    int32_t lumaPlaneSize = 0, chromaPlaneSize = 0;

    memset(&encodeConfig, 0, sizeof(EncodeConfig));

    encodeConfig.endFrameIdx = INT_MAX;
    encodeConfig.bitrate = 5000000;
    encodeConfig.rcMode = NV_ENC_PARAMS_RC_CONSTQP;
    encodeConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
    encodeConfig.deviceType = NV_ENC_CUDA;
    encodeConfig.codec = NV_ENC_H264;
    encodeConfig.fps = 30;
    encodeConfig.qp = 28;
    encodeConfig.i_quant_factor = DEFAULT_I_QFACTOR;
    encodeConfig.b_quant_factor = DEFAULT_B_QFACTOR;  
    encodeConfig.i_quant_offset = DEFAULT_I_QOFFSET;
    encodeConfig.b_quant_offset = DEFAULT_B_QOFFSET; 
    encodeConfig.presetGUID = NV_ENC_PRESET_DEFAULT_GUID;
    encodeConfig.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    encodeConfig.inputFormat = NV_ENC_BUFFER_FORMAT_NV12;

    nvStatus = m_pNvHWEncoder->ParseArguments(&encodeConfig, argc, argv);
    if (nvStatus != NV_ENC_SUCCESS)
    {
        PrintHelp();
        return 1;
    }

    if (!encodeConfig.inputFileName || !encodeConfig.outputFileName || encodeConfig.width == 0 || encodeConfig.height == 0)
    {
        PrintHelp();
        return 1;
    }

    encodeConfig.fOutput = fopen(encodeConfig.outputFileName, "wb");
    if (encodeConfig.fOutput == NULL)
    {
        PRINTERR("Failed to create \"%s\"\n", encodeConfig.outputFileName);
        return 1;
    }

    hInput = nvOpenFile(encodeConfig.inputFileName);
    if (hInput == INVALID_HANDLE_VALUE)
    {
        PRINTERR("Failed to open \"%s\"\n", encodeConfig.inputFileName);
        return 1;
    }

    switch (encodeConfig.deviceType)
    {
#if defined(NV_WINDOWS)
    case NV_ENC_DX9:
        InitD3D9(encodeConfig.deviceID);
        break;

    case NV_ENC_DX10:
        InitD3D10(encodeConfig.deviceID);
        break;

    case NV_ENC_DX11:
        InitD3D11(encodeConfig.deviceID);
        break;
#endif

    case NV_ENC_CUDA:
        InitCuda(encodeConfig.deviceID);
        break;
    }

    if (encodeConfig.deviceType != NV_ENC_CUDA)
        nvStatus = m_pNvHWEncoder->Initialize(m_pDevice, NV_ENC_DEVICE_TYPE_DIRECTX);
    else
        nvStatus = m_pNvHWEncoder->Initialize(m_pDevice, NV_ENC_DEVICE_TYPE_CUDA);

    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    encodeConfig.presetGUID = m_pNvHWEncoder->GetPresetGUID(encodeConfig.encoderPreset, encodeConfig.codec);

    printf("Encoding input           : \"%s\"\n", encodeConfig.inputFileName);
    printf("         output          : \"%s\"\n", encodeConfig.outputFileName);
    printf("         codec           : \"%s\"\n", encodeConfig.codec == NV_ENC_HEVC ? "HEVC" : "H264");
    printf("         size            : %dx%d\n", encodeConfig.width, encodeConfig.height);
    printf("         bitrate         : %d bits/sec\n", encodeConfig.bitrate);
    printf("         vbvMaxBitrate   : %d bits/sec\n", encodeConfig.vbvMaxBitrate);
    printf("         vbvSize         : %d bits\n", encodeConfig.vbvSize);
    printf("         fps             : %d frames/sec\n", encodeConfig.fps);
    printf("         rcMode          : %s\n", encodeConfig.rcMode == NV_ENC_PARAMS_RC_CONSTQP ? "CONSTQP" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_VBR ? "VBR" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_CBR ? "CBR" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_VBR_MINQP ? "VBR MINQP (deprecated)" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ ? "CBR_LOWDELAY_HQ" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_CBR_HQ ? "CBR_HQ" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_VBR_HQ ? "VBR_HQ" : "UNKNOWN");
    if (encodeConfig.gopLength == NVENC_INFINITE_GOPLENGTH)
        printf("         goplength       : INFINITE GOP \n");
    else
        printf("         goplength       : %d \n", encodeConfig.gopLength);
    printf("         B frames        : %d \n", encodeConfig.numB);
    printf("         QP              : %d \n", encodeConfig.qp);
    printf("         preset          : %s\n", (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_HQ_GUID) ? "LOW_LATENCY_HQ" :
                                        (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_HP_GUID) ? "LOW_LATENCY_HP" :
                                        (encodeConfig.presetGUID == NV_ENC_PRESET_HQ_GUID) ? "HQ_PRESET" :
                                        (encodeConfig.presetGUID == NV_ENC_PRESET_HP_GUID) ? "HP_PRESET" :
                                        (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID) ? "LOW_LATENCY_DEFAULT" : "DEFAULT");
    printf("         devicetype      : %s\n", encodeConfig.deviceType == NV_ENC_DX9 ? "DX9" :
                                        encodeConfig.deviceType == NV_ENC_DX10 ? "DX10" :
                                        encodeConfig.deviceType == NV_ENC_DX11 ? "DX11" :
                                        encodeConfig.deviceType == NV_ENC_CUDA ? "CUDA" : "INVALID");

    printf("\n");

    nvStatus = m_pNvHWEncoder->CreateEncoder(&encodeConfig);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;
    m_stEncoderInput.enableMEOnly = encodeConfig.enableMEOnly;
    m_stEncoderInput.enableAsyncMode = encodeConfig.enableAsyncMode;
    //Adding extra frame to get N MEOnly output for n + 1 frames
    m_uEncodeBufferCount = MAX_FRAMES_TO_PRELOAD + 1;

    nvStatus = AllocateIOBuffers(encodeConfig.width, encodeConfig.height, encodeConfig.inputFormat);
    if (nvStatus != NV_ENC_SUCCESS)
    {
        bError = true;
        goto exit;
    }
    chromaFormatIDC = encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV444 || encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT ? 3 : 1;
    lumaPlaneSize = encodeConfig.width * encodeConfig.height * (encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV420_10BIT || encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT ? 2 : 1);
    chromaPlaneSize = (chromaFormatIDC == 3) ? lumaPlaneSize : (lumaPlaneSize >> 2);

    yuv[0] = new uint8_t[lumaPlaneSize];
    yuv[1] = new uint8_t[chromaPlaneSize];
    yuv[2] = new uint8_t[chromaPlaneSize];

    for (int frm = encodeConfig.startFrameIdx; frm <= encodeConfig.endFrameIdx; frm += MAX_FRAMES_TO_PRELOAD)
    {
        int numFramesLoaded = 0;
        int iterationCount = (encodeConfig.enableMEOnly ? (frm + MAX_FRAMES_TO_PRELOAD) : (frm + MAX_FRAMES_TO_PRELOAD - 1));
        for (int frmCnt = frm; frmCnt <= MIN(iterationCount, encodeConfig.endFrameIdx); frmCnt++)
        {
            numBytesRead = 0;
            loadframe(yuv, hInput, frmCnt, encodeConfig.width, encodeConfig.height, numBytesRead, encodeConfig.inputFormat);
            switch (encodeConfig.inputFormat) {
            default:
            case NV_ENC_BUFFER_FORMAT_NV12:
                ConvertYUVpitchToNV12(yuv[0], yuv[1], yuv[2], encodeConfig.width, encodeConfig.height, (frmCnt - frm));
                break;
            case NV_ENC_BUFFER_FORMAT_YUV444:
                ConvertYUVpitchToYUV444(yuv[0], yuv[1], yuv[2], encodeConfig.width, encodeConfig.height, (frmCnt - frm));
                break;
            case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
                ConvertYUV10pitchtoP010PL((unsigned short *)yuv[0], (unsigned short *)yuv[1], (unsigned short *)yuv[2], encodeConfig.width, encodeConfig.height, (frmCnt - frm));
                break;
            case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
                ConvertYUV10pitchtoYUV444((unsigned short *)yuv[0], (unsigned short *)yuv[1], (unsigned short *)yuv[2], encodeConfig.width, encodeConfig.height, (frmCnt - frm));
                break;
            }

            if (numBytesRead == 0)
            {
                eof = true;
                break;
            }
            numFramesLoaded++;
        }

        if (!encodeConfig.enableMEOnly)
        {
            NvQueryPerformanceCounter(&lStart);
            for (int frmCnt = 0; frmCnt < numFramesLoaded; frmCnt++)
            {
                EncodeFrame(false, encodeConfig.width, encodeConfig.height);
                numFramesEncoded++;
            }
            nvStatus = EncodeFrame(true, encodeConfig.width, encodeConfig.height);
            if (nvStatus != NV_ENC_SUCCESS)
            {
                bError = true;
                goto exit;
            }
            NvQueryPerformanceCounter(&lEnd);
            elapsedTime += (double)(lEnd - lStart);
        }
        else
        {
            NvQueryPerformanceCounter(&lStart);
            for (int frmCnt = frm; frmCnt < MIN((frm + MAX_FRAMES_TO_PRELOAD), encodeConfig.endFrameIdx); frmCnt++)
            {
                RunMotionEstimationOnly(false, encodeConfig.width, encodeConfig.height, frmCnt, frmCnt+1);
                numFramesEncoded++;
            }
            nvStatus = RunMotionEstimationOnly(true, encodeConfig.width, encodeConfig.height,0,0);
            if (nvStatus != NV_ENC_SUCCESS)
            {
                bError = true;
                goto exit;
            }
            NvQueryPerformanceCounter(&lEnd);
            elapsedTime += (double)(lEnd - lStart);
        }

        if (eof == true)
        {
            break;
        }
    }
    if (numFramesEncoded > 0)
    {
        NvQueryPerformanceFrequency(&lFreq);
        printf("Encoded %d frames in %6.2fms\n", numFramesEncoded, (elapsedTime*1000.0) / lFreq);
        printf("Average Encode Time : %6.2fms\n", ((elapsedTime*1000.0) / numFramesEncoded) / lFreq);
        printf("Frames per second: %dfps\n", (int)((float)numFramesEncoded * 1000.0 /(float)((elapsedTime*1000.0) / lFreq)));
    }

exit:
    if (encodeConfig.fOutput)
    {
        fclose(encodeConfig.fOutput);
    }

    if (hInput)
    {
        nvCloseFile(hInput);
    }

    Deinitialize(encodeConfig.deviceType);

    for (int i = 0; i < 3; i ++)
    {
        if (yuv[i])
        {
            delete [] yuv[i];
        }
    }

    return bError ? 1 : 0;
}

NVENCSTATUS CNvEncoderPerf::RunMotionEstimationOnly(bool bFlush, uint32_t width, uint32_t height, uint32_t inputFrameIndex, uint32_t refFrameIndex)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    NV_ENC_PIC_PARAMS encPicParams;
    static EncodeBuffer *pEncodeBuffer[2] = { NULL };
    memset(&encPicParams, 0, sizeof(encPicParams));
    SET_VER(encPicParams, NV_ENC_PIC_PARAMS);

    if (bFlush)
    {
        FlushMEOutput();
        return NV_ENC_SUCCESS;
    }

    if (pEncodeBuffer[0] == NULL)
    {
        pEncodeBuffer[0] = m_EncodeBufferQueue.GetAvailable();
        if (!pEncodeBuffer[0])
        {
            m_pNvHWEncoder->ProcessOutput(m_EncodeBufferQueue.GetPending());
            pEncodeBuffer[0] = m_EncodeBufferQueue.GetAvailable();
        }
    }
    pEncodeBuffer[1] = m_EncodeBufferQueue.GetAvailable();
    if (!pEncodeBuffer[1])
    {
        m_pNvHWEncoder->ProcessOutput(m_EncodeBufferQueue.GetPending());
        pEncodeBuffer[1] = m_EncodeBufferQueue.GetAvailable();
    }
    MotionEstimationBuffer meBuffer;
    meBuffer.inputFrameIndex = inputFrameIndex;
    meBuffer.referenceFrameIndex = refFrameIndex;
    meBuffer.stInputBfr[0] = pEncodeBuffer[0]->stInputBfr;
    meBuffer.stInputBfr[1] = pEncodeBuffer[1]->stInputBfr;;
    meBuffer.stOutputBfr   = pEncodeBuffer[0]->stOutputBfr;
    nvStatus = m_pNvHWEncoder->NvRunMotionEstimationOnly(&meBuffer, NULL);
    pEncodeBuffer[0] = pEncodeBuffer[1];
    pEncodeBuffer[1] = NULL;
    return nvStatus;
}


NVENCSTATUS CNvEncoderPerf::EncodeFrame(bool bFlush, uint32_t width, uint32_t height)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    EncodeBuffer *pEncodeBuffer = NULL;
    NV_ENC_PIC_PARAMS encPicParams;

    memset(&encPicParams, 0, sizeof(encPicParams));
    SET_VER(encPicParams, NV_ENC_PIC_PARAMS);

    if (bFlush)
    {
        FlushEncoder();
        return NV_ENC_SUCCESS;
    }

    pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
    if(!pEncodeBuffer)
    {
        m_pNvHWEncoder->ProcessOutput(m_EncodeBufferQueue.GetPending());
        pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
    }

    nvStatus = m_pNvHWEncoder->NvEncEncodeFrame(pEncodeBuffer, NULL, width, height);
    return nvStatus;
}

int main(int argc, char **argv)
{
    CNvEncoderPerf nvEncoder;
    return nvEncoder.EncodeMain(argc, argv);
}
