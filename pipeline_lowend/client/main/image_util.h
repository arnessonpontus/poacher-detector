/*
  * ESPRESSIF MIT License
  *
  * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
  *
  * Permission is hereby granted for use on ESPRESSIF SYSTEMS products only, in which case,
  * it is free of charge, to any person obtaining a copy of this software and associated
  * documentation files (the "Software"), to deal in the Software without restriction, including
  * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
  * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
  * to do so, subject to the following conditions:
  *
  * The above copyright notice and this permission notice shall be included in all copies or
  * substantial portions of the Software.
  *
  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
  * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
  * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
  * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  *
  */
#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>
#include <math.h>

#define MAX_VALID_COUNT_PER_IMAGE (30)

#define DL_IMAGE_MIN(A, B) ((A) < (B) ? (A) : (B))
#define DL_IMAGE_MAX(A, B) ((A) < (B) ? (B) : (A))

#define RGB565_MASK_RED 0xF800
#define RGB565_MASK_GREEN 0x07E0
#define RGB565_MASK_BLUE 0x001F

    typedef enum
    {
        BINARY,
    } en_threshold_mode;
    

    static inline void rgb565_to_888(uint16_t in, uint8_t *dst)
    {                                           /*{{{*/
        dst[0] = (in & RGB565_MASK_BLUE) << 3;  // blue
        dst[1] = (in & RGB565_MASK_GREEN) >> 3; // green
        dst[2] = (in & RGB565_MASK_RED) >> 8;   // red
    }                                           /*}}}*/

    static inline void rgb888_to_565(uint16_t *in, uint8_t r, uint8_t g, uint8_t b)
    { /*{{{*/
        uint16_t rgb565 = 0;
        rgb565 = ((r >> 3) << 11);
        rgb565 |= ((g >> 2) << 5);
        rgb565 |= (b >> 3);
        *in = rgb565;
    } /*}}}*/


    /**
     * @brief 
     * 
     * @param dimage 
     * @param dw 
     * @param dh 
     * @param dc 
     * @param simage 
     * @param sw 
     * @param sc 
     */
    void image_zoom_in_twice(uint8_t *dimage,
                             int dw,
                             int dh,
                             int dc,
                             uint8_t *simage,
                             int sw,
                             int sc);

    /**
     * @brief 
     * 
     * @param dst_image 
     * @param src_image 
     * @param dst_w 
     * @param dst_h 
     * @param dst_c 
     * @param src_w 
     * @param src_h 
     */
    void image_resize_linear(uint8_t *dst_image, uint8_t *src_image, int dst_w, int dst_h, int dst_c, int src_w, int src_h);

    /**
     * @brief 
     * 
     * @param corp_image 
     * @param src_image 
     * @param rotate_angle 
     * @param ratio 
     * @param center 
     */
    void image_cropper(uint8_t *corp_image, uint8_t *src_image, int dst_w, int dst_h, int dst_c, int src_w, int src_h, float rotate_angle, float ratio, float *center);

    /**
     * @brief 
     * 
     * @param bmp 
     * @param m 
     * @param count 
     */
    void transform_output_image(uint16_t *bmp, uint8_t *m, int count);
    void transform_output_image_adjustable(uint16_t *bmp, uint8_t *m, int src_w, int src_h, int dst_w, int dst_h);

    void image_abs_diff(uint8_t *dst, uint8_t *src1, uint8_t *src2, int count);
    void image_threshold(uint8_t *dst, uint8_t *src, int threshold, int value, int count, en_threshold_mode mode);
    void image_erode(uint8_t *dst, uint8_t *src, int src_w, int src_h, int src_c);

    typedef float matrixType;
    typedef struct
    {
        int w;
        int h;
        matrixType **array;
    } Matrix;

    Matrix *matrix_alloc(int h, int w);
    void matrix_free(Matrix *m);
    Matrix *get_similarity_matrix(float *srcx, float *srcy, float *dstx, float *dsty, int num);

#ifdef __cplusplus
}
#endif