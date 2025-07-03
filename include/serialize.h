//
// copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
//
#pragma once

#include <cassert>
#include <type_traits>
#include <vector>

namespace record
{

//
//
//
class Serializer
{
  static constexpr size_t ByteBits = 8;

  std::vector<uint8_t> buffer_;
  size_t bufferSize_;
  size_t bitPos_;

public:
  Serializer(size_t n) : buffer_(n), bufferSize_(n * ByteBits), bitPos_(0) {}

  // 書き込み終了(任意)
  void terminate(uint32_t mark) { writeBits(mark, sizeof(mark) * ByteBits); }

  // ビット書き込み
  template <class NumType>
  bool writeBits(NumType value, size_t bits)
  {
    assert(bits <= sizeof(NumType) * ByteBits);
    if (bitPos_ + bits > bufferSize_)
    {
      return false;
    }

    if constexpr (std::is_signed_v<NumType>)
    {
      if (value < 0)
      {
        value = -value;
        value |= (1 << (bits - 1));
      }
      else
      {
        value &= ~(1 << (bits - 1));
      }
    }

    for (size_t i = 0; i < bits; ++i)
    {
      size_t byteIndex = bitPos_ / ByteBits;
      size_t bitIndex = bitPos_ % ByteBits;

      uint8_t bit = (value >> i) & 1;
      if (bit)
      {
        buffer_[byteIndex] |= (1 << bitIndex);
      }
      else
      {
        buffer_[byteIndex] &= ~(1 << bitIndex);
      }

      ++bitPos_;
    }

    return true;
  }

  // ビット読み込み
  template <class NumType>
  bool readBits(NumType &value, size_t bits)
  {
    assert(bits <= sizeof(NumType) * ByteBits);
    if (bitPos_ + bits > bufferSize_)
    {
      return false;
    }

    value = 0;
    for (size_t i = 0; i < bits; ++i)
    {
      size_t byteIndex = bitPos_ / ByteBits;
      size_t bitIndex = bitPos_ % ByteBits;

      uint8_t bit = (buffer_[byteIndex] >> bitIndex) & 1;
      value |= (static_cast<NumType>(bit) << i);

      ++bitPos_;
    }
    if constexpr (std::is_signed_v<NumType>)
    {
      auto mask = (1 << (bits - 1));
      if (value & mask)
      {
        value &= ~mask;
        value = -value;
      }
    }

    return true;
  }

  //
  bool writeBool(bool value)
  {
    uint8_t valueByte = value ? 1 : 0;
    return writeBits(valueByte, 1);
  }

  //
  bool readBool(bool &value)
  {
    uint8_t resValue;
    if (readBits(resValue, 1))
    {
      value = resValue != 0;
      return true;
    }
    return false;
  }

  // バイト書き込み(要バイトアライン)
  bool writeByte(uint8_t value)
  {
    assert(bitPos_ % ByteBits == 0);
    if (bitPos_ + ByteBits > bufferSize_)
    {
      return false;
    }
    buffer_[bitPos_ / ByteBits] = value;
    bitPos_ += ByteBits;
    return true;
  }

  // バイト読み込み(要バイトアライン)
  bool readByte(uint8_t &value)
  {
    assert(bitPos_ % ByteBits == 0);
    if (bitPos_ + ByteBits > bufferSize_)
    {
      return false;
    }
    value = buffer_[bitPos_ / ByteBits];
    bitPos_ += ByteBits;
    return true;
  }

  // バイトにそろえる
  void alignByte()
  {
    bitPos_ = ((bitPos_ + ByteBits - 1) / ByteBits) * ByteBits;
  }

  // バイトにそろえて0で埋める
  void padToNext()
  {
    size_t rem = bitPos_ % ByteBits;
    if (rem != 0)
    {
      size_t padding = ByteBits - rem;
      writeBits<uint8_t>(0, padding);
    }
  }

  // 先頭戻し
  void reset() { bitPos_ = 0; }
  // ポインタ移動
  void seek(size_t pos) { bitPos_ = pos; }
  // 現在値取得
  size_t tell() const { return bitPos_; }

  // 先頭ポインタ取得
  [[nodiscard]] const uint8_t *data() const { return buffer_.data(); }

  // 保持データーサイズ(バイト数)
  [[nodiscard]] size_t size() const
  {
    return (bitPos_ + ByteBits - 1) / ByteBits;
  }
};

} // namespace record
