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

#include <string>
#include "NvEncoderGLInterop.h"
#include "../common/inc/nvUtils.h"
#include "../common/inc/nvFileIO.h"
#include "../common/inc/helper_string.h"

using namespace std;

#define BITSTREAM_BUFFER_SIZE 2*1024*1024

CNvEncoderGLInterop::CNvEncoderGLInterop()
{
    m_pNvHWEncoder = new CNvHWEncoder;

    m_dwWindow = 0;

    m_uEncodeBufferCount = 0;
    memset(&m_stEncoderInput, 0, sizeof(m_stEncoderInput));
    memset(&m_stEOSOutputBfr, 0, sizeof(m_stEOSOutputBfr));

    memset(&m_stEncodeBuffer, 0, sizeof(m_stEncodeBuffer));
}

CNvEncoderGLInterop::~CNvEncoderGLInterop()
{
    if (m_pNvHWEncoder)
    {
        delete m_pNvHWEncoder;
        m_pNvHWEncoder = NULL;
    }
}

NVENCSTATUS CNvEncoderGLInterop::InitOGL(int argc, char **argv)
{
    int window = 0;

    /* freeglut is being used only for context creation; we don't intend to
       display the window or render to it */
    glutInit(&argc, argv);

    glutInitDisplayMode(GLUT_RGB | GLUT_SINGLE);
    glutInitWindowSize(16, 16);

    window = glutCreateWindow("NvEncoder");
    if (!window)
        return NV_ENC_ERR_NO_ENCODE_DEVICE;

    m_dwWindow = window;

    glutHideWindow();

    return NV_ENC_SUCCESS;
}

void CNvEncoderGLInterop::TransferToTexture(uint32_t tex, uint32_t width, uint32_t height)
{
    glBindTexture(GL_TEXTURE_RECTANGLE, tex);
    glTexSubImage2D(GL_TEXTURE_RECTANGLE, 0, 0, 0, width, height,
                    GL_RED, GL_UNSIGNED_BYTE, m_yuv);
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
}

NVENCSTATUS CNvEncoderGLInterop::AllocateIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    GLenum result = GL_NO_ERROR;
    uint32_t dwTexWidth = 0, dwTexHeight = 0;

    m_EncodeBufferQueue.Initialize(m_stEncodeBuffer, m_uEncodeBufferCount);

    m_yuv = new uint8_t[3 * uInputWidth * uInputHeight / 2];

    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        glGenTextures(1, &m_stEncodeBuffer[i].stInputBfr.dwTex);

        glBindTexture(GL_TEXTURE_RECTANGLE, m_stEncodeBuffer[i].stInputBfr.dwTex);

        dwTexWidth = (uInputWidth + 0x3) & ~0x3;
        dwTexHeight = 3 * uInputHeight / 2;
        m_stEncodeBuffer[i].stInputBfr.uNV12Stride = dwTexWidth;
        glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_R8, dwTexWidth, dwTexHeight, 0, GL_RED,
                     GL_UNSIGNED_BYTE, NULL);

        if ((result = glGetError()) != GL_NO_ERROR)
        {
            PRINTERR("glTexImage2D failed with error %d\n", result);
            glBindTexture(GL_TEXTURE_RECTANGLE, 0);
            glDeleteTextures(1, &m_stEncodeBuffer[i].stInputBfr.dwTex);
            return NV_ENC_ERR_OUT_OF_MEMORY;
        }

        glBindTexture(GL_TEXTURE_RECTANGLE, 0);

        NV_ENC_INPUT_RESOURCE_OPENGL_TEX resource;
        resource.texture = m_stEncodeBuffer[i].stInputBfr.dwTex;
        resource.target = GL_TEXTURE_RECTANGLE;

        nvStatus = m_pNvHWEncoder->NvEncRegisterResource(NV_ENC_INPUT_RESOURCE_TYPE_OPENGL_TEX, (void*)&resource,
                                                         uInputWidth, uInputHeight,
                                                         m_stEncodeBuffer[i].stInputBfr.uNV12Stride,
                                                         &m_stEncodeBuffer[i].stInputBfr.nvRegisteredResource,
                                                         NV_ENC_BUFFER_FORMAT_IYUV);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;

        m_stEncodeBuffer[i].stInputBfr.bufferFmt = NV_ENC_BUFFER_FORMAT_IYUV;
        m_stEncodeBuffer[i].stInputBfr.dwWidth = uInputWidth;
        m_stEncodeBuffer[i].stInputBfr.dwHeight = uInputHeight;

        nvStatus = m_pNvHWEncoder->NvEncCreateBitstreamBuffer(BITSTREAM_BUFFER_SIZE, &m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;
        m_stEncodeBuffer[i].stOutputBfr.dwBitstreamBufferSize = BITSTREAM_BUFFER_SIZE;

        m_stEncodeBuffer[i].stOutputBfr.hOutputEvent = NULL;
    }

    m_stEOSOutputBfr.bEOSFlag = TRUE;
    m_stEOSOutputBfr.hOutputEvent = NULL;

    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderGLInterop::ReleaseIOBuffers()
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    if (m_yuv)
    {
        delete m_yuv;
    }

    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        nvStatus = m_pNvHWEncoder->NvEncUnregisterResource(m_stEncodeBuffer[i].stInputBfr.nvRegisteredResource);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;

        glDeleteTextures(1, &m_stEncodeBuffer[i].stInputBfr.dwTex);

        m_pNvHWEncoder->NvEncDestroyBitstreamBuffer(m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
        m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer = NULL;
    }

    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderGLInterop::FlushEncoder()
{
    NVENCSTATUS nvStatus = m_pNvHWEncoder->NvEncFlushEncoderQueue(m_stEOSOutputBfr.hOutputEvent);
    if(nvStatus != NV_ENC_SUCCESS)
    {
        assert(0);
        return nvStatus;
    }

    EncodeBuffer *pEncodeBuffer = m_EncodeBufferQueue.GetPending();
    while(pEncodeBuffer)
    {
        m_pNvHWEncoder->ProcessOutput(pEncodeBuffer);
        // UnMap the input buffer after frame is done
        if (pEncodeBuffer->stInputBfr.hInputSurface)
        {
            nvStatus = m_pNvHWEncoder->NvEncUnmapInputResource(pEncodeBuffer->stInputBfr.hInputSurface);
            pEncodeBuffer->stInputBfr.hInputSurface = NULL;
        }
        pEncodeBuffer = m_EncodeBufferQueue.GetPending();
    }

    return nvStatus;
}

NVENCSTATUS CNvEncoderGLInterop::Deinitialize()
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    ReleaseIOBuffers();

    nvStatus = m_pNvHWEncoder->NvEncDestroyEncoder();
    if (nvStatus != NV_ENC_SUCCESS)
    {
        assert(0);
    }

    glutDestroyWindow(m_dwWindow);
    m_dwWindow = 0;

    return NV_ENC_SUCCESS;
}

NVENCSTATUS loadframe(uint8_t *yuvInput, HANDLE hInputYUVFile, uint32_t frmIdx, uint32_t width, uint32_t height, uint32_t &numBytesRead)
{
    uint64_t fileOffset;
    uint32_t result;
    uint32_t dwInFrameSize = width*height + (width*height)/2;
    fileOffset = (uint64_t)dwInFrameSize * frmIdx;
    result = nvSetFilePointer64(hInputYUVFile, fileOffset, NULL, FILE_BEGIN);
    if (result == INVALID_SET_FILE_POINTER)
    {
        return NV_ENC_ERR_INVALID_PARAM;
    }
    nvReadFile(hInputYUVFile, yuvInput, 3 * width * height / 2, &numBytesRead, NULL);

    return NV_ENC_SUCCESS;
}

void PrintHelp()
{
    printf("Usage : NvEncoderGLInterop \n"
        "-i <string>                  Specify input yuv420 file\n"
        "-o <string>                  Specify output bitstream file\n"
        "-size <int int>              Specify input resolution <width height>\n"
        "\n### Optional parameters ###\n"
        "-startf <integer>            Specify start index for encoding. Default is 0\n"
        "-endf <integer>              Specify end index for encoding. Default is end of file\n"
        "-codec <integer>             Specify the codec \n"
        "                                 0: H264\n"
        "                                 1: HEVC\n"
        "-preset <string>             Specify the preset for encoder settings\n"
        "                                 hq : nvenc HQ \n"
        "                                 hp : nvenc HP \n"
        "                                 lowLatencyHP : nvenc low latency HP \n"
        "                                 lowLatencyHQ : nvenc low latency HQ \n"
        "                                 lossless : nvenc Lossless HP \n"
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
        "-help                        Prints Help Information\n\n"
        );
}

int CNvEncoderGLInterop::EncodeMain(int argc, char *argv[])
{
    HANDLE hInput;
    uint32_t numBytesRead = 0;
    unsigned long long lStart, lEnd, lFreq;
    int numFramesEncoded = 0;
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    bool bError = false;
    EncodeBuffer *pEncodeBuffer;
    EncodeConfig encodeConfig;

    memset(&encodeConfig, 0, sizeof(EncodeConfig));

    encodeConfig.endFrameIdx = INT_MAX;
    encodeConfig.bitrate = 5000000;
    encodeConfig.rcMode = NV_ENC_PARAMS_RC_CONSTQP;
    encodeConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
    encodeConfig.deviceType = NV_ENC_OGL;
    encodeConfig.codec = NV_ENC_H264;
    encodeConfig.fps = 30;
    encodeConfig.qp = 28;
    encodeConfig.i_quant_factor = DEFAULT_I_QFACTOR;
    encodeConfig.b_quant_factor = DEFAULT_B_QFACTOR;
    encodeConfig.i_quant_offset = DEFAULT_I_QOFFSET;
    encodeConfig.b_quant_offset = DEFAULT_B_QOFFSET;
    encodeConfig.presetGUID = NV_ENC_PRESET_DEFAULT_GUID;
    encodeConfig.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

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

    // Create an OpenGL context
    nvStatus = InitOGL(argc, argv);
    if (nvStatus != NV_ENC_SUCCESS)
        return nvStatus;

    nvStatus = m_pNvHWEncoder->Initialize(NULL, NV_ENC_DEVICE_TYPE_OPENGL);
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
        (encodeConfig.presetGUID == NV_ENC_PRESET_LOSSLESS_HP_GUID) ? "LOSSLESS_HP" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID) ? "LOW_LATENCY_DEFAULT" : "DEFAULT");
    printf("\n");

    nvStatus = m_pNvHWEncoder->CreateEncoder(&encodeConfig);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    m_uEncodeBufferCount = encodeConfig.numB + 4;

    nvStatus = AllocateIOBuffers(encodeConfig.width, encodeConfig.height);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    NvQueryPerformanceCounter(&lStart);

    for (int frm = encodeConfig.startFrameIdx; frm <= encodeConfig.endFrameIdx; frm++)
    {
        numBytesRead = 0;
        loadframe(m_yuv, hInput, frm, encodeConfig.width, encodeConfig.height, numBytesRead);
        if (numBytesRead == 0)
            break;

        pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
        if(!pEncodeBuffer)
        {
            pEncodeBuffer = m_EncodeBufferQueue.GetPending();
            m_pNvHWEncoder->ProcessOutput(pEncodeBuffer);
            // UnMap the input buffer after frame done
            if (pEncodeBuffer->stInputBfr.hInputSurface)
            {
                nvStatus = m_pNvHWEncoder->NvEncUnmapInputResource(pEncodeBuffer->stInputBfr.hInputSurface);
                pEncodeBuffer->stInputBfr.hInputSurface = NULL;
            }
            pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
        }

        TransferToTexture(pEncodeBuffer->stInputBfr.dwTex, pEncodeBuffer->stInputBfr.dwWidth,
                          3 * pEncodeBuffer->stInputBfr.dwHeight / 2);

        nvStatus = m_pNvHWEncoder->NvEncMapInputResource(pEncodeBuffer->stInputBfr.nvRegisteredResource, &pEncodeBuffer->stInputBfr.hInputSurface);
        if (nvStatus != NV_ENC_SUCCESS)
        {
            PRINTERR("Failed to Map input buffer %p\n", pEncodeBuffer->stInputBfr.hInputSurface);
            return nvStatus;
        }

        m_pNvHWEncoder->NvEncEncodeFrame(pEncodeBuffer, NULL, encodeConfig.width, encodeConfig.height);
        numFramesEncoded++;
    }

    FlushEncoder();

    if (numFramesEncoded > 0)
    {
        NvQueryPerformanceCounter(&lEnd);
        NvQueryPerformanceFrequency(&lFreq);
        double elapsedTime = (double)(lEnd - lStart);
        printf("Encoded %d frames in %6.2fms\n", numFramesEncoded, (elapsedTime*1000.0)/lFreq);
        printf("Average Encode Time : %6.2fms\n", ((elapsedTime*1000.0)/numFramesEncoded)/lFreq);
    }

    if (encodeConfig.fOutput)
    {
        fclose(encodeConfig.fOutput);
    }

    if (hInput)
    {
        nvCloseFile(hInput);
    }

    Deinitialize();

    return bError ? 1 : 0;
}

int main(int argc, char **argv)
{
    CNvEncoderGLInterop nvEncoder;
    return nvEncoder.EncodeMain(argc, argv);
}
