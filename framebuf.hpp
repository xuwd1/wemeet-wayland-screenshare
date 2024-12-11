#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <atomic>

#include "format.hpp"



struct FrameBuffer {

  FrameBuffer() = default;
  inline FrameBuffer(
    uint32_t height,
    uint32_t width,
    SpaVideoFormat_e const& format
  ){
    update_param(height, width, format);
  }
  
  inline void update_param(
    uint32_t height,
    uint32_t width,
    SpaVideoFormat_e const& format
  ){

    int bytes_per_pixel = spa_videoformat_bytesize(format);
    if (bytes_per_pixel == -1) {
      throw std::runtime_error("Invalid format");
    }

    // always store in (height, width):(stride, 1) layout
    uint32_t needed_stride = (width * bytes_per_pixel + 4 - 1) / 4 * 4;
    uint32_t needed_size = height * needed_stride;
    if (needed_size > data_size || data.get() == nullptr) {
      data_size = needed_size;
      data.reset(new uint8_t[data_size]);
    }
    this->height = height;
    this->width = width;
    this->row_byte_stride = needed_stride;
    this->format = format;
  }

  std::unique_ptr<uint8_t[]> data{nullptr};
  size_t data_size{0};
  uint32_t height{0};
  uint32_t width{0};
  uint32_t row_byte_stride{0};
  SpaVideoFormat_e format{SpaVideoFormat_e::INVALID};

};

struct SimpleZOHSingleFrameBufferQueue{

  SimpleZOHSingleFrameBufferQueue(
    uint32_t init_height,
    uint32_t init_width,
    SpaVideoFormat_e const& init_format
  ){
    read_index = 0;
    framebuf[0].update_param(init_height, init_width, init_format);
    framebuf[1].update_param(init_height, init_width, init_format);
  }

  inline const FrameBuffer& read() {
    return framebuf[read_index];
  }

  inline FrameBuffer& write() {
    return framebuf[read_index ^ 1];
  }

  inline void commit() {
    read_index ^= 1;
  }

  std::atomic<int> read_index;
  FrameBuffer framebuf[2];
};
