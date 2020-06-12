#include "jpegEncode.h"

#include <stdio.h>
#include <jpeglib.h>
#include <turbojpeg.h>

#ifdef __ARM_NEON
__attribute__((optimize("O1")))
void splitYuvPlanes(yuvSplitStruct_t* yuvStruct) {
  __asm__ volatile(
      "PUSH {r4}\n"                     /* Save callee-save registers R4 and R5 on the stack */
      "PUSH {r5}\n"                     /* r1 is the pointer to the input structure ( r0 is 'this' because c++ ) */
      "ldr r1 , [r0]\n"                 /* reuse r0 scratch register for the address of our frame input */
      "ldr r2 , [r0, #4]\n"             /* use r2 scratch register to store the size in bytes of the YUYV frame */
      "ldr r3 , [r0, #8]\n"             /* use r3 scratch register to store the destination Y plane address */
      "ldr r4 , [r0, #12]\n"            /* use r4 register to store the destination U plane address */
      "ldr r5 , [r0, #16]\n"            /* use r5 register to store the destination V plane address */
      "mov r2, r2, lsr #5\n"            /* Divide number of bytes by 32 because we process 16 pixels at a time */
      "loopYUYV:\n"
          "vld4.8 {d0-d3}, [r1]!\n"     /* Load 8 YUYV elements from our frame into d0-d3, increment frame pointer */
          "vst2.8 {d0,d2}, [r3]!\n"     /* Store both Y elements into destination y plane, increment plane pointer */
          "vst1.8 {d1}, [r4]!\n"        /* Store U element into destination u plane, increment plane pointer */
          "vst1.8 {d3}, [r5]!\n"        /* Store V element into destination v plane, increment plane pointer */
          "subs r2, r2, #1\n"           /* Decrement the loop counter */
      "bgt loopYUYV\n"                  /* Loop until entire frame is processed */
      "POP {r5}\n"                      /* Restore callee-save registers */
      "POP {r4}\n"
  );
}
#else
void splitYuvPlanes(yuvSplitStruct_t* yuvStruct) {
  auto data = yuvStruct->input_data;
  auto size = yuvStruct->input_size;
  auto y = yuvStruct->y_plane;
  auto u = yuvStruct->u_plane;
  auto v = yuvStruct->v_plane;
  for ( size_t c = 0 ; c < ( size - 4 ) ; c+=4 ) {
    *y = *data; // Y0
    y++;
    data++;

    *u = *data; // U0
    u++;
    data++;

    *y = *data; // Y1
    y++;
    data++;

    *v = *data; // V0
    v++;
    data++;
  }
}
#endif

std::pair<unsigned long, unsigned char*> tjYUYVtoJPEG(const uint8_t* input, const int width, const int height) {
  static yuvSplitStruct_t s_yuvSplit { nullptr, 0, nullptr, nullptr, nullptr };
  static unsigned long s_y_size { 0 }, s_chro_size { 0 };
  unsigned long y_size = tjPlaneSizeYUV(0, width, 0, height, TJSAMP_422);
  unsigned long chro_size = tjPlaneSizeYUV(1, width, 0, height, TJSAMP_422);
  if(s_y_size != y_size || s_chro_size != chro_size) {
    if(s_yuvSplit.y_plane != nullptr) {
      delete[] s_yuvSplit.y_plane;
      s_yuvSplit.y_plane = nullptr;
      s_yuvSplit.u_plane = nullptr;
      s_yuvSplit.v_plane = nullptr;
    }
    s_y_size = y_size;
    s_chro_size = chro_size;
    s_yuvSplit.y_plane = new uint8_t[s_y_size + (s_chro_size * 2)]();
    s_yuvSplit.u_plane = &s_yuvSplit.y_plane[s_y_size];
    s_yuvSplit.v_plane = &s_yuvSplit.u_plane[s_chro_size];
  }
  s_yuvSplit.input_data = input;
  s_yuvSplit.input_size = tjBufSizeYUV(width, height, TJSAMP_422);

  splitYuvPlanes(&s_yuvSplit);
  tjhandle handle = tjInitCompress();
  unsigned char* dstBuf = nullptr;
  unsigned long dstSize = 0;
  tjCompressFromYUV(handle, s_yuvSplit.y_plane, width, 4/*pad*/, height, TJSAMP_422, 
                      &dstBuf, &dstSize, 70/*jpegQual*/, TJFLAG_FASTDCT);
  tjDestroy(handle);
  return std::make_pair(dstSize, dstBuf);
}

std::pair<unsigned long, unsigned char*> tjYUYVtoYUV(const uint8_t* input, const int width, const int height) {
  static yuvSplitStruct_t s_yuvSplit { nullptr, 0, nullptr, nullptr, nullptr };
  static unsigned long s_y_size { 0 }, s_chro_size { 0 };
  unsigned long y_size = tjPlaneSizeYUV(0, width, 0, height, TJSAMP_422);
  unsigned long chro_size = tjPlaneSizeYUV(1, width, 0, height, TJSAMP_422);
  if(s_y_size != y_size || s_chro_size != chro_size) {
    if(s_yuvSplit.y_plane != nullptr) {
      delete[] s_yuvSplit.y_plane;
      s_yuvSplit.y_plane = nullptr;
      s_yuvSplit.u_plane = nullptr;
      s_yuvSplit.v_plane = nullptr;
    }
    s_y_size = y_size;
    s_chro_size = chro_size;
    s_yuvSplit.y_plane = new uint8_t[s_y_size + (s_chro_size * 2)]();
    s_yuvSplit.u_plane = &s_yuvSplit.y_plane[s_y_size];
    s_yuvSplit.v_plane = &s_yuvSplit.u_plane[s_chro_size];
  }
  auto totalSize = s_y_size + (s_chro_size * 2);
  s_yuvSplit.input_data = input;
  s_yuvSplit.input_size = tjBufSizeYUV(width, height, TJSAMP_422);
  splitYuvPlanes(&s_yuvSplit);
  return std::make_pair(totalSize, s_yuvSplit.y_plane);
}

std::pair<unsigned long, unsigned char*> compressYUYVtoJPEG(const uint8_t* input, const int width, const int height) {
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  //JSAMPROW row_ptr[1];
  //int row_stride;

  unsigned char* outbuffer = NULL;
  unsigned long outlen = 0;

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  // jrow is a libjpeg row of samples array of 1 row pointer
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_YCbCr; //libJPEG expects YUV 3bytes, 24bit
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 50, TRUE);

  jpeg_mem_dest(&cinfo, &outbuffer, &outlen);
  jpeg_start_compress(&cinfo, TRUE);

  std::vector<uint8_t> tmprowbuf(width * 3);

  JSAMPROW row_pointer[1];
  row_pointer[0] = &tmprowbuf[0];
  while (cinfo.next_scanline < cinfo.image_height) {
      unsigned i, j;
      unsigned offset = cinfo.next_scanline * cinfo.image_width * 2; //offset to the correct row
      for (i = 0, j = 0; i < cinfo.image_width * 2; i += 4, j += 6) { //input strides by 4 bytes, output strides by 6 (2 pixels)
          tmprowbuf[j + 0] = input[offset + i + 0]; // Y (unique to this pixel)
          tmprowbuf[j + 1] = input[offset + i + 1]; // U (shared between pixels)
          tmprowbuf[j + 2] = input[offset + i + 3]; // V (shared between pixels)
          tmprowbuf[j + 3] = input[offset + i + 2]; // Y (unique to this pixel)
          tmprowbuf[j + 4] = input[offset + i + 1]; // U (shared between pixels)
          tmprowbuf[j + 5] = input[offset + i + 3]; // V (shared between pixels)
      }
      jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  return std::make_pair(outlen, outbuffer);
}
