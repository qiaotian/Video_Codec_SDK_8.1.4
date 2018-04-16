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

#if defined(NV_WINDOWS)
    #include <d3d9.h>
    #include <d3d10_1.h>
    #include <d3d11.h>
#pragma warning(disable : 4996)
#endif

#include "../common/inc/NvHWEncoder.h"

#define MAX_ENCODE_QUEUE 32
#define FRAME_QUEUE 240
#define NUM_OF_MVHINTS_PER_BLOCK8x8   4
#define NUM_OF_MVHINTS_PER_BLOCK8x16  2
#define NUM_OF_MVHINTS_PER_BLOCK16x8  2
#define NUM_OF_MVHINTS_PER_BLOCK16x16 1

enum
{
    PARTITION_TYPE_16x16,
    PARTITION_TYPE_8x8,
    PARTITION_TYPE_16x8,
    PARTITION_TYPE_8x16
};
#define SET_VER(configStruct, type) {configStruct.version = type##_VER;}

template<class T>
class CNvQueue {
    T** m_pBuffer;
    unsigned int m_uSize;
    unsigned int m_uPendingCount;
    unsigned int m_uAvailableIdx;
    unsigned int m_uPendingndex;
public:
    CNvQueue(): m_pBuffer(NULL), m_uSize(0), m_uPendingCount(0), m_uAvailableIdx(0),
                m_uPendingndex(0)
    {
    }

    ~CNvQueue()
    {
        delete[] m_pBuffer;
    }

    bool Initialize(T *pItems, unsigned int uSize)
    {
        m_uSize = uSize;
        m_uPendingCount = 0;
        m_uAvailableIdx = 0;
        m_uPendingndex = 0;
        m_pBuffer = new T *[m_uSize];
        for (unsigned int i = 0; i < m_uSize; i++)
        {
            m_pBuffer[i] = &pItems[i];
        }
        return true;
    }


    T * GetAvailable()
    {
        T *pItem = NULL;
        if (m_uPendingCount == m_uSize)
        {
            return NULL;
        }
        pItem = m_pBuffer[m_uAvailableIdx];
        m_uAvailableIdx = (m_uAvailableIdx+1)%m_uSize;
        m_uPendingCount += 1;
        return pItem;
    }

    T* GetPending()
    {
        if (m_uPendingCount == 0) 
        {
            return NULL;
        }

        T *pItem = m_pBuffer[m_uPendingndex];
        m_uPendingndex = (m_uPendingndex+1)%m_uSize;
        m_uPendingCount -= 1;
        return pItem;
    }
};

typedef struct _EncodeFrameConfig
{
    uint8_t  *yuv[3];
    uint32_t stride[3];
    uint32_t width;
    uint32_t height;
    int8_t *qpDeltaMapArray;
    uint32_t qpDeltaMapArraySize;
    NVENC_EXTERNAL_ME_HINT *meExternalHints;
    NVENC_EXTERNAL_ME_HINT_COUNTS_PER_BLOCKTYPE meHintCountsPerBlock[1];
}EncodeFrameConfig;

typedef enum 
{
    NV_ENC_DX9 = 0,
    NV_ENC_DX11 = 1,
    NV_ENC_CUDA = 2,
    NV_ENC_DX10 = 3,
} NvEncodeDeviceType;

class CNvEncoder
{
public:
    CNvEncoder();
    virtual ~CNvEncoder();

    int                                                  EncodeMain(int argc, char **argv);

protected:
    CNvHWEncoder                                        *m_pNvHWEncoder;
    uint32_t                                             m_uEncodeBufferCount;
    uint32_t                                             m_uPicStruct;
    void*                                                m_pDevice;
#if defined(NV_WINDOWS)
    IDirect3D9                                          *m_pD3D;
#endif

    CUcontext                                            m_cuContext;
    EncodeConfig                                         m_stEncoderInput;
    EncodeBuffer                                         m_stEncodeBuffer[MAX_ENCODE_QUEUE];
    MotionEstimationBuffer                               m_stMVBuffer[MAX_ENCODE_QUEUE];
    CNvQueue<EncodeBuffer>                               m_EncodeBufferQueue;
    CNvQueue<MotionEstimationBuffer>                     m_MVBufferQueue;
    EncodeOutputBuffer                                   m_stEOSOutputBfr; 

protected:
    NVENCSTATUS                                          Deinitialize(uint32_t devicetype);
    NVENCSTATUS                                          EncodeFrame(EncodeFrameConfig *pEncodeFrame, bool bFlush=false, uint32_t width=0, uint32_t height=0);
    NVENCSTATUS                                          InitD3D9(uint32_t deviceID = 0);
    NVENCSTATUS                                          InitD3D11(uint32_t deviceID = 0);
    NVENCSTATUS                                          InitD3D10(uint32_t deviceID = 0);
    NVENCSTATUS                                          InitCuda(uint32_t deviceID = 0);
    NVENCSTATUS                                          AllocateIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight, NV_ENC_BUFFER_FORMAT inputFormat);
    NVENCSTATUS                                          AllocateMVIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight, NV_ENC_BUFFER_FORMAT inputFormat);
    NVENCSTATUS                                          ReleaseIOBuffers();
    NVENCSTATUS                                          ReleaseMVIOBuffers();
    unsigned char*                                       LockInputBuffer(void * hInputSurface, uint32_t *pLockedPitch);
    NVENCSTATUS                                          FlushEncoder();
    void                                                 FlushMVOutputBuffer();
    NVENCSTATUS                                          RunMotionEstimationOnly(MEOnlyConfig *pMEOnly, bool bFlush);
};

// NVEncodeAPI entry point
typedef NVENCSTATUS (NVENCAPI *MYPROC)(NV_ENCODE_API_FUNCTION_LIST*); 
