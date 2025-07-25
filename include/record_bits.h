//
// copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
//
#pragma once

#include "serialize.h"
#include <cstdint>

namespace record
{

// シリアライズで先頭に付加されるヘッダーデータのサイズ
static constexpr size_t BitFieldHeaderSize = (3 + 13) / 8;

//
// data: 書き込むデータのポインタ
// num: dataの要素数
//
template <class BitClass>
bool serializeBitField(Serializer &ser, BitClass *data, size_t num)
{
  static_assert(sizeof(BitClass) % 4 == 0, "need 4 byte align");
  static_assert(sizeof(BitClass) <= 32, "limit 32 bytes");

  if (!ser.writeBits(sizeof(BitClass) / 4 - 1, 3))
  {
    return false;
  }
  if (!ser.writeBits(num, 13))
  {
    return false;
  }

  bool ret = true;
  if constexpr (sizeof(BitClass) % 8 == 0)
  {
    // 8byte align
    constexpr size_t Size = sizeof(BitClass) / 8;
    union DataPtr
    {
      BitClass *data_;
      uint64_t *ptr_;
    };
    DataPtr dptr;
    dptr.data_ = data;
    for (size_t i = 0; i < num * Size; i++)
    {
      if (!ser.writeBits64(*dptr.ptr_, 64))
      {
        ret = false;
        break;
      }
      dptr.ptr_++;
    }
  }
  else
  {
    // 4byte align
    constexpr size_t Size = sizeof(BitClass) / 4;
    union DataPtr
    {
      BitClass *data_;
      uint32_t *ptr_;
    };
    DataPtr dptr;
    dptr.data_ = data;
    for (size_t i = 0; i < num * Size; i++)
    {
      if (!ser.writeBits(*dptr.ptr_, 32))
      {
        ret = false;
        break;
      }
      dptr.ptr_++;
    }
  }
  return ret;
}

//
// data: 読み込むデータのポインタ
// num: dataのキャパシティーを格納する/実際に読み込んだ要素数が返る
//
template <class BitClass>
bool deserializeBitField(Serializer &ser, BitClass *data, size_t &num)
{
  static_assert(sizeof(BitClass) % 4 == 0, "need 4 byte align");

  uint64_t readSize;
  if (!ser.readBits(readSize, 3))
  {
    return false;
  }
  uint64_t readNum;
  if (!ser.readBits(readNum, 13))
  {
    return false;
  }
  readSize += 1;
  if (num < readNum)
  {
    // シリアライズデータの方が要素数が多いので切り捨てる
    readNum = num;
  }
  else
  {
    // 読み込む要素数
    num = readNum;
  }

  size_t posOffset = 0;
  if (sizeof(BitClass) / 4 < readSize)
  {
    // シリアライズデータの方が構造体のサイズが大きいので調整
    posOffset = (readSize * 4 - sizeof(BitClass)) * 8;
    readSize = sizeof(BitClass) / 4;
  }

  bool ret = true;
  union DataPtr
  {
    BitClass *data_;
    uint32_t *ptr_;
  };
  DataPtr dptr;
  dptr.data_ = data;

  for (size_t i = 0; i < num; i++)
  {
    for (size_t p = 0; p < readSize; p++)
    {
      if (!ser.readBits(dptr.ptr_[p], 32))
      {
        ret = false;
        goto error_exit;
      }
    }
    ser.seek(ser.tell() + posOffset);
    dptr.data_++;
  }

error_exit:
  return ret;
}

} // namespace record
