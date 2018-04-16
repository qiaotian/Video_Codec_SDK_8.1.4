
// BlockDim = 32x16
//GridDim = w/32*h/16
extern "C" __global__ void InterleaveUV( unsigned char *yuv_cb, unsigned char *yuv_cr, unsigned char *nv12_chroma,
                  int chroma_width, int chroma_height, int cb_pitch, int cr_pitch, int nv12_pitch )
{
    int x,y;
    unsigned char *pCb;
    unsigned char *pCr;
    unsigned char *pDst;
    x = blockIdx.x*blockDim.x+threadIdx.x;
    y = blockIdx.y*blockDim.y+threadIdx.y;

    if ((x < chroma_width) && (y < chroma_height))
    {
        pCb = yuv_cb + (y*cb_pitch);
        pCr = yuv_cr + (y*cr_pitch);
        pDst = nv12_chroma + y*nv12_pitch;
        pDst[x << 1]       = pCb[x];
        pDst[(x << 1) + 1] = pCr[x];
    }
}

// Simple NV12 bi-linear scaling using 2D textures
//
// blockDim {64,1,1}

texture<unsigned char, 2> luma_tex;
texture<uchar2, 2>  chroma_tex;

typedef struct {
    uchar2 uv0;
    uchar2 uv1;
} uvpair_t;

extern "C" __global__ void Scale_Bilinear_NV12(unsigned char *dst, int dst_uv_offset,
    int dst_width, int dst_height, int dst_pitch,
    float left, float right,
    float x_offset, float y_offset, float xc_offset, float yc_offset, float x_scale, float y_scale)
{
    unsigned char *dsty, *dstuv;
    uchar4 tmp0, tmp1;
    uvpair_t tmp2;
    int y0, tx;
    float x, yt, yb, yc, leftuv, rightuv;

    tx = (blockIdx.x << 8) + threadIdx.x * 4;
    if (tx < dst_width)
    {
        y0 = blockIdx.y << 1;
        // Luma
        dsty = dst + __umul24(y0, dst_pitch);
        yt = y_offset + (y0 + 0) * y_scale;
        yb = y_offset + (y0 + 1) * y_scale;
        x = 0.5f + fminf(fmaxf(x_offset + (tx + 0) * x_scale, left), right);
        tmp0.x = tex2D(luma_tex, x, yt);
        tmp1.x = tex2D(luma_tex, x, yb);
        x = 0.5f + fminf(fmaxf(x_offset + (tx + 1) * x_scale, left), right);
        tmp0.y = tex2D(luma_tex, x, yt);
        tmp1.y = tex2D(luma_tex, x, yb);
        x = 0.5f + fminf(fmaxf(x_offset + (tx + 2) * x_scale, left), right);
        tmp0.z = tex2D(luma_tex, x, yt);
        tmp1.z = tex2D(luma_tex, x, yb);
        x = 0.5f + fminf(fmaxf(x_offset + (tx + 3) * x_scale, left), right);
        tmp0.w = tex2D(luma_tex, x, yt);
        tmp1.w = tex2D(luma_tex, x, yb);
        *(uchar4 *)(dsty + tx) = tmp0;
        *(uchar4 *)(dsty + tx + dst_pitch) = tmp1;
        // Chroma
		dstuv = dst + dst_uv_offset + __umul24(blockIdx.y, dst_pitch);
        leftuv = 0.5f + 0.5f*left;
        rightuv = 0.5f*(right + 1.0f - left) - 1.0f;
        yc = yc_offset + (y0 >> 1) * y_scale;
        x = leftuv + fminf(fmaxf(xc_offset + (tx >> 1) * x_scale - left, 0.0f), rightuv);
        tmp2.uv0 = tex2D(chroma_tex, x, yc);
        x = leftuv + fminf(fmaxf(xc_offset + ((tx + 2) >> 1) * x_scale - left, 0.0f), rightuv);
        tmp2.uv1 = tex2D(chroma_tex, x, yc);
        *(uvpair_t *)(dstuv + tx) = tmp2;
    }
}
