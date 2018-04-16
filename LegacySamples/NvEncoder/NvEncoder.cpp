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

#include "../common/inc/nvCPUOPSys.h"
#include "../common/inc/nvEncodeAPI.h"
#include "../common/inc/nvUtils.h"
#include "NvEncoder.h"
#include "../common/inc/nvFileIO.h"
#include <new>

#define BITSTREAM_BUFFER_SIZE 2 * 1024 * 1024

void convertYUVpitchtoNV12( unsigned char *yuv_luma, unsigned char *yuv_cb, unsigned char *yuv_cr,
                            unsigned char *nv12_luma, unsigned char *nv12_chroma,
                            int width, int height , int srcStride, int dstStride)
{
    int y;
    int x;
    if (srcStride == 0)
        srcStride = width;
    if (dstStride == 0)
        dstStride = width;

    for ( y = 0 ; y < height ; y++)
    {
        memcpy( nv12_luma + (dstStride*y), yuv_luma + (srcStride*y) , width );
    }

    for ( y = 0 ; y < height/2 ; y++)
    {
        for ( x= 0 ; x < width; x=x+2)
        {
            nv12_chroma[(y*dstStride) + x] =    yuv_cb[((srcStride/2)*y) + (x >>1)];
            nv12_chroma[(y*dstStride) +(x+1)] = yuv_cr[((srcStride/2)*y) + (x >>1)];
        }
    }
}

void convertYUV10pitchtoP010PL(unsigned short *yuv_luma, unsigned short *yuv_cb, unsigned short *yuv_cr,
    unsigned short *nv12_luma, unsigned short *nv12_chroma, int width, int height, int srcStride, int dstStride)
{
    int x, y;

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            nv12_luma[(y*dstStride / 2) + x] = yuv_luma[(srcStride*y) + x] << 6;
        }
    }

    for (y = 0; y < height / 2; y++)
    {
        for (x = 0; x < width; x = x + 2)
        {
            nv12_chroma[(y*dstStride / 2) + x] = yuv_cb[((srcStride / 2)*y) + (x >> 1)] << 6;
            nv12_chroma[(y*dstStride / 2) + (x + 1)] = yuv_cr[((srcStride / 2)*y) + (x >> 1)] << 6;
        }
    }
}

void convertYUVpitchtoYUV444(unsigned char *yuv_luma, unsigned char *yuv_cb, unsigned char *yuv_cr,
    unsigned char *surf_luma, unsigned char *surf_cb, unsigned char *surf_cr, int width, int height, int srcStride, int dstStride)
{
    int h;

    for (h = 0; h < height; h++)
    {
        memcpy(surf_luma + dstStride * h, yuv_luma + srcStride * h, width);
        memcpy(surf_cb + dstStride * h, yuv_cb + srcStride * h, width);
        memcpy(surf_cr + dstStride * h, yuv_cr + srcStride * h, width);
    }
}

void convertYUV10pitchtoYUV444(unsigned short *yuv_luma, unsigned short *yuv_cb, unsigned short *yuv_cr,
    unsigned short *surf_luma, unsigned short *surf_cb, unsigned short *surf_cr,
    int width, int height, int srcStride, int dstStride)
{
    int x, y;

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            surf_luma[(y*dstStride / 2) + x] = yuv_luma[(srcStride*y) + x] << 6;
            surf_cb[(y*dstStride / 2) + x] = yuv_cb[(srcStride*y) + x] << 6;
            surf_cr[(y*dstStride / 2) + x] = yuv_cr[(srcStride*y) + x] << 6;
        }
    }
}

CNvEncoder::CNvEncoder()
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
    memset(&m_stMVBuffer, 0, sizeof(m_stMVBuffer));
    memset(&m_stEncodeBuffer, 0, sizeof(m_stEncodeBuffer));
}

CNvEncoder::~CNvEncoder()
{
    if (m_pNvHWEncoder)
    {
        delete m_pNvHWEncoder;
        m_pNvHWEncoder = NULL;
    }
}

NVENCSTATUS CNvEncoder::InitCuda(uint32_t deviceID)
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
NVENCSTATUS CNvEncoder::InitD3D9(uint32_t deviceID)
{
    D3DPRESENT_PARAMETERS d3dpp;
    D3DADAPTER_IDENTIFIER9 adapterId;
    unsigned int iAdapter = NULL; // Our adapter
    HRESULT hr = S_OK;

    m_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (m_pD3D == NULL)
    {
        assert(m_pD3D);
        return NV_ENC_ERR_OUT_OF_MEMORY;;
    }

    if (deviceID >= m_pD3D->GetAdapterCount())
    {
        PRINTERR("Invalid Device Id = %d\n. Please use DX10/DX11 to detect headless video devices.\n", deviceID);
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

NVENCSTATUS CNvEncoder::InitD3D10(uint32_t deviceID)
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
            PRINTERR("Problem while creating %d D3d10 device \n", deviceID);
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

NVENCSTATUS CNvEncoder::InitD3D11(uint32_t deviceID)
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
            PRINTERR("Problem while creating %d D3d11 device \n", deviceID);
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
#endif

NVENCSTATUS CNvEncoder::AllocateIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight, NV_ENC_BUFFER_FORMAT inputFormat)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    m_EncodeBufferQueue.Initialize(m_stEncodeBuffer, m_uEncodeBufferCount);
    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        nvStatus = m_pNvHWEncoder->NvEncCreateInputBuffer(uInputWidth, uInputHeight, &m_stEncodeBuffer[i].stInputBfr.hInputSurface, inputFormat);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;

        m_stEncodeBuffer[i].stInputBfr.bufferFmt = inputFormat;
        m_stEncodeBuffer[i].stInputBfr.dwWidth = uInputWidth;
        m_stEncodeBuffer[i].stInputBfr.dwHeight = uInputHeight;
        nvStatus = m_pNvHWEncoder->NvEncCreateBitstreamBuffer(BITSTREAM_BUFFER_SIZE, &m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;
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

NVENCSTATUS CNvEncoder::AllocateMVIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight, NV_ENC_BUFFER_FORMAT inputFormat)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    m_MVBufferQueue.Initialize(m_stMVBuffer, m_uEncodeBufferCount);
    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        // Allocate Input, Reference surface
        for (uint32_t j = 0; j < 2; j++)
        {
            nvStatus = m_pNvHWEncoder->NvEncCreateInputBuffer(uInputWidth, uInputHeight, &m_stMVBuffer[i].stInputBfr[j].hInputSurface, inputFormat);
            if (nvStatus != NV_ENC_SUCCESS)
                return nvStatus;
            m_stMVBuffer[i].stInputBfr[j].bufferFmt = inputFormat;
            m_stMVBuffer[i].stInputBfr[j].dwWidth = uInputWidth;
            m_stMVBuffer[i].stInputBfr[j].dwHeight = uInputHeight;
        }
        //Allocate output surface
        uint32_t encodeWidthInMbs = (uInputWidth + 15) >> 4;
        uint32_t encodeHeightInMbs = (uInputHeight + 15) >> 4;
        uint32_t dwSize = encodeWidthInMbs * encodeHeightInMbs * 64;
        nvStatus = m_pNvHWEncoder->NvEncCreateMVBuffer(dwSize, &m_stMVBuffer[i].stOutputBfr.hBitstreamBuffer);
        if (nvStatus != NV_ENC_SUCCESS)
        {
            PRINTERR("nvEncCreateMVBuffer error:0x%x\n", nvStatus);
            return nvStatus;
        }
        m_stMVBuffer[i].stOutputBfr.dwBitstreamBufferSize = dwSize;
        if (m_stEncoderInput.enableAsyncMode)
        {
            nvStatus = m_pNvHWEncoder->NvEncRegisterAsyncEvent(&m_stMVBuffer[i].stOutputBfr.hOutputEvent);
            if (nvStatus != NV_ENC_SUCCESS)
                return nvStatus;
            m_stMVBuffer[i].stOutputBfr.bWaitOnEvent = true;
        }
        else
            m_stMVBuffer[i].stOutputBfr.hOutputEvent = NULL;
    }
    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoder::ReleaseIOBuffers()
{
    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        m_pNvHWEncoder->NvEncDestroyInputBuffer(m_stEncodeBuffer[i].stInputBfr.hInputSurface);
        m_stEncodeBuffer[i].stInputBfr.hInputSurface = NULL;
        m_pNvHWEncoder->NvEncDestroyBitstreamBuffer(m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
        m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer = NULL;
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

NVENCSTATUS CNvEncoder::ReleaseMVIOBuffers()
{
    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        for (uint32_t j = 0; j < 2; j++)
        {
            m_pNvHWEncoder->NvEncDestroyInputBuffer(m_stMVBuffer[i].stInputBfr[j].hInputSurface);
            m_stMVBuffer[i].stInputBfr[j].hInputSurface = NULL;
        }
        m_pNvHWEncoder->NvEncDestroyMVBuffer(m_stMVBuffer[i].stOutputBfr.hBitstreamBuffer);
        m_stMVBuffer[i].stOutputBfr.hBitstreamBuffer = NULL;
        if (m_stEncoderInput.enableAsyncMode)
        {
            m_pNvHWEncoder->NvEncUnregisterAsyncEvent(m_stMVBuffer[i].stOutputBfr.hOutputEvent);
            nvCloseFile(m_stMVBuffer[i].stOutputBfr.hOutputEvent);
            m_stMVBuffer[i].stOutputBfr.hOutputEvent = NULL;
        }
    }

    return NV_ENC_SUCCESS;
}

void CNvEncoder::FlushMVOutputBuffer()
{
    MotionEstimationBuffer *pMEBufer = m_MVBufferQueue.GetPending();

    while (pMEBufer)
    {
            m_pNvHWEncoder->ProcessMVOutput(pMEBufer);
            pMEBufer = m_MVBufferQueue.GetPending();
    }
}

NVENCSTATUS CNvEncoder::FlushEncoder()
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

NVENCSTATUS CNvEncoder::Deinitialize(uint32_t devicetype)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    if (m_stEncoderInput.enableMEOnly)
    {
        ReleaseMVIOBuffers();
    }
    else
    {
        ReleaseIOBuffers();
    }

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

NVENCSTATUS MeonlyOutPutToCEABufferPacker(FILE *fpExternalHint, uint32_t width, uint32_t height,
                                          uint32_t frameIndex, NVENC_EXTERNAL_ME_HINT **ceaBuffer,
                                          NVENC_EXTERNAL_ME_HINT_COUNTS_PER_BLOCKTYPE *meHintCountsPerBlock)
{
    char temp[256] = { 0 };
    unsigned int nMBWidth = (width + 15) >> 4;
    unsigned int nMBHeight = (height + 15) >> 4;
    NV_ENC_H264_MV_DATA buffer = {0};
    int block_num = 0;
    int totalhint = 0;
    int blocknumcnt = 0;
    int numOfHintsPerMB = 0;
    int numOfMBs = 0;
    uint32_t ceaBufferSize = 0;
    uint32_t *ceaBufferPtrCopy = NULL;
    uint32_t ceaBufferIndex = 0;
    static uint32_t inputFrameIdx = -1;
    static uint32_t referenceFrameIdx = -1;

    if (inputFrameIdx == -1 && !feof(fpExternalHint))
    {
        fscanf(fpExternalHint, "Motion Vectors for input frame = %d, reference frame = %d\n", &inputFrameIdx, &referenceFrameIdx);
        fscanf(fpExternalHint, "block, mb_type, partitionType, MV[0].x, MV[0].y, MV[1].x, MV[1].y, MV[2].x, MV[2].y, MV[3].x, MV[3].y, cost\n");
    }
    if (inputFrameIdx == frameIndex)
    {
        // PartitionOrder supportered in CEA Buffer
        uint16_t partitionOrderInCeaBuffer[4] = { PARTITION_TYPE_16x16, PARTITION_TYPE_16x8,
                                                PARTITION_TYPE_8x16, PARTITION_TYPE_8x8 };
        // Number of hints in PartitionOrder define in partitionOrderInCeaBuffer array
        uint16_t numOfPartitionHints[4] = { NUM_OF_MVHINTS_PER_BLOCK16x16, NUM_OF_MVHINTS_PER_BLOCK16x8,
            NUM_OF_MVHINTS_PER_BLOCK8x16, NUM_OF_MVHINTS_PER_BLOCK8x8 };
        numOfHintsPerMB = NUM_OF_MVHINTS_PER_BLOCK16x16 * meHintCountsPerBlock->numCandsPerBlk16x16 +
            NUM_OF_MVHINTS_PER_BLOCK16x8  * meHintCountsPerBlock->numCandsPerBlk16x8 +
            NUM_OF_MVHINTS_PER_BLOCK8x16  * meHintCountsPerBlock->numCandsPerBlk8x16 +
            NUM_OF_MVHINTS_PER_BLOCK8x8 * meHintCountsPerBlock->numCandsPerBlk8x8;
        memset(*ceaBuffer, 0, ceaBufferSize);
        ceaBufferPtrCopy = (uint32_t *)(*ceaBuffer);

        for (uint32_t i = 0; i < nMBWidth; i++)
        {
            for (uint32_t j = 0; j < nMBHeight; j++)
            {
                uint32_t mbType = 0;
                uint32_t partitionType = 0;
                // last of MB is set for 0 index in partitionOrderInCeaBuffer(16x16) as per struture NVENC_EXTERNAL_ME_HINT
                uint32_t lastOfMBMask = 0x40000000;
                // last of Partition is set for 0 index in partitionOrderInCeaBuffer(16x16) as per struture NVENC_EXTERNAL_ME_HINT
                uint32_t lastOfPartitionMask = 0xC0000000;
                // Point to first index of parition order define in partitionOrderInCeaBuffer
                int      hintsIterator = 0;
                int      mvHintCount = 0;
                int      hintCount = numOfPartitionHints[hintsIterator];

                fscanf(fpExternalHint, "%u, %u, %u, %hd, %hd, %hd, %hd, %hd, %hd, %hd, %hd, %us\n", &block_num,
                    &mbType, &partitionType, &buffer.mv[0].mvx, &buffer.mv[0].mvy, &buffer.mv[1].mvx,
                    &buffer.mv[1].mvy, &buffer.mv[2].mvx, &buffer.mv[2].mvy, &buffer.mv[3].mvx, &buffer.mv[3].mvy, &buffer.mbCost);
                buffer.mbType = mbType;
                buffer.partitionType = partitionType;
                buffer.mv[0].mvx /= 4;    buffer.mv[0].mvy /= 4;
                buffer.mv[1].mvx /= 4;    buffer.mv[1].mvy /= 4;
                buffer.mv[2].mvx /= 4;    buffer.mv[2].mvy /= 4;
                buffer.mv[3].mvx /= 4;    buffer.mv[3].mvy /= 4;

                for (int k = 0, mvIdx = 0; k < numOfHintsPerMB; k++)
                {
                    // Move to next index in partitionOrderInCeaBuffer array when user has processed all hints for previous indexes
                    // and update lastOfMBMask and lastOfPartitionMask for the latest partitionType. 
                    if (hintCount == k)
                    {
                        lastOfMBMask += 0x10000000;
                        lastOfPartitionMask += 0x10000000;
                        ++hintsIterator;
                        hintCount += numOfPartitionHints[hintsIterator];
                    }
                    uint32_t bitmask = (k == (numOfHintsPerMB - 1)) ? lastOfPartitionMask : lastOfMBMask;
                    if (partitionOrderInCeaBuffer[hintsIterator] == partitionType && partitionType != PARTITION_TYPE_16x8)
                    {
                        ceaBufferPtrCopy[ceaBufferIndex++] = bitmask | ((buffer.mv[mvIdx].mvy & 0x3ff) << 12) | (buffer.mv[mvIdx].mvx & 0xfff);
                        mvIdx++;
                    }
                    else if (partitionOrderInCeaBuffer[hintsIterator] == partitionType && partitionType == PARTITION_TYPE_16x8)
                    {
                        ceaBufferPtrCopy[ceaBufferIndex++] = bitmask | ((buffer.mv[0].mvy & 0x3ff) << 12) | (buffer.mv[0].mvx & 0xfff);
                        k++;
                        ceaBufferPtrCopy[ceaBufferIndex++] = ((k == (numOfHintsPerMB - 1)) ? lastOfPartitionMask : lastOfMBMask) | ((buffer.mv[2].mvy & 0x3ff) << 12) | (buffer.mv[2].mvx & 0xfff);
                    }
                    else
                        ceaBufferPtrCopy[ceaBufferIndex++] = bitmask;
                }
            }
        }
        fscanf(fpExternalHint, "\n");
        inputFrameIdx = -1;
        return NV_ENC_SUCCESS;
    }

    return NV_ENC_ERR_INVALID_PARAM;
}

void PrintHelp()
{
    printf("Usage : NvEncoder \n"
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
        "                                 lossless : nvenc Lossless HP \n"
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
        "-picStruct <integer>         Specify the picture structure\n"
        "                                 1:  Progressive frame\n"
        "                                 2:  Field encoding top field first\n"
        "                                 3:  Field encoding bottom field first\n"
        "-devicetype <integer>        Specify devicetype used for encoding\n"
        "                                 0:  DX9\n"
        "                                 1:  DX11\n"
        "                                 2:  Cuda\n"
        "                                 3:  DX10\n"
        "-inputFormat <integer>       Specify the input format\n"
        "                                 0: YUV 420\n"
        "                                 1: YUV 444\n"
        "                                 2: YUV 420 10-bit\n"
        "                                 3: YUV 444 10-bit\n"
        "-deviceID <integer>           Specify the GPU device on which encoding will take place\n"
        "-meonly <integer>             Specify Motion estimation only(permissive value 1 and 2) to generates motion vectors and Mode information\n"
        "                                 1: Motion estimation between startf and endf\n"
        "                                 2: Motion estimation for all consecutive frames from startf to endf\n"
        "-preloadedFrameCount <integer> Specify number of frame to load in memory(default value=240) with min value 2(1 frame for ref, 1 frame for input)\n"
        "-temporalAQ                      1: Enable TemporalAQ\n"
        "-generateQpDeltaMap <string>   Demonstrate QP delta map, and use opposite delta values for 1,3 quadrants and 2,4 quadrants for each frame. "
        "                              Also, save the delta map array in the specified file.\n"
        "-enableExternalMEHint <bool>    Specify external hint support\n"
        "                                 1: Enable external hint support along with spatial and temporal hints\n"
        "-externalHintInputFile <string> Specify hint file which is in H264 meonly output format.\n"
        "                                  The total number of hints per MB per direction =\n"
        "                                    1*meHintCountsPerBlock[Lx].numCandsPerBlk16x16 +\n"
        "                                    2*meHintCountsPerBlock[Lx].numCandsPerBlk16x8 +\n"
        "                                    2*meHintCountsPerBlock[Lx].numCandsPerBlk8x8\n"
        "                                  The sample application demostrates 9 hints per MB considering numCandsPerBlkNxN=1 for all partitionmodes\n"
        "                                  enabled for L0 predictor. The application sets mv=0 for partitionType which are not supported for a MB.\n"
        "                                  The sample application is enabled with external hint support for H264 encoding only.\n"
        "-help                          Prints Help Information\n\n"
        );
}

int CNvEncoder::EncodeMain(int argc, char *argv[])
{
    HANDLE hInput;
    DWORD fileSize;
    uint32_t numBytesRead = 0;
    uint8_t *yuv[3];
    int lumaPlaneSize, chromaPlaneSize;
    unsigned long long lStart, lEnd, lFreq;
    int numFramesEncoded = 0;
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    bool bError = false;
    EncodeConfig encodeConfig;
    unsigned int preloadedFrameCount = FRAME_QUEUE;
    uint32_t  chromaFormatIDC = 0;
    NVENC_EXTERNAL_ME_HINT *ceaBuffer = NULL; 
    unsigned int numOfFramesWithHints = 0;
    FILE *fpExternalHint = NULL;

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
    
    chromaFormatIDC = (encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV444 || encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT) ? 3 : 1;
    if (((encodeConfig.width & 1) || (encodeConfig.height & 1)) && chromaFormatIDC == 1)
    {
        PRINTERR("nvEncoder.exe Error: Odd dimentions are not supported \n");
        return 1;
    }
    if ((encodeConfig.enableMEOnly == 1) || (encodeConfig.enableMEOnly == 2))
    {

        if ((encodeConfig.codec != NV_ENC_H264) && (encodeConfig.codec != NV_ENC_HEVC))
        {
            PRINTERR("\nvEncoder.exe Error: MEOnly mode is now only supported for H264 and HEVC. Check input params!\n");
            return 1;
        }
        memcpy(&m_stEncoderInput, &encodeConfig, sizeof(encodeConfig));
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
    printf("       Input Format      : %s\n", 
        encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_NV12 ? "YUV 420" : 
        (encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV444 ? "YUV 444" : 
        (encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV420_10BIT ? "YUV 420 10-bit" : "YUV 444 10-bit")));
    printf("         preset          : %s\n", (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_HQ_GUID) ? "LOW_LATENCY_HQ" :
                                        (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_HP_GUID) ? "LOW_LATENCY_HP" :
                                        (encodeConfig.presetGUID == NV_ENC_PRESET_HQ_GUID) ? "HQ_PRESET" :
                                        (encodeConfig.presetGUID == NV_ENC_PRESET_HP_GUID) ? "HP_PRESET" :
                                        (encodeConfig.presetGUID == NV_ENC_PRESET_LOSSLESS_HP_GUID) ? "LOSSLESS_HP" :
                                        (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID) ? "LOW_LATENCY_DEFAULT" : "DEFAULT");
    printf("  Picture Structure      : %s\n", (encodeConfig.pictureStruct == NV_ENC_PIC_STRUCT_FRAME) ? "Frame Mode" :
                                              (encodeConfig.pictureStruct == NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM) ? "Top Field first" :
                                              (encodeConfig.pictureStruct == NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP) ? "Bottom Field first" : "INVALID");
    printf("         devicetype      : %s\n",   encodeConfig.deviceType == NV_ENC_DX9 ? "DX9" :
                                                encodeConfig.deviceType == NV_ENC_DX10 ? "DX10" :
                                                encodeConfig.deviceType == NV_ENC_DX11 ? "DX11" :
                                                encodeConfig.deviceType == NV_ENC_CUDA ? "CUDA" : "INVALID");

    printf("\n");

    nvStatus = m_pNvHWEncoder->CreateEncoder(&encodeConfig);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;
    encodeConfig.maxWidth = encodeConfig.maxWidth ? encodeConfig.maxWidth : encodeConfig.width;
    encodeConfig.maxHeight = encodeConfig.maxHeight ? encodeConfig.maxHeight : encodeConfig.height;

    m_stEncoderInput.enableAsyncMode = encodeConfig.enableAsyncMode;

    if (encodeConfig.enableExternalMEHint && (m_stEncoderInput.enableMEOnly || 
        encodeConfig.codec != NV_ENC_H264 || encodeConfig.numB > 0))
    {
        printf("Application supports external hint only for H264 encoding for P frame \n");
        return 1;
    }

    if (encodeConfig.numB > 0)
    {
        m_uEncodeBufferCount = encodeConfig.numB + 4; // min buffers is numb + 1 + 3 pipelining
    }
    else
    {
        int numMBs = ((encodeConfig.maxHeight + 15) >> 4) * ((encodeConfig.maxWidth + 15) >> 4);
        int NumIOBuffers;
        if (numMBs >= 32768) //4kx2k
            NumIOBuffers = MAX_ENCODE_QUEUE / 8;
        else if (numMBs >= 16384) // 2kx2k
            NumIOBuffers = MAX_ENCODE_QUEUE / 4;
        else if (numMBs >= 8160) // 1920x1080
            NumIOBuffers = MAX_ENCODE_QUEUE / 2;
        else
            NumIOBuffers = MAX_ENCODE_QUEUE;
        m_uEncodeBufferCount = NumIOBuffers;
    }
    m_uPicStruct = encodeConfig.pictureStruct;
    if (m_stEncoderInput.enableMEOnly)
    {
        // Struct MotionEstimationBuffer has capacity to store two inputBuffer in single object.
        m_uEncodeBufferCount = m_uEncodeBufferCount/2;
        nvStatus = AllocateMVIOBuffers(encodeConfig.width, encodeConfig.height, encodeConfig.inputFormat);
    }
    else
    {
        nvStatus = AllocateIOBuffers(encodeConfig.width, encodeConfig.height, encodeConfig.inputFormat);
    }
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    if (encodeConfig.preloadedFrameCount >= 2)
    {
        preloadedFrameCount = encodeConfig.preloadedFrameCount;
    }

    lumaPlaneSize = encodeConfig.maxWidth * encodeConfig.maxHeight * (encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV420_10BIT || encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT ? 2 : 1);
    chromaPlaneSize = (chromaFormatIDC == 3) ? lumaPlaneSize : (lumaPlaneSize >> 2);
    nvGetFileSize(hInput, &fileSize);
    int totalFrames = fileSize / (lumaPlaneSize + chromaPlaneSize + chromaPlaneSize);
    if (encodeConfig.endFrameIdx < 0) {  
        encodeConfig.endFrameIdx = totalFrames - 1;
    }
    else if (encodeConfig.endFrameIdx > totalFrames) {
        PRINTERR("nvEncoder.exe Warning: -endf %d exceeds total video frame %d, using %d instead\n", encodeConfig.endFrameIdx, totalFrames, totalFrames);
        encodeConfig.endFrameIdx = totalFrames - 1;
    }

    if ((encodeConfig.enableMEOnly == 1) || (encodeConfig.enableMEOnly == 2))
    {        
        int nBytesPerSample = encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV420_10BIT || encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT ? 2 : 1;
        MEOnlyConfig stMEOnly;
        memset(&stMEOnly, 0, sizeof(stMEOnly));
        stMEOnly.width = encodeConfig.width;
        stMEOnly.height = encodeConfig.height;
        stMEOnly.stride[0] = encodeConfig.width * nBytesPerSample;
        stMEOnly.stride[1] = ((chromaFormatIDC == 3) ? encodeConfig.width : encodeConfig.width >> 1) * nBytesPerSample;
        stMEOnly.stride[2] = ((chromaFormatIDC == 3) ? encodeConfig.width : encodeConfig.width >> 1) * nBytesPerSample;

        if (encodeConfig.enableMEOnly == 1)
        {
            stMEOnly.referenceFrameIndex = encodeConfig.startFrameIdx;
            stMEOnly.inputFrameIndex = encodeConfig.endFrameIdx;
            for (unsigned int i = 0; i < 2; i++)
            {
                stMEOnly.yuv[i][0] = new(std::nothrow)  unsigned char[lumaPlaneSize];
                stMEOnly.yuv[i][1] = new(std::nothrow)  unsigned char[chromaPlaneSize];
                stMEOnly.yuv[i][2] = new(std::nothrow)  unsigned char[chromaPlaneSize];
                if (stMEOnly.yuv[i][0] == NULL || stMEOnly.yuv[i][1] == NULL || stMEOnly.yuv[i][2] == NULL)
                {
                    PRINTERR("\nvEncoder.exe Error: Failed to allocate memory for array yuvLoaded of Size = %u !\n", (lumaPlaneSize + 2 * chromaPlaneSize));
                    return 1;
                }
            }
            numBytesRead = 0;
            loadframe(stMEOnly.yuv[0], hInput, encodeConfig.startFrameIdx, encodeConfig.width, encodeConfig.height, numBytesRead, encodeConfig.inputFormat);
            loadframe(stMEOnly.yuv[1], hInput, encodeConfig.endFrameIdx, encodeConfig.width, encodeConfig.height, numBytesRead, encodeConfig.inputFormat);
            RunMotionEstimationOnly(&stMEOnly, false);
        }
        else
        {
            unsigned char *yuvLoaded[3] = { NULL, NULL, NULL };
            unsigned char *yuvScaled[3] = { NULL, NULL, NULL };
            unsigned char *yuvInput[3] = { NULL, NULL, NULL };
            unsigned int numFramesToEncode = MAX(1, (encodeConfig.endFrameIdx - encodeConfig.startFrameIdx));
            yuvLoaded[0] = new(std::nothrow)  unsigned char[preloadedFrameCount * lumaPlaneSize];
            yuvLoaded[1] = new(std::nothrow)  unsigned char[preloadedFrameCount * chromaPlaneSize];
            yuvLoaded[2] = new(std::nothrow)  unsigned char[preloadedFrameCount * chromaPlaneSize];
            
            if (yuvLoaded[0] == NULL || yuvLoaded[1] == NULL || yuvLoaded[2] == NULL)
            {
                PRINTERR("\nvEncoder.exe Error: Failed to allocate memory for array yuvLoaded of Size = %u !\n", preloadedFrameCount*(lumaPlaneSize + 2 * chromaPlaneSize));
                return 1;
            }

            for (unsigned int iNumFrms = encodeConfig.startFrameIdx; iNumFrms < (unsigned int)encodeConfig.endFrameIdx + 1; iNumFrms += (preloadedFrameCount-1))
            {
                if (iNumFrms != (MIN(iNumFrms + preloadedFrameCount - 1, (unsigned int)encodeConfig.endFrameIdx)))
                    printf("\nLoading Frames [%d,%d] into system memory\n", iNumFrms, (MIN(iNumFrms + preloadedFrameCount-1, (unsigned int)encodeConfig.endFrameIdx)));

                for (unsigned int frameCount = iNumFrms; frameCount < MIN(iNumFrms + preloadedFrameCount, (unsigned int)encodeConfig.endFrameIdx + 1); frameCount++)
                {
                    yuvInput[0] = &yuvLoaded[0][(frameCount % preloadedFrameCount)*lumaPlaneSize];
                    yuvInput[1] = &yuvLoaded[1][(frameCount % preloadedFrameCount)*chromaPlaneSize];
                    yuvInput[2] = &yuvLoaded[2][(frameCount % preloadedFrameCount)*chromaPlaneSize];
                    loadframe(yuvInput, hInput, frameCount, encodeConfig.width, encodeConfig.height, numBytesRead, encodeConfig.inputFormat);
                    yuvInput[0] = NULL;
                    yuvInput[1] = NULL;
                    yuvInput[2] = NULL;
                }

                for (unsigned int frameCount = iNumFrms; frameCount < MIN(iNumFrms + preloadedFrameCount -1, (unsigned int)encodeConfig.endFrameIdx); frameCount++)
                {
                    numBytesRead = 0;
                    memset(&stMEOnly, 0, sizeof(stMEOnly));
                    stMEOnly.width = encodeConfig.width;
                    stMEOnly.height = encodeConfig.height;
                    stMEOnly.stride[0] = encodeConfig.width;
                    stMEOnly.stride[1] = (chromaFormatIDC == 3) ? encodeConfig.width : encodeConfig.width >> 1;
                    stMEOnly.stride[2] = (chromaFormatIDC == 3) ? encodeConfig.width : encodeConfig.width >> 1;
                    stMEOnly.inputFrameIndex = frameCount + 1;
                    stMEOnly.referenceFrameIndex = frameCount;

                    //ref
                    stMEOnly.yuv[0][0] = &yuvLoaded[0][(frameCount % preloadedFrameCount)*lumaPlaneSize];
                    stMEOnly.yuv[0][1] = &yuvLoaded[1][(frameCount % preloadedFrameCount)*chromaPlaneSize];
                    stMEOnly.yuv[0][2] = &yuvLoaded[2][(frameCount % preloadedFrameCount)*chromaPlaneSize];
                    //input
                    stMEOnly.yuv[1][0] = &yuvLoaded[0][((frameCount + 1) % preloadedFrameCount)*lumaPlaneSize];
                    stMEOnly.yuv[1][1] = &yuvLoaded[1][((frameCount + 1) % preloadedFrameCount)*chromaPlaneSize];
                    stMEOnly.yuv[1][2] = &yuvLoaded[2][((frameCount + 1) % preloadedFrameCount)*chromaPlaneSize];
                    RunMotionEstimationOnly(&stMEOnly, false);

                    stMEOnly.yuv[0][0] = NULL;
                    stMEOnly.yuv[0][1] = NULL;
                    stMEOnly.yuv[0][2] = NULL;
                    stMEOnly.yuv[1][0] = NULL;
                    stMEOnly.yuv[1][1] = NULL;
                    stMEOnly.yuv[1][2] = NULL;
                }
            }
        }
        RunMotionEstimationOnly(NULL, true);
        if (encodeConfig.fOutput)
        {
            fclose(encodeConfig.fOutput);
        }

        if (hInput)
        {
            nvCloseFile(hInput);
        }

        Deinitialize(encodeConfig.deviceType);
        for (unsigned int i = 0; i < 3; i++)
        {
            for (unsigned int j = 0; j < 2; j++)
            {
                 if (stMEOnly.yuv[j][i])
                 {
                    delete[] stMEOnly.yuv[j][i];
                    stMEOnly.yuv[j][i] = NULL;
                 }
            }
         }
         printf("Done!! \n");
         return bError ? 1 : 0;
    }

    
    yuv[0] = new(std::nothrow) uint8_t[lumaPlaneSize];
    yuv[1] = new(std::nothrow) uint8_t[chromaPlaneSize];
    yuv[2] = new(std::nothrow) uint8_t[chromaPlaneSize];       
    NvQueryPerformanceCounter(&lStart);

    if (yuv[0] == NULL || yuv[1] == NULL || yuv[2] == NULL)
    {
       PRINTERR("\nvEncoder.exe Error: Failed to allocate memory for yuv array!\n");
       return 1;
    }

    int8_t *qpDeltaMapArray = NULL; 
    uint32_t qpDeltaMapArraySize = 0;

    if (encodeConfig.generateQpDeltaMap) {
        const int nQpDelta = 15;
        const int nMbSize = encodeConfig.codec == NV_ENC_H264 ? 16 : 32;
        int cxMap = (encodeConfig.width + nMbSize - 1) / nMbSize, cyMap = (encodeConfig.height + nMbSize - 1) / nMbSize;
        qpDeltaMapArraySize = cxMap * cyMap;
        qpDeltaMapArray = new int8_t[qpDeltaMapArraySize];
        for (int y = 0; y < cyMap; y++) {
            for (int x = 0; x < cxMap; x++) {
                qpDeltaMapArray[y * cxMap + x] = (x - cxMap / 2) * (y - cyMap / 2) > 0 ? nQpDelta : -nQpDelta;
            }
        }
        FILE *fpQpDeltaMap = fopen(encodeConfig.qpDeltaMapFile, "wb");
        if (!fpQpDeltaMap) {
            PRINTERR("\nvEncoder.exe Error: Failed to create QP delta map file\n");
            return 1;
        }
        fwrite(qpDeltaMapArray, qpDeltaMapArraySize, 1, fpQpDeltaMap);
        fclose(fpQpDeltaMap);
    }
    //Sample application demostrates 9 hints per MB considering numCandsPerBlkNxN = 1 for all partitionmodes enabled for L0 predictor."
    if (encodeConfig.enableExternalMEHint)
    {
        int numOfHintsPerMB = 0;
        int numOfMBs = 0;
        uint32_t ceaBufferSize = 0;

        //Sample application demostrates 9 hints per MB considering numCandsPerBlkNxN = 1 for all partitionmodes enabled for L0 predictor."
        numOfHintsPerMB = NUM_OF_MVHINTS_PER_BLOCK16x16 * 1 +
            NUM_OF_MVHINTS_PER_BLOCK16x8 * 1 +
            NUM_OF_MVHINTS_PER_BLOCK8x16 * 1 +
            NUM_OF_MVHINTS_PER_BLOCK8x8 * 1;
        numOfMBs = ((encodeConfig.width + 15) >> 4) * ((encodeConfig.height + 15) >> 4);
        ceaBufferSize = numOfMBs * sizeof(NVENC_EXTERNAL_ME_HINT) * numOfHintsPerMB;
        ceaBuffer = (NVENC_EXTERNAL_ME_HINT *)malloc(ceaBufferSize);
        if (ceaBuffer == NULL)
        {
            printf("Memory allocation failure \n");
            return NV_ENC_ERR_OUT_OF_MEMORY;
        }
        fpExternalHint = fopen(encodeConfig.externalHintInputFile, "r");
        if (!fpExternalHint)
        {
            printf("Failed to open file \n");
            return NV_ENC_ERR_INVALID_PARAM;
        }
    }

    for (int frm = encodeConfig.startFrameIdx; frm <= encodeConfig.endFrameIdx; frm++)
    {
        numBytesRead = 0;
        loadframe(yuv, hInput, frm, encodeConfig.width, encodeConfig.height, numBytesRead, encodeConfig.inputFormat);
        if (numBytesRead == 0)
            break;

        EncodeFrameConfig stEncodeFrame;
        memset(&stEncodeFrame, 0, sizeof(stEncodeFrame));
        stEncodeFrame.yuv[0] = yuv[0];
        stEncodeFrame.yuv[1] = yuv[1];
        stEncodeFrame.yuv[2] = yuv[2];

        stEncodeFrame.stride[0] = encodeConfig.width * (encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV420_10BIT || encodeConfig.inputFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT ? 2 : 1);
        stEncodeFrame.stride[1] = stEncodeFrame.stride[2] = chromaFormatIDC == 3 ? stEncodeFrame.stride[0] : stEncodeFrame.stride[0] / 2;
        stEncodeFrame.width = encodeConfig.width;
        stEncodeFrame.height = encodeConfig.height;
        stEncodeFrame.qpDeltaMapArray = qpDeltaMapArray;
        stEncodeFrame.qpDeltaMapArraySize = qpDeltaMapArraySize;

        if (encodeConfig.enableExternalMEHint)
        {
            stEncodeFrame.meHintCountsPerBlock[0].numCandsPerBlk16x16 = 1;
            stEncodeFrame.meHintCountsPerBlock[0].numCandsPerBlk8x16 = 1;
            stEncodeFrame.meHintCountsPerBlock[0].numCandsPerBlk16x8 = 1;
            stEncodeFrame.meHintCountsPerBlock[0].numCandsPerBlk8x8 = 1;

            if (MeonlyOutPutToCEABufferPacker(fpExternalHint, encodeConfig.maxWidth, encodeConfig.maxHeight, frm,
                                              &ceaBuffer, &stEncodeFrame.meHintCountsPerBlock[0]) == NV_ENC_SUCCESS)
            {
                stEncodeFrame.meExternalHints = ceaBuffer;
            }
        }
        EncodeFrame(&stEncodeFrame, false, encodeConfig.width, encodeConfig.height);
        numFramesEncoded++;
    }

    nvStatus = EncodeFrame(NULL, true, encodeConfig.width, encodeConfig.height);
    if (nvStatus != NV_ENC_SUCCESS)
    {
        bError = true;
        goto exit;
    }

    if (numFramesEncoded > 0)
    {
        NvQueryPerformanceCounter(&lEnd);
        NvQueryPerformanceFrequency(&lFreq);
        double elapsedTime = (double)(lEnd - lStart);
        printf("Encoded %d frames in %6.2fms\n", numFramesEncoded, (elapsedTime*1000.0)/lFreq);
        printf("Avergage Encode Time : %6.2fms\n", ((elapsedTime*1000.0)/numFramesEncoded)/lFreq);
    }

exit:
    if (ceaBuffer)
    {
        free(ceaBuffer);
        ceaBuffer = NULL;
    }
    if (encodeConfig.fOutput)
    {
        fclose(encodeConfig.fOutput);
    }
    if (fpExternalHint)
    {
        fclose(fpExternalHint);
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

    if (qpDeltaMapArray) {
        delete[] qpDeltaMapArray;
    }

    return bError ? 1 : 0;
}

NVENCSTATUS CNvEncoder::RunMotionEstimationOnly(MEOnlyConfig *pMEOnly, bool bFlush)
{
    uint8_t *pInputSurface = NULL;
    uint8_t *pInputSurfaceCh = NULL;
    uint32_t lockedPitch = 0;
    uint32_t dwSurfHeight = 0;
    static unsigned int dwCurWidth = 0;
    static unsigned int dwCurHeight = 0;
    HRESULT hr = S_OK;
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    MotionEstimationBuffer *pMEBuffer = NULL;

    if (bFlush)
    {
        FlushMVOutputBuffer();
        return NV_ENC_SUCCESS;
    }

    if (!pMEOnly)
    {
        assert(0);
        return NV_ENC_ERR_INVALID_PARAM;
    }

    pMEBuffer = m_MVBufferQueue.GetAvailable();
    if(!pMEBuffer)
    {
        m_pNvHWEncoder->ProcessMVOutput(m_MVBufferQueue.GetPending());
        pMEBuffer = m_MVBufferQueue.GetAvailable();
    }
    pMEBuffer->inputFrameIndex = pMEOnly->inputFrameIndex;
    pMEBuffer->referenceFrameIndex = pMEOnly->referenceFrameIndex;
    dwCurWidth = pMEOnly->width;
    dwCurHeight = pMEOnly->height;

    for (int i = 0; i < 2; i++)
    {
        unsigned char *pInputSurface = NULL;
        unsigned char *pInputSurfaceCh = NULL;
        nvStatus = m_pNvHWEncoder->NvEncLockInputBuffer(pMEBuffer->stInputBfr[i].hInputSurface, (void**)&pInputSurface, &lockedPitch);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;

        if (pMEBuffer->stInputBfr[i].bufferFmt == NV_ENC_BUFFER_FORMAT_NV12_PL)
        {
            pInputSurfaceCh = pInputSurface + (pMEBuffer->stInputBfr[i].dwHeight*lockedPitch);
            convertYUVpitchtoNV12(pMEOnly->yuv[i][0], pMEOnly->yuv[i][1], pMEOnly->yuv[i][2], pInputSurface, pInputSurfaceCh, dwCurWidth, dwCurHeight, dwCurWidth, lockedPitch);
        }
        else if (pMEBuffer->stInputBfr[i].bufferFmt == NV_ENC_BUFFER_FORMAT_YUV444)
        {
            unsigned char *pInputSurfaceCb = pInputSurface + (pMEBuffer->stInputBfr[i].dwHeight * lockedPitch);
            unsigned char *pInputSurfaceCr = pInputSurfaceCb + (pMEBuffer->stInputBfr[i].dwHeight * lockedPitch);
            convertYUVpitchtoYUV444(pMEOnly->yuv[i][0], pMEOnly->yuv[i][1], pMEOnly->yuv[i][2], pInputSurface, pInputSurfaceCb, pInputSurfaceCr, dwCurWidth, dwCurHeight, dwCurWidth, lockedPitch);
        }
        else if (pMEBuffer->stInputBfr[i].bufferFmt == NV_ENC_BUFFER_FORMAT_YUV420_10BIT)
        {
            unsigned char *pInputSurfaceCh = pInputSurface + (pMEBuffer->stInputBfr[i].dwHeight*lockedPitch);
            convertYUV10pitchtoP010PL((uint16_t *)pMEOnly->yuv[i][0], (uint16_t *)pMEOnly->yuv[i][1], (uint16_t *)pMEOnly->yuv[i][2], (uint16_t *)pInputSurface, (uint16_t *)pInputSurfaceCh, dwCurWidth, dwCurHeight, dwCurWidth, lockedPitch);
        }
        else
        {
            unsigned char *pInputSurfaceCb = pInputSurface + (pMEBuffer->stInputBfr[i].dwHeight * lockedPitch);
            unsigned char *pInputSurfaceCr = pInputSurfaceCb + (pMEBuffer->stInputBfr[i].dwHeight * lockedPitch);
            convertYUV10pitchtoYUV444((uint16_t *)pMEOnly->yuv[i][0], (uint16_t *)pMEOnly->yuv[i][1], (uint16_t *)pMEOnly->yuv[i][2], (uint16_t *)pInputSurface, (uint16_t *)pInputSurfaceCb, (uint16_t *)pInputSurfaceCr, dwCurWidth, dwCurHeight, dwCurWidth, lockedPitch);
        }
        nvStatus = m_pNvHWEncoder->NvEncUnlockInputBuffer(pMEBuffer->stInputBfr[i].hInputSurface);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;
    }

    nvStatus = m_pNvHWEncoder->NvRunMotionEstimationOnly(pMEBuffer, pMEOnly);
    if (nvStatus != NV_ENC_SUCCESS)
    {
        PRINTERR("nvEncRunMotionEstimationOnly error:0x%x\n", nvStatus);
        assert(0);
    }
    return nvStatus;

}

NVENCSTATUS CNvEncoder::EncodeFrame(EncodeFrameConfig *pEncodeFrame, bool bFlush, uint32_t width, uint32_t height)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    uint32_t lockedPitch = 0;
    EncodeBuffer *pEncodeBuffer = NULL;

    if (bFlush)
    {
        FlushEncoder();
        return NV_ENC_SUCCESS;
    }

    if (!pEncodeFrame)
    {
        return NV_ENC_ERR_INVALID_PARAM;
    }

    pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
    if(!pEncodeBuffer)
    {
        m_pNvHWEncoder->ProcessOutput(m_EncodeBufferQueue.GetPending());
        pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
    }

    unsigned char *pInputSurface;
    
    nvStatus = m_pNvHWEncoder->NvEncLockInputBuffer(pEncodeBuffer->stInputBfr.hInputSurface, (void**)&pInputSurface, &lockedPitch);
    if (nvStatus != NV_ENC_SUCCESS)
        return nvStatus;

    if (pEncodeBuffer->stInputBfr.bufferFmt == NV_ENC_BUFFER_FORMAT_NV12_PL)
    {
        unsigned char *pInputSurfaceCh = pInputSurface + (pEncodeBuffer->stInputBfr.dwHeight*lockedPitch);
        convertYUVpitchtoNV12(pEncodeFrame->yuv[0], pEncodeFrame->yuv[1], pEncodeFrame->yuv[2], pInputSurface, pInputSurfaceCh, width, height, width, lockedPitch);
    }
    else if (pEncodeBuffer->stInputBfr.bufferFmt == NV_ENC_BUFFER_FORMAT_YUV444)
    {
        unsigned char *pInputSurfaceCb = pInputSurface + (pEncodeBuffer->stInputBfr.dwHeight * lockedPitch);
        unsigned char *pInputSurfaceCr = pInputSurfaceCb + (pEncodeBuffer->stInputBfr.dwHeight * lockedPitch);
        convertYUVpitchtoYUV444(pEncodeFrame->yuv[0], pEncodeFrame->yuv[1], pEncodeFrame->yuv[2], pInputSurface, pInputSurfaceCb, pInputSurfaceCr, width, height, width, lockedPitch);
    }
    else if (pEncodeBuffer->stInputBfr.bufferFmt == NV_ENC_BUFFER_FORMAT_YUV420_10BIT)
    {
        unsigned char *pInputSurfaceCh = pInputSurface + (pEncodeBuffer->stInputBfr.dwHeight*lockedPitch);
        convertYUV10pitchtoP010PL((uint16_t *)pEncodeFrame->yuv[0], (uint16_t *)pEncodeFrame->yuv[1], (uint16_t *)pEncodeFrame->yuv[2], (uint16_t *)pInputSurface, (uint16_t *)pInputSurfaceCh, width, height, width, lockedPitch);
    }
    else
    {
        unsigned char *pInputSurfaceCb = pInputSurface + (pEncodeBuffer->stInputBfr.dwHeight * lockedPitch);
        unsigned char *pInputSurfaceCr = pInputSurfaceCb + (pEncodeBuffer->stInputBfr.dwHeight * lockedPitch);
        convertYUV10pitchtoYUV444((uint16_t *)pEncodeFrame->yuv[0], (uint16_t *)pEncodeFrame->yuv[1], (uint16_t *)pEncodeFrame->yuv[2], (uint16_t *)pInputSurface, (uint16_t *)pInputSurfaceCb, (uint16_t *)pInputSurfaceCr, width, height, width, lockedPitch);
    }
    nvStatus = m_pNvHWEncoder->NvEncUnlockInputBuffer(pEncodeBuffer->stInputBfr.hInputSurface);
    if (nvStatus != NV_ENC_SUCCESS)
        return nvStatus;

    nvStatus = m_pNvHWEncoder->NvEncEncodeFrame(pEncodeBuffer, NULL, width, height, (NV_ENC_PIC_STRUCT)m_uPicStruct, pEncodeFrame->qpDeltaMapArray, pEncodeFrame->qpDeltaMapArraySize, pEncodeFrame->meExternalHints, pEncodeFrame->meHintCountsPerBlock);
    return nvStatus;
}

int main(int argc, char **argv)
{
    CNvEncoder nvEncoder;
    return nvEncoder.EncodeMain(argc, argv);
}
