//
// copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
//
#pragma once

#include <cassert>
#include <vector>

namespace record
{

//
//
//
class Serializer
{
  static constexpr size_t ByteBits = 8;
  static constexpr size_t WordBytes = sizeof(uint64_t);
  static constexpr size_t WordBits = WordBytes * ByteBits;

  std::vector<uint64_t> buffer_;
  size_t bufferSize_;
  size_t bitPos_;

public:
  Serializer(size_t n)
      : buffer_((n + WordBytes - 1) / WordBytes),
        bufferSize_(buffer_.size() * WordBits), bitPos_(0)
  {
  }

  // 書き込み終了(任意)
  void terminate(uint32_t mark) { writeBits(mark, sizeof(mark) * ByteBits); }

  // ビット書き込み 64bit専用
  bool writeBits64(uint64_t value, size_t bits)
  {
    assert(bits <= WordBits);
    if (bitPos_ + bits > bufferSize_)
    {
      return false;
    }

    auto bitIndex = bitPos_ % WordBits;
    auto *ptr = &buffer_[bitPos_ / WordBits];

    if (bitIndex + bits <= WordBits)
    {
      // single
      auto mask = ((1ULL << bits) - 1ULL);
      *ptr &= ~(mask << bitIndex);
      *ptr |= (value & mask) << bitIndex;
    }
    else
    {
      // double
      auto bits1 = WordBits - bitIndex;
      auto bits2 = bits - bits1;

      auto mask1 = ((1ULL << bits1) - 1ULL);
      auto mask2 = ((1ULL << bits2) - 1ULL);

      *ptr &= ~(mask1 << bitIndex);
      *ptr |= (value & mask1) << bitIndex;
      ptr++;
      *ptr &= ~mask2;
      *ptr |= (value >> bits1) & mask2;
    }

    bitPos_ += bits;
    return true;
  }

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
    return writeBits64(uint64_t(value), bits);
  }

  // ビット読み込み 64bit専用
  bool readBits64(uint64_t &value, size_t bits)
  {
    assert(bits <= WordBits);
    if (bitPos_ + bits > bufferSize_)
    {
      return false;
    }

    auto bitIndex = bitPos_ % WordBits;
    auto *ptr = &buffer_[bitPos_ / WordBits];

    if (bitIndex + bits <= WordBits)
    {
      // single
      auto mask = ((1ULL << bits) - 1ULL);
      value = (*ptr >> bitIndex) & mask;
    }
    else
    {
      // double
      auto bits1 = WordBits - bitIndex;
      auto bits2 = bits - bits1;

      auto mask1 = ((1ULL << bits1) - 1ULL);
      auto mask2 = ((1ULL << bits2) - 1ULL);

      value = (*ptr >> bitIndex) & mask1;
      ptr++;
      value |= (*ptr & mask2) << bits1;
    }

    bitPos_ += bits;
    return true;
  }

  // ビット読み込み
  template <class NumType>
  bool readBits(NumType &value, size_t bits)
  {
    uint64_t tempValue = 0;
    if (readBits64(tempValue, bits))
    {
      value = tempValue;
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
    return false;
  }

  //
  bool writeBool(bool value) { return writeBits<uint8_t>(value ? 1 : 0, 1); }

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
  bool writeByte(uint8_t value) { return writeBits64(value, ByteBits); }

  // バイト読み込み(要バイトアライン)
  bool readByte(uint8_t &value)
  {
    uint64_t readValue;
    if (readBits64(readValue, ByteBits))
    {
      value = readValue;
      return true;
    }
    return false;
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
  [[nodiscard]] const void *data() const { return buffer_.data(); }

  // 保持データーサイズ(バイト数)
  [[nodiscard]] size_t size() const
  {
    return (bitPos_ + ByteBits - 1) / ByteBits;
  }
};

} // namespace record
