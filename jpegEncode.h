#ifndef JPEGENCODE_H
#define JPEGENCODE_H

#include <vector>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t* input_data;
    uint32_t input_size;
    uint8_t* y_plane;
    uint8_t* u_plane;
    uint8_t* v_plane;
} yuvSplitStruct_t;

std::pair<unsigned long, unsigned char*> tjYUYVtoJPEG(const uint8_t* input, const int width, const int height);
std::pair<unsigned long, unsigned char*> tjYUYVtoYUV(const uint8_t* input, const int width, const int height);
std::pair<unsigned long, unsigned char*> compressYUYVtoJPEG(const uint8_t* input, const int width, const int height);

#ifdef __cplusplus
}
#endif

#endif
