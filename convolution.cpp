
#include"function_map.hpp"
#include<cstdint>
#include<cassert>

//START
extern "C"
uint64_t* convolution(uint64_t * source, uint64_t source_size,
                      uint64_t * kernel, uint64_t kernel_size, 
                      uint64_t * target, uint64_t _target_size, int32_t tile_size) {
    
    // Here's the the key part:
    uint64_t target_size = source_size - kernel_size;
    for(int64_t i = 0; i < target_size; i++) {
        for(int64_t j = 0; j < kernel_size; j++) {
            target[i] += source[i + j] * kernel[j];
        }
    }
    return target;
}

FUNCTION(convolution, convolution);
//END




extern "C"
uint64_t* convolution_new_loop(uint64_t * source, uint64_t source_size,
                   uint64_t * kernel, uint64_t kernel_size, 
                   uint64_t * target, uint64_t _target_size, int32_t tile_size) {
    uint64_t target_size = source_size - kernel_size;
    for(int64_t i = 0; i < target_size; i++) {
        for(int64_t jj = 0; jj < kernel_size; jj += tile_size) { // We create a new loop variable jj that advanced one chunk at a time.
            for(int64_t j = jj; j < kernel_size && j < jj + tile_size; j++) { // We iterate over the chunk.  The more complicated termination 
                // condition keeps ups from running off the end of the arry
                target[i] += source[i + j] * kernel[j];
            }
        }
    }
    return target;
}

FUNCTION(convolution, convolution_new_loop);



extern "C"
uint64_t* convolution_split(uint64_t * source, uint64_t source_size,
                uint64_t * kernel, uint64_t kernel_size, 
                uint64_t * target, uint64_t _target_size, int32_t tile_size) {

    uint64_t target_size = source_size - kernel_size;

    // Here's the the key part:
    for(int32_t i = 0; i < target_size; i++) {
        for(int32_t jj = 0; jj < kernel_size; jj += 2048) { // We create a new loop variable jj that advanced one chunk at a time.
            for(int32_t j = jj; j < kernel_size && j < jj + 2048; j++) { // We iterate over the chunk.  The more complicated termination 
                // condition keeps ups from running off the end of the arry
                target[i] += source[i + j] * kernel[j];
            }
        }
    }
    
    return target;
}

FUNCTION(convolution, convolution_split);




extern "C"
uint64_t* convolution_tiled(uint64_t * source, uint64_t source_size,
                uint64_t * kernel, uint64_t kernel_size, 
                uint64_t * target, uint64_t _target_size, int32_t tile_size) {

    uint64_t target_size = source_size - kernel_size;

    for(int32_t jj = 0; jj < kernel_size; jj += tile_size) {  // Move the jj chunk loop outside
        for(int32_t i = 0; i < target_size; i++) {
            for(int32_t j = jj; j < kernel_size && j < jj + tile_size; j++) {
                target[i] += source[i + j] * kernel[j];
            }
        }
    }
    return target;  
}

FUNCTION(convolution, convolution_tiled);




extern "C"
uint64_t*  __attribute__((optimize("unroll-loops"))) convolution_tiled_unrolled(uint64_t * source, uint64_t source_size,
                     uint64_t * kernel, uint64_t kernel_size, 
                     uint64_t * target, uint64_t _target_size, int32_t tile_size) {

    uint64_t target_size = source_size - kernel_size;
    for(int32_t jj = 0; jj < kernel_size; jj += tile_size) {  // Move the jj chunk loop outside
        for(int32_t i = 0; i < target_size; i++) {
            for(int32_t j = jj; j < kernel_size && j < jj + tile_size; j++) {
                target[i] += source[i + j] * kernel[j];
            }
        }
    }
    return target;  
}

FUNCTION(convolution, convolution_tiled_unrolled);


extern "C"
uint64_t* __attribute__((optimize("unroll-loops"))) convolution_tiled_split(uint64_t * source, uint64_t source_size,
                  uint64_t * kernel, uint64_t kernel_size, 
                  uint64_t * target, uint64_t _target_size, int32_t tile_size) {
    uint64_t target_size = source_size - kernel_size;
    int32_t real_tile_size = tile_size/8 * 8; // this clears the low 3 bits.  Check the assembly!
    assert(tile_size>=8);

    for(int32_t jj = 0; jj < kernel_size; jj += real_tile_size) {  // Move the jj chunk loop outside
        for(int32_t i = 0; i < target_size; i++) {
            if (jj + real_tile_size > kernel_size) {
                for(int32_t j = jj; j < kernel_size; j++) {
                    target[i] += source[i + j] * kernel[j];
                } 
            } else {
                for(int32_t j = jj; j < jj + real_tile_size; j++) {
                    target[i] += source[i + j] * kernel[j];
                }
            }
        }
    }
    return target;
}

FUNCTION(convolution, convolution_tiled_split);



extern "C"
uint64_t* __attribute__((optimize("unroll-loops")))convolution_tiled_fixed_tile(uint64_t * source, uint64_t source_size,
                  uint64_t * kernel, uint64_t kernel_size, 
                  uint64_t * target, uint64_t _target_size, int32_t tile_size) {
    uint64_t target_size = source_size - kernel_size;
#define real_tile_size 512
//    int32_t real_tile_size = tile_size/8 * 8; // this clears the low 3 bits.  Check the assembly!

    
    for(int32_t jj = 0; jj < kernel_size; jj += real_tile_size) {  // Move the jj chunk loop outside
        for(int32_t i = 0; i < target_size; i++) {
            if (jj + real_tile_size > kernel_size) {
                for(int32_t j = jj; j < kernel_size; j++) {
                    target[i] += source[i + j] * kernel[j];
                } 
            } else {
                for(int32_t j = jj; j < jj + real_tile_size; j++) {
                    target[i] += source[i + j] * kernel[j];
                }
            }
        }
    }
    return target;
}

FUNCTION(convolution, convolution_tiled_fixed_tile);


//-O3 -funroll-all-loops
