/*
* Copyright 2017-2018 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

#include <iostream>
#include <algorithm>

#include "nvcuvid.h"
#include "../Utils/NvCodecUtils.h"
#include "NvDecoder/NvDecoder.h"

#define CUDA_DRVAPI_CALL( call )                                                                                                 \
    do                                                                                                                           \
    {                                                                                                                            \
        CUresult err__ = call;                                                                                                   \
        if (err__ != CUDA_SUCCESS)                                                                                               \
        {                                                                                                                        \
            const char *szErrName = NULL;                                                                                        \
            cuGetErrorName(err__, &szErrName);                                                                                   \
            std::ostringstream errorLog;                                                                                         \
            errorLog << "CUDA driver API error " << szErrName ;                                                                  \
            throw NVDECException::makeNVDECException(errorLog.str(), err__, __FUNCTION__, __FILE__, __LINE__);                   \
        }                                                                                                                        \
    }                                                                                                                            \
    while (0)

static const char * GetVideoCodecString(cudaVideoCodec eCodec) {
    static struct {
        cudaVideoCodec eCodec;
        const char *name;
    } aCodecName [] = {
        { cudaVideoCodec_MPEG1,     "MPEG-1"       },
        { cudaVideoCodec_MPEG2,     "MPEG-2"       },
        { cudaVideoCodec_MPEG4,     "MPEG-4 (ASP)" },
        { cudaVideoCodec_VC1,       "VC-1/WMV"     },
        { cudaVideoCodec_H264,      "AVC/H.264"    },
        { cudaVideoCodec_JPEG,      "M-JPEG"       },
        { cudaVideoCodec_H264_SVC,  "H.264/SVC"    },
        { cudaVideoCodec_H264_MVC,  "H.264/MVC"    },
        { cudaVideoCodec_HEVC,      "H.265/HEVC"   },
        { cudaVideoCodec_VP8,       "VP8"          },
        { cudaVideoCodec_VP9,       "VP9"          },
        { cudaVideoCodec_NumCodecs, "Invalid"      },
        { cudaVideoCodec_YUV420,    "YUV  4:2:0"   },
        { cudaVideoCodec_YV12,      "YV12 4:2:0"   },
        { cudaVideoCodec_NV12,      "NV12 4:2:0"   },
        { cudaVideoCodec_YUYV,      "YUYV 4:2:2"   },
        { cudaVideoCodec_UYVY,      "UYVY 4:2:2"   },
    };

    if (eCodec >= 0 && eCodec <= cudaVideoCodec_NumCodecs) {
        return aCodecName[eCodec].name;
    }
    for (int i = cudaVideoCodec_NumCodecs + 1; i < sizeof(aCodecName) / sizeof(aCodecName[0]); i++) {
        if (eCodec == aCodecName[i].eCodec) {
            return aCodecName[eCodec].name;
        }
    }
    return "Unknown";
}

static const char * GetVideoChromaFormatString(cudaVideoChromaFormat eChromaFormat) {
    static struct {
        cudaVideoChromaFormat eChromaFormat;
        const char *name;
    } aChromaFormatName[] = {
        { cudaVideoChromaFormat_Monochrome, "YUV 400 (Monochrome)" },
        { cudaVideoChromaFormat_420,        "YUV 420"              },
        { cudaVideoChromaFormat_422,        "YUV 422"              },
        { cudaVideoChromaFormat_444,        "YUV 444"              },
    };

    if (eChromaFormat >= 0 && eChromaFormat < sizeof(aChromaFormatName) / sizeof(aChromaFormatName[0])) {
        return aChromaFormatName[eChromaFormat].name;
    }
    return "Unknown";
}

static unsigned long GetNumDecodeSurfaces(cudaVideoCodec eCodec, unsigned int nWidth, unsigned int nHeight) {
    if (eCodec == cudaVideoCodec_VP9) {
        return 12;
    }

    if (eCodec == cudaVideoCodec_H264 || eCodec == cudaVideoCodec_H264_SVC || eCodec == cudaVideoCodec_H264_MVC) {
        // assume worst-case of 20 decode surfaces for H264
        return 20;
    }

    if (eCodec == cudaVideoCodec_HEVC) {
        // ref HEVC spec: A.4.1 General tier and level limits
        // currently assuming level 6.2, 8Kx4K
        int MaxLumaPS = 35651584;
        int MaxDpbPicBuf = 6;
        int PicSizeInSamplesY = (int)(nWidth * nHeight);
        int MaxDpbSize;
        if (PicSizeInSamplesY <= (MaxLumaPS>>2))
            MaxDpbSize = MaxDpbPicBuf * 4;
        else if (PicSizeInSamplesY <= (MaxLumaPS>>1))
            MaxDpbSize = MaxDpbPicBuf * 2;
        else if (PicSizeInSamplesY <= ((3*MaxLumaPS)>>2))
            MaxDpbSize = (MaxDpbPicBuf * 4) / 3;
        else
            MaxDpbSize = MaxDpbPicBuf;
        return (std::min)(MaxDpbSize, 16) + 4;
    }

    return 8;
}

int NvDecoder::HandleVideoSequence(CUVIDEOFORMAT *pVideoFormat)
{
    m_videoInfo << "Video Input Information" << std::endl
        << "\tCodec        : " << GetVideoCodecString(pVideoFormat->codec) << std::endl
        << "\tFrame rate   : " << pVideoFormat->frame_rate.numerator << "/" << pVideoFormat->frame_rate.denominator 
            << " = " << 1.0 * pVideoFormat->frame_rate.numerator / pVideoFormat->frame_rate.denominator << " fps" << std::endl
        << "\tSequence     : " << (pVideoFormat->progressive_sequence ? "Progressive" : "Interlaced") << std::endl
        << "\tCoded size   : [" << pVideoFormat->coded_width << ", " << pVideoFormat->coded_height << "]" << std::endl
        << "\tDisplay area : [" << pVideoFormat->display_area.left << ", " << pVideoFormat->display_area.top << ", " 
            << pVideoFormat->display_area.right << ", " << pVideoFormat->display_area.bottom << "]" << std::endl
        << "\tChroma       : " << GetVideoChromaFormatString(pVideoFormat->chroma_format) << std::endl
        << "\tBit depth    : " << pVideoFormat->bit_depth_luma_minus8 + 8
    ;
    m_videoInfo << std::endl;

    int nDecodeSurface = GetNumDecodeSurfaces(pVideoFormat->codec, pVideoFormat->coded_width, pVideoFormat->coded_height);

    CUVIDDECODECAPS decodecaps;
    memset(&decodecaps, 0, sizeof(decodecaps));

    decodecaps.eCodecType = pVideoFormat->codec;
    decodecaps.eChromaFormat = pVideoFormat->chroma_format;
    decodecaps.nBitDepthMinus8 = pVideoFormat->bit_depth_luma_minus8; 

    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_cuContext));
    NVDEC_API_CALL(cuvidGetDecoderCaps(&decodecaps));
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(NULL));
    
    if(!decodecaps.bIsSupported){
        NVDEC_THROW_ERROR("Codec not supported on this GPU", CUDA_ERROR_NOT_SUPPORTED);
        return nDecodeSurface;
    }
    

    if ((pVideoFormat->coded_width > decodecaps.nMaxWidth) || 
        (pVideoFormat->coded_height > decodecaps.nMaxHeight)){
        
        std::ostringstream errorString;
        errorString << std::endl
                    << "Resolution          : " << pVideoFormat->coded_width << "x" << pVideoFormat->coded_height << std::endl
                    << "Max Supported (wxh) : " << decodecaps.nMaxWidth << "x" << decodecaps.nMaxHeight << std::endl
                    << "Resolution not supported on this GPU";

        const std::string cErr = errorString.str();
        NVDEC_THROW_ERROR(cErr, CUDA_ERROR_NOT_SUPPORTED);
        return nDecodeSurface;
    }
    
    if ((pVideoFormat->coded_width>>4)*(pVideoFormat->coded_height>>4) > decodecaps.nMaxMBCount){
        
        std::ostringstream errorString;
        errorString << std::endl
                    << "MBCount             : " << (pVideoFormat->coded_width >> 4)*(pVideoFormat->coded_height >> 4) << std::endl
                    << "Max Supported mbcnt : " << decodecaps.nMaxMBCount << std::endl
                    << "MBCount not supported on this GPU";

        const std::string cErr = errorString.str();
        NVDEC_THROW_ERROR(cErr, CUDA_ERROR_NOT_SUPPORTED);
        return nDecodeSurface;
    }
    
    if (m_nWidth && m_nHeight) {
    // cuvidCreateDecoder() has been called before, and now there's possible config change
        if (m_eCodec == cudaVideoCodec_VP9) {
        // For VP9, driver will handle the change
            return nDecodeSurface;
        }
        if (pVideoFormat->coded_width == m_videoFormat.coded_width && pVideoFormat->coded_height == m_videoFormat.coded_height) {
        // No resolution change
            return nDecodeSurface;
        }
        NVDEC_THROW_ERROR("Dynamic resolution change isn't supported - decoded result may be incorrect", CUDA_ERROR_NOT_SUPPORTED);
        return nDecodeSurface;
    }

    // eCodec has been set in the constructor (for parser). Here it's set again for potential correction
    m_eCodec = pVideoFormat->codec;
    m_eChromaFormat = pVideoFormat->chroma_format;
    m_nBitDepthMinus8 = pVideoFormat->bit_depth_luma_minus8;
    m_videoFormat = *pVideoFormat;

    CUVIDDECODECREATEINFO videoDecodeCreateInfo = { 0 };
    videoDecodeCreateInfo.CodecType = pVideoFormat->codec;
    videoDecodeCreateInfo.ChromaFormat = pVideoFormat->chroma_format;
    videoDecodeCreateInfo.OutputFormat = pVideoFormat->bit_depth_luma_minus8 ? cudaVideoSurfaceFormat_P016 : cudaVideoSurfaceFormat_NV12;
    videoDecodeCreateInfo.bitDepthMinus8 = pVideoFormat->bit_depth_luma_minus8;
    videoDecodeCreateInfo.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
    videoDecodeCreateInfo.ulNumOutputSurfaces = 2;
    // With PreferCUVID, JPEG is still decoded by CUDA while video is decoded by NVDEC hardware
    videoDecodeCreateInfo.ulCreationFlags = cudaVideoCreate_PreferCUVID;
    videoDecodeCreateInfo.ulNumDecodeSurfaces = nDecodeSurface;
    videoDecodeCreateInfo.vidLock = m_ctxLock;
    videoDecodeCreateInfo.ulWidth = pVideoFormat->coded_width;
    videoDecodeCreateInfo.ulHeight = pVideoFormat->coded_height;

    if (!(m_cropRect.r && m_cropRect.b) && !(m_resizeDim.w && m_resizeDim.h)) {
        m_nWidth = pVideoFormat->display_area.right - pVideoFormat->display_area.left;
        m_nHeight = pVideoFormat->display_area.bottom - pVideoFormat->display_area.top;
        videoDecodeCreateInfo.ulTargetWidth = pVideoFormat->coded_width;
        videoDecodeCreateInfo.ulTargetHeight = pVideoFormat->coded_height;
    } else {
        if (m_resizeDim.w && m_resizeDim.h) {
            videoDecodeCreateInfo.display_area.left = pVideoFormat->display_area.left;
            videoDecodeCreateInfo.display_area.top = pVideoFormat->display_area.top;
            videoDecodeCreateInfo.display_area.right = pVideoFormat->display_area.right;
            videoDecodeCreateInfo.display_area.bottom = pVideoFormat->display_area.bottom;
            m_nWidth = m_resizeDim.w;
            m_nHeight = m_resizeDim.h;
        }

        if (m_cropRect.r && m_cropRect.b) {
            videoDecodeCreateInfo.display_area.left = m_cropRect.l;
            videoDecodeCreateInfo.display_area.top = m_cropRect.t;
            videoDecodeCreateInfo.display_area.right = m_cropRect.r;
            videoDecodeCreateInfo.display_area.bottom = m_cropRect.b;
            m_nWidth = m_cropRect.r - m_cropRect.l;
            m_nHeight = m_cropRect.b - m_cropRect.t;
        }
        videoDecodeCreateInfo.ulTargetWidth = m_nWidth;
        videoDecodeCreateInfo.ulTargetHeight = m_nHeight;
    }
    m_nSurfaceHeight = videoDecodeCreateInfo.ulTargetHeight;

    m_videoInfo << "Video Decoding Params:" << std::endl
        << "\tNum Surfaces : " << videoDecodeCreateInfo.ulNumDecodeSurfaces << std::endl
        << "\tCrop         : [" << videoDecodeCreateInfo.display_area.left << ", " << videoDecodeCreateInfo.display_area.top << ", "
        << videoDecodeCreateInfo.display_area.right << ", " << videoDecodeCreateInfo.display_area.bottom << "]" << std::endl
        << "\tResize       : " << videoDecodeCreateInfo.ulTargetWidth << "x" << videoDecodeCreateInfo.ulTargetHeight << std::endl
        << "\tDeinterlace  : " << std::vector<const char *>{"Weave", "Bob", "Adaptive"}[videoDecodeCreateInfo.DeinterlaceMode] 
    ;
    m_videoInfo << std::endl;

    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_cuContext));
    NVDEC_API_CALL(cuvidCreateDecoder(&m_hDecoder, &videoDecodeCreateInfo));
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(NULL));
    return nDecodeSurface;
}

int NvDecoder::HandlePictureDecode(CUVIDPICPARAMS *pPicParams) {
    if (!m_hDecoder) 
    {
        NVDEC_THROW_ERROR("Decoder not initialized.", CUDA_ERROR_NOT_INITIALIZED);
        return false;
    }

    NVDEC_API_CALL(cuvidDecodePicture(m_hDecoder, pPicParams));
    return 1;
}

int NvDecoder::HandlePictureDisplay(CUVIDPARSERDISPINFO *pDispInfo) {
    CUVIDPROCPARAMS videoProcessingParameters = {};
    videoProcessingParameters.progressive_frame = pDispInfo->progressive_frame;
    videoProcessingParameters.second_field = pDispInfo->repeat_first_field + 1;
    videoProcessingParameters.top_field_first = pDispInfo->top_field_first;
    videoProcessingParameters.unpaired_field = pDispInfo->repeat_first_field < 0;
    videoProcessingParameters.output_stream = m_cuvidStream;

    CUdeviceptr dpSrcFrame = 0;
    unsigned int nSrcPitch = 0;
    NVDEC_API_CALL(cuvidMapVideoFrame(m_hDecoder, pDispInfo->picture_index, &dpSrcFrame,
        &nSrcPitch, &videoProcessingParameters));
    uint8_t *pDecodedFrame = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mtxVPFrame);
        if ((unsigned)++m_nDecodedFrame > m_vpFrame.size())
        {
            // Not enough frames in stock
            m_nFrameAlloc++;
            uint8_t *pFrame = NULL;
            if (m_bUseDeviceFrame)
            {
                CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_cuContext));
                if (m_bDeviceFramePitched)
                {
                    CUDA_DRVAPI_CALL(cuMemAllocPitch((CUdeviceptr *)&pFrame, &m_nDeviceFramePitch, m_nWidth * (m_nBitDepthMinus8 ? 2 : 1), m_nHeight * 3 / 2, 16));
                }
                else 
                {
                    CUDA_DRVAPI_CALL(cuMemAlloc((CUdeviceptr *)&pFrame, GetFrameSize()));
                }
                CUDA_DRVAPI_CALL(cuCtxPopCurrent(NULL));
            }
            else 
            {
                pFrame = new uint8_t[GetFrameSize()];
            }
            m_vpFrame.push_back(pFrame);
        }
        pDecodedFrame = m_vpFrame[m_nDecodedFrame - 1];
    }

    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_cuContext));
    CUDA_MEMCPY2D m = { 0 };
    m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    m.srcDevice = dpSrcFrame;
    m.srcPitch = nSrcPitch;
    m.dstMemoryType = m_bUseDeviceFrame ? CU_MEMORYTYPE_DEVICE : CU_MEMORYTYPE_HOST;
    m.dstDevice = (CUdeviceptr)(m.dstHost = pDecodedFrame);
    m.dstPitch = m_nDeviceFramePitch ? m_nDeviceFramePitch : m_nWidth * (m_nBitDepthMinus8 ? 2 : 1);
    m.WidthInBytes = m_nWidth * (m_nBitDepthMinus8 ? 2 : 1);
    m.Height = m_nHeight;
    CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&m, m_cuvidStream));
    m.srcDevice = (CUdeviceptr)((uint8_t *)dpSrcFrame + m.srcPitch * m_nSurfaceHeight);
    m.dstDevice = (CUdeviceptr)(m.dstHost = pDecodedFrame + m.dstPitch * m_nHeight);
    m.Height = m_nHeight / 2;
    CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&m, m_cuvidStream));
    CUDA_DRVAPI_CALL(cuStreamSynchronize(m_cuvidStream));
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(NULL));

    if ((int)m_vTimestamp.size() < m_nDecodedFrame) {
        m_vTimestamp.resize(m_vpFrame.size());
    }
    m_vTimestamp[m_nDecodedFrame - 1] = pDispInfo->timestamp;

    NVDEC_API_CALL(cuvidUnmapVideoFrame(m_hDecoder, dpSrcFrame));
    return 1;
}

NvDecoder::NvDecoder(CUcontext cuContext, int nWidth, int nHeight, bool bUseDeviceFrame, cudaVideoCodec eCodec, std::mutex *pMutex,
    bool bLowLatency, bool bDeviceFramePitched, const Rect *pCropRect, const Dim *pResizeDim) :
    m_cuContext(cuContext), m_bUseDeviceFrame(bUseDeviceFrame), m_eCodec(eCodec), m_pMutex(pMutex), m_bDeviceFramePitched(bDeviceFramePitched)
{
    if (pCropRect) m_cropRect = *pCropRect;
    if (pResizeDim) m_resizeDim = *pResizeDim;

    NVDEC_API_CALL(cuvidCtxLockCreate(&m_ctxLock, cuContext));

    CUVIDPARSERPARAMS videoParserParameters = {};
    videoParserParameters.CodecType = eCodec;
    videoParserParameters.ulMaxNumDecodeSurfaces = 1;
    videoParserParameters.ulMaxDisplayDelay = bLowLatency ? 0 : 1;
    videoParserParameters.pUserData = this;
    videoParserParameters.pfnSequenceCallback = HandleVideoSequenceProc;
    videoParserParameters.pfnDecodePicture = HandlePictureDecodeProc;
    videoParserParameters.pfnDisplayPicture = HandlePictureDisplayProc;
    if (m_pMutex) m_pMutex->lock();
    NVDEC_API_CALL(cuvidCreateVideoParser(&m_hParser, &videoParserParameters));
    if (m_pMutex) m_pMutex->unlock();
}

NvDecoder::~NvDecoder() {

    cuCtxPushCurrent(m_cuContext);
    cuCtxPopCurrent(NULL);

    if (m_hParser) {
        cuvidDestroyVideoParser(m_hParser);
    }

    if (m_hDecoder) {
        if (m_pMutex) m_pMutex->lock();
        cuvidDestroyDecoder(m_hDecoder);
        if (m_pMutex) m_pMutex->unlock();
    }

    std::lock_guard<std::mutex> lock(m_mtxVPFrame);
    if (m_vpFrame.size() != m_nFrameAlloc)
    {
        //LOG(WARNING) << "nFrameAlloc(" << m_nFrameAlloc << ") != m_vpFrame.size()(" << m_vpFrame.size() << ")";
    }
    for (uint8_t *pFrame : m_vpFrame)
    {
        if (m_bUseDeviceFrame)
        {
            if (m_pMutex) m_pMutex->lock();
            cuCtxPushCurrent(m_cuContext);
            cuMemFree((CUdeviceptr)pFrame);
            cuCtxPopCurrent(NULL);
            if (m_pMutex) m_pMutex->unlock();
        }
        else
        {
            delete[] pFrame;
        }
    }
    cuvidCtxLockDestroy(m_ctxLock);
}

bool NvDecoder::Decode(const uint8_t *pData, int nSize, uint8_t ***pppFrame, int *pnFrameReturned, uint32_t flags, int64_t **ppTimestamp, int64_t timestamp, CUstream stream)
{
    if (!m_hParser)
    {
        NVDEC_THROW_ERROR("Parser not initialized.", CUDA_ERROR_NOT_INITIALIZED);
        return false;
    }

    m_nDecodedFrame = 0;
    CUVIDSOURCEDATAPACKET packet = {0};
    packet.payload = pData;
    packet.payload_size = nSize;
    packet.flags = flags | CUVID_PKT_TIMESTAMP;
    packet.timestamp = timestamp;
    if (!pData || nSize == 0) {
        packet.flags |= CUVID_PKT_ENDOFSTREAM;
    }
    m_cuvidStream = stream;
    if (m_pMutex) m_pMutex->lock();
    NVDEC_API_CALL(cuvidParseVideoData(m_hParser, &packet));
    if (m_pMutex) m_pMutex->unlock();
    m_cuvidStream = 0;

    if (m_nDecodedFrame > 0)
    {
        if (pppFrame) 
        {
            m_vpFrameRet.clear();
            std::lock_guard<std::mutex> lock(m_mtxVPFrame);
            m_vpFrameRet.insert(m_vpFrameRet.begin(), m_vpFrame.begin(), m_vpFrame.begin() + m_nDecodedFrame);
            *pppFrame = &m_vpFrameRet[0];
        }
        if (ppTimestamp) 
        {
            *ppTimestamp = &m_vTimestamp[0];
        }
    }
    if (pnFrameReturned)
    {
        *pnFrameReturned = m_nDecodedFrame;
    }
    return true;
}

bool NvDecoder::DecodeLockFrame(const uint8_t *pData, int nSize, uint8_t ***pppFrame, int *pnFrameReturned, uint32_t flags, int64_t **ppTimestamp, int64_t timestamp, CUstream stream)
{
    bool ret = Decode(pData, nSize, pppFrame, pnFrameReturned, flags, ppTimestamp, timestamp, stream);
    std::lock_guard<std::mutex> lock(m_mtxVPFrame);
    m_vpFrame.erase(m_vpFrame.begin(), m_vpFrame.begin() + m_nDecodedFrame);
    return true;
}

void NvDecoder::UnlockFrame(uint8_t **ppFrame, int nFrame)
{
    std::lock_guard<std::mutex> lock(m_mtxVPFrame);
    m_vpFrame.insert(m_vpFrame.end(), &ppFrame[0], &ppFrame[nFrame]);
}
