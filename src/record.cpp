//
// copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
//
#include "record.h"
#include "serialize.h"
#include <cassert>
#include <cstdint>

#include <iostream>

namespace record
{

//
// 丸ごと保存
//
bool ValueLink::serialize(Serializer &ser) const
{
  auto begPos = ser.tell();
  for (const auto val : list_)
  {
    if (!val->serialize(ser))
    {
      // 失敗したのでポインタもどす
      ser.seek(begPos);
      return false;
    }
  }
  return true;
}

//
// 差分を保存
//
bool ValueLink::serializeDiff(Serializer &ser, const ValueLink &other) const
{
  if (list_.size() != other.list_.size())
  {
    return false;
  }

  auto begPos = ser.tell();
  auto beg0 = list_.begin();
  auto beg1 = other.list_.begin();
  for (; beg0 != list_.end(); beg0++, beg1++)
  {
    if (!(*beg0)->serializeDiff(ser, **beg1))
    {
      // 失敗したのでポインタもどす
      ser.seek(begPos);
      return false;
    }
  }
  return true;
}

//
// 丸ごと更新
//
bool ValueLink::deserialize(Serializer &ser)
{
  auto begPos = ser.tell();
  for (const auto val : list_)
  {
    auto prevPos = ser.tell();
    if (!val->deserialize(ser))
    {
      if (val->isSeparator())
      {
        // セパレータ(バージョン区切り)なら
        // 過去バージョン(=正常終了)
        ser.seek(prevPos);
        return true;
      }
      // 失敗したのでポインタもどす
      ser.seek(begPos);
      return false;
    }
  }
  return true;
}

//
// 差分を読んで更新
//
bool ValueLink::deserializeDiff(Serializer &ser)
{
  auto begPos = ser.tell();
  for (const auto val : list_)
  {
    auto prevPos = ser.tell();
    if (!val->deserializeDiff(ser))
    {
      if (val->isSeparator())
      {
        // セパレータ(バージョン区切り)なら
        // 過去バージョン(=正常終了)
        ser.seek(prevPos);
        return true;
      }
      // 失敗したのでポインタもどす
      ser.seek(begPos);
      return false;
    }
  }
  return true;
}

//
// クラス別シリアライザ
//
namespace
{
// BaseBit(BB)
constexpr uint32_t BBZero = 0x0;
constexpr uint32_t BBOne = 0x1;
constexpr uint32_t BBVersion = 0x2;
constexpr uint32_t BBOther = 0x3;

} // namespace

//
// バージョンセパレータ
//

//
bool ValueVersion::serialize(Serializer &ser) const
{
  return ser.writeBits(BBVersion, BaseBits);
}
//
bool ValueVersion::serializeDiff(Serializer &ser, const ValueInterface &) const
{
  // versionセパレータはother関係ない
  return serialize(ser);
}
//
bool ValueVersion::deserialize(Serializer &ser)
{
  uint32_t version;
  if (ser.readBits(version, BaseBits))
  {
    return version == BBVersion;
  }
  return false;
}
//
bool ValueVersion::deserializeDiff(Serializer &ser) { return deserialize(ser); }

//
// boolean
//

//
bool ValueBool::serialize(Serializer &ser) const
{
  return ser.writeBits(val_ ? 1 : 0, BaseBits);
}
//
bool ValueBool::serializeDiff(Serializer &ser, const ValueInterface &) const
{
  // booleanはother関係ない
  return serialize(ser);
}
//
bool ValueBool::deserialize(Serializer &ser)
{
  uint32_t value;
  if (ser.readBits(value, BaseBits))
  {
    if (value == 0)
    {
      val_ = false;
      return true;
    }
    else if (value == 1)
    {
      val_ = true;
      return true;
    }
  }
  return false;
}
//
bool ValueBool::deserializeDiff(Serializer &ser) { return deserialize(ser); }

//
// string
//

//
bool ValueString::serialize(Serializer &ser) const
{
  if (!ser.writeBits(BBOther, BaseBits))
  {
    // 型書き込み失敗
    return false;
  }
  auto len = val_.size();
  if (!ser.writeBits(len, SizeBits))
  {
    // バイト数書き込み失敗
    return false;
  }
  if (len == 0)
  {
    // 空なのでここで終わり
    return true;
  }

  // 文字列はバイト単位にそろえ直す
  ser.padToNext();
  for (auto byte : val_)
  {
    if (!ser.writeByte(byte))
    {
      return false;
    }
  }

  return true;
}
//
bool ValueString::serializeDiff(Serializer &ser,
                                const ValueInterface &other) const
{
  if (auto *oval = dynamic_cast<const ValueString *>(&other))
  {
    if (val_ == oval->val_)
    {
      // 同じなので差分無し(BaseBit<Zero>のみ出力)
      return ser.writeBits(BBZero, BaseBits);
    }
    else
    {
      // 違うのでそのまま出力
      return serialize(ser);
    }
  }
  return false;
}
//
bool ValueString::deserialize(Serializer &ser)
{
  uint32_t base;
  if (!ser.readBits(base, BaseBits))
  {
    return false;
  }
  if (base != BBOther)
  {
    // 型が違う
    return false;
  }
  size_t bytes;
  if (!ser.readBits(bytes, SizeBits))
  {
    return false;
  }
  val_.clear();
  if (bytes == 0)
  {
    // 空なので終了
    return true;
  }
  ser.alignByte();
  for (int i = 0; i < bytes; i++)
  {
    uint8_t value;
    if (!ser.readByte(value))
    {
      return false;
    }
    val_.push_back(value);
  }

  return true;
}
//
bool ValueString::deserializeDiff(Serializer &ser)
{
  uint32_t base;
  if (!ser.readBits(base, BaseBits))
  {
    return false;
  }
  if (base == BBZero)
  {
    // 変更なし
    return true;
  }
  if (base != BBOther)
  {
    // 型が違う
    return false;
  }

  size_t bytes;
  if (!ser.readBits(bytes, SizeBits))
  {
    return false;
  }
  val_.clear();
  if (bytes == 0)
  {
    // 空なので終了
    return true;
  }
  ser.alignByte();
  for (int i = 0; i < bytes; i++)
  {
    uint8_t value;
    if (!ser.readByte(value))
    {
      return false;
    }
    val_.push_back(value);
  }

  return true;
}

//
// number
//

namespace
{
// write
template <class NumType>
bool writeNumberImpl(Serializer &ser, NumType num, size_t bits)
{
  if (num == 0)
  {
    // 0は特殊
    return ser.writeBits(BBZero, ValueInterface::BaseBits);
  }
  if (!ser.writeBits(BBOther, ValueInterface::BaseBits))
  {
    return false;
  }
  if (!ser.writeBits(bits, ValueInterface::SizeBits))
  {
    return false;
  }
  return ser.writeBits(num, bits);
}
// read
template <class NumType>
bool readNumberImpl(Serializer &ser, NumType &num)
{
  uint64_t base;
  if (!ser.readBits(base, ValueInterface::BaseBits))
  {
    return false;
  }
  if (base == 0)
  {
    // 0は特殊
    num = 0;
    return true;
  }
  if (base != BBOther)
  {
    // 数値ではない
    return false;
  }

  uint64_t bits;
  if (!ser.readBits(bits, ValueInterface::SizeBits))
  {
    return false;
  }
  if (bits == 0)
  {
    // 配列だった
    return false;
  }

  return ser.readBits(num, bits);
}
// read array
template <class NumType>
bool readArrayNumber(Serializer &ser, NumType &num)
{
  uint64_t type;
  if (!ser.readBits(type, 2))
  {
    return false;
  }

  bool result = false;
  switch (type)
  {
  case 0x0: // 6bit
    result = ser.readBits(num, 6);
    break;
  case 0x1: // 14bit
    result = ser.readBits(num, 14);
    break;
  case 0x2: // 30bit
    result = ser.readBits(num, 30);
    break;
  case 0x3: // 62bit
    result = ser.readBits(num, 62);
    break;
  }
  return result;
}

} // namespace

//
bool ValueNumber::writeNumber(Serializer &ser, UIntType num, size_t bits)
{
  return writeNumberImpl(ser, num, bits);
}

//
bool ValueNumber::writeNumber(Serializer &ser, IntType num, size_t bits)
{
  return writeNumberImpl(ser, num, bits);
}

//
bool ValueNumber::readNumber(Serializer &ser, UIntType &num)
{
  return readNumberImpl(ser, num);
}

//
bool ValueNumber::readNumber(Serializer &ser, IntType &num)
{
  return readNumberImpl(ser, num);
}

// 配列の先頭情報書き込み
bool ValueNumber::writeArrayHeader(Serializer &ser, size_t num)
{
  if (!ser.writeBits(BBOther, ValueInterface::BaseBits))
  {
    return false;
  }
  if (!ser.writeBits(0, ValueInterface::SizeBits))
  {
    return false;
  }
  return ser.writeBits(num, ByteBits);
}

// 配列の先頭情報読み込み
bool ValueNumber::readArrayHeader(Serializer &ser, size_t &num)
{
  uint64_t base;
  if (!ser.readBits(base, ValueInterface::BaseBits))
  {
    return false;
  }
  if (base != BBOther)
  {
    return false;
  }
  if (!ser.readBits(base, ValueInterface::SizeBits))
  {
    return false;
  }
  if (base != 0)
  {
    return false;
  }
  return ser.readBits(num, ByteBits);
}

//
bool ValueNumber::writeArrayValue(Serializer &ser, UIntType num)
{
  if (num < (1 << 6))
  {
    // total 8bits
    if (ser.writeBits((uint8_t)0, 2))
    {
      return ser.writeBits(num, 6);
    }
    return false;
  }
  if (num < (1 << 14))
  {
    // total 16bits
    if (ser.writeBits((uint8_t)1, 2))
    {
      return ser.writeBits(num, 14);
    }
    return false;
  }
  if (num < (1 << 30))
  {
    // total 32bits
    if (ser.writeBits((uint8_t)2, 2))
    {
      return ser.writeBits(num, 30);
    }
    return false;
  }
  // 64bitsフルは使えない62bitsまで
  if (ser.writeBits((uint8_t)3, 2))
  {
    return ser.writeBits(num, 62);
  }
  return false;
}

//
bool ValueNumber::writeArrayValue(Serializer &ser, IntType num)
{
  auto anum = std::abs(num);

  if (anum < (1 << 5))
  {
    // total 8bits
    if (ser.writeBits((uint8_t)0, 2))
    {
      return ser.writeBits(num, 6);
    }
    return false;
  }
  if (anum < (1 << 13))
  {
    // total 16bits
    if (ser.writeBits((uint8_t)1, 2))
    {
      return ser.writeBits(num, 14);
    }
    return false;
  }
  if (anum < (1 << 29))
  {
    // total 32bits
    if (ser.writeBits((uint8_t)2, 2))
    {
      return ser.writeBits(num, 30);
    }
    return false;
  }
  if (ser.writeBits((uint8_t)3, 2))
  {
    // 64bitsフルは使えない61bitsまで
    return ser.writeBits(num, 62);
  }
  return false;
}

//
bool ValueNumber::readArrayValue(Serializer &ser, UIntType &num)
{
  return readArrayNumber(ser, num);
}

//
bool ValueNumber::readArrayValue(Serializer &ser, IntType &num)
{
  return readArrayNumber(ser, num);
}

} // namespace record
