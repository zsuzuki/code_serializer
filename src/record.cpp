//
// copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
//
#include "record.h"
#include "serialize.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace record
{

struct ValueLink::MasterLayout
{
  std::vector<size_t> offsets;
  std::vector<const void *> typeTags;
};

namespace
{

//
// データ先頭2bitの種別
//
// BaseBit(BB)
[[maybe_unused]] constexpr uint32_t BBZero = 0x0;    // 0/false/finish
[[maybe_unused]] constexpr uint32_t BBOne = 0x1;     // 1/true
[[maybe_unused]] constexpr uint32_t BBVersion = 0x2; // version separator
[[maybe_unused]] constexpr uint32_t BBOther = 0x3;   // data

} // namespace

void ValueLink::appendMasterMatchedValuesToList()
{
  if (!master_)
  {
    return;
  }

  const auto count =
      std::min(registeredCount_, static_cast<size_t>(master_->offsets.size()));
  auto base = reinterpret_cast<std::uintptr_t>(this);
  for (size_t i = 0; i < count; ++i)
  {
    list_.push_back(
        reinterpret_cast<ValueInterface *>(base + master_->offsets[i]));
  }
  registeredCount_ = list_.size();
}

void ValueLink::ensureMaster()
{
  if (masterToken_ == nullptr)
  {
    registeredCount_ = list_.size();
    return;
  }
  if (master_ && list_.empty())
  {
    return;
  }

  static std::mutex registryMutex;
  static std::unordered_map<const void *, std::weak_ptr<const MasterLayout>>
      registry;

  {
    std::lock_guard lock{registryMutex};
    if (!master_)
    {
      auto it = registry.find(masterToken_);
      if (it != registry.end())
      {
        master_ = it->second.lock();
      }
    }

    if (!master_ && !list_.empty())
    {
      auto layout = std::make_shared<MasterLayout>();
      layout->offsets.reserve(list_.size());
      layout->typeTags.reserve(list_.size());
      auto base = reinterpret_cast<std::uintptr_t>(this);
      for (const auto *val : list_)
      {
        auto addr = reinterpret_cast<std::uintptr_t>(val);
        layout->offsets.push_back(addr - base);
        layout->typeTags.push_back(val->typeTag());
      }

      auto it = registry.find(masterToken_);
      if (it != registry.end())
      {
        if (auto existing = it->second.lock())
        {
          master_ = existing;
        }
      }
      if (!master_)
      {
        master_ = layout;
        registry[masterToken_] = master_;
      }
    }
  }

  if (!master_)
  {
    registeredCount_ = list_.size();
    return;
  }

  if (list_.empty())
  {
    if (registeredCount_ == 0)
    {
      return;
    }
    return;
  }

  if (list_.size() != master_->offsets.size())
  {
    // 同じキーに別レイアウトが混ざったのでこの個体は通常モードに戻す
    master_.reset();
    registeredCount_ = list_.size();
    return;
  }

  auto base = reinterpret_cast<std::uintptr_t>(this);
  for (size_t i = 0; i < list_.size(); ++i)
  {
    auto addr = reinterpret_cast<std::uintptr_t>(list_.at(i));
    if (addr - base != master_->offsets[i] ||
        list_.at(i)->typeTag() != master_->typeTags[i])
    {
      master_.reset();
      registeredCount_ = list_.size();
      return;
    }
  }

  registeredCount_ = master_->offsets.size();
  list_.clear();
}

size_t ValueLink::valueCount() const
{
  return master_ ? registeredCount_ : list_.size();
}

ValueInterface *ValueLink::valueAt(size_t index) const
{
  if (!master_)
  {
    return list_[index];
  }
  auto base = reinterpret_cast<std::uintptr_t>(const_cast<ValueLink *>(this));
  return reinterpret_cast<ValueInterface *>(base + master_->offsets[index]);
}

void ValueLink::add(ValueInterface *val)
{
  if (masterToken_ == nullptr)
  {
    list_.push_back(val);
    registeredCount_ = list_.size();
    return;
  }

  ensureMaster();
  if (master_)
  {
    if (registeredCount_ < master_->offsets.size())
    {
      auto base = reinterpret_cast<std::uintptr_t>(this);
      auto addr = reinterpret_cast<std::uintptr_t>(val);
      if (addr - base == master_->offsets[registeredCount_] &&
          val->typeTag() == master_->typeTags[registeredCount_])
      {
        registeredCount_++;
        return;
      }
    }
    appendMasterMatchedValuesToList();
    master_.reset();
  }

  list_.push_back(val);
  registeredCount_ = list_.size();
}

uint32_t ValueLink::getDataVersion() const
{
  const_cast<ValueLink *>(this)->ensureMaster();

  uint32_t version = 0;
  const auto count = master_ ? registeredCount_ : list_.size();
  if (!master_)
  {
    for (size_t i = 0; i < count; ++i)
    {
      if (list_[i]->isSeparator())
      {
        version++;
      }
    }
    return version;
  }

  const auto base = reinterpret_cast<std::uintptr_t>(const_cast<ValueLink *>(this));
  for (size_t i = 0; i < count; ++i)
  {
    const auto *val =
        reinterpret_cast<const ValueInterface *>(base + master_->offsets[i]);
    if (val->isSeparator())
    {
      version++;
    }
  }
  return version;
}

bool ValueLink::equal(const ValueLink &other) const
{
  const_cast<ValueLink *>(this)->ensureMaster();
  auto &rhs = const_cast<ValueLink &>(other);
  rhs.ensureMaster();

  const auto lhsCount = master_ ? registeredCount_ : list_.size();
  const auto rhsCount = rhs.master_ ? rhs.registeredCount_ : rhs.list_.size();
  if (lhsCount != rhsCount)
  {
    return false;
  }

  const auto count = lhsCount;
  const auto base0 =
      reinterpret_cast<std::uintptr_t>(const_cast<ValueLink *>(this));
  const auto base1 = reinterpret_cast<std::uintptr_t>(&rhs);
  for (size_t i = 0; i < count; ++i)
  {
    const auto *lhs =
        master_ ? reinterpret_cast<const ValueInterface *>(base0 + master_->offsets[i])
                : list_[i];
    const auto *rhsVal = rhs.master_
                             ? reinterpret_cast<const ValueInterface *>(
                                   base1 + rhs.master_->offsets[i])
                             : rhs.list_[i];
    if (!lhs->equal(*rhsVal))
    {
      return false;
    }
  }
  return true;
}

void ValueLink::copy(const ValueLink &other)
{
  ensureMaster();
  auto &rhs = const_cast<ValueLink &>(other);
  rhs.ensureMaster();

  const auto lhsCount = master_ ? registeredCount_ : list_.size();
  const auto rhsCount = rhs.master_ ? rhs.registeredCount_ : rhs.list_.size();
  if (lhsCount != rhsCount)
  {
    return;
  }

  const auto count = lhsCount;
  const auto base0 = reinterpret_cast<std::uintptr_t>(this);
  const auto base1 = reinterpret_cast<std::uintptr_t>(&rhs);
  for (size_t i = 0; i < count; ++i)
  {
    auto *lhs =
        master_ ? reinterpret_cast<ValueInterface *>(base0 + master_->offsets[i])
                : list_[i];
    const auto *rhsVal =
        rhs.master_ ? reinterpret_cast<const ValueInterface *>(
                          base1 + rhs.master_->offsets[i])
                    : rhs.list_[i];
    lhs->copy(*rhsVal);
  }
}

size_t ValueLink::getTotalBitSize() const
{
  const_cast<ValueLink *>(this)->ensureMaster();

  size_t bitSize = ValueInterface::BaseBits;
  const auto count = master_ ? registeredCount_ : list_.size();
  if (!master_)
  {
    for (size_t i = 0; i < count; ++i)
    {
      const auto *val = list_[i];
      bitSize += ValueInterface::BaseBits;
      if (!val->isBool() && !val->isSeparator())
      {
        bitSize += ValueInterface::SizeBits;
        auto singleSize =
            (val->getByteSize() + ValueInterface::ArraySizeBits) *
            ValueInterface::ByteBits;
        auto numarray = val->getArraySize();
        bitSize += numarray * singleSize;
        if (numarray > 1)
        {
          bitSize += ValueInterface::ByteBits;
        }
        else
        {
          bitSize += ValueInterface::ByteBits - 1;
        }
      }
    }
    return bitSize;
  }

  const auto base = reinterpret_cast<std::uintptr_t>(const_cast<ValueLink *>(this));
  for (size_t i = 0; i < count; ++i)
  {
    const auto *val =
        reinterpret_cast<const ValueInterface *>(base + master_->offsets[i]);
    bitSize += ValueInterface::BaseBits;
    if (!val->isBool() && !val->isSeparator())
    {
      bitSize += ValueInterface::SizeBits;
      auto singleSize =
          (val->getByteSize() + ValueInterface::ArraySizeBits) *
          ValueInterface::ByteBits;
      auto numarray = val->getArraySize();
      bitSize += numarray * singleSize;
      if (numarray > 1)
      {
        bitSize += ValueInterface::ByteBits;
      }
      else
      {
        bitSize += ValueInterface::ByteBits - 1;
      }
    }
  }
  return bitSize;
}

size_t ValueLink::needTotalSize() const
{
  constexpr auto ByteBits = ValueInterface::ByteBits;
  return (getTotalBitSize() + ByteBits - 1) / ByteBits;
}

//
// 終端チェック
//
bool ValueLink::checkTerminate(Serializer &ser)
{
  uint32_t term = BBZero;
  bool ret = ser.readBits(term, ValueInterface::BaseBits);
  // ここでも未来バージョンのチェックしたほうが良いか？
  return ret ? term == BBZero : false;
}

//
// 丸ごと保存
//
bool ValueLink::serialize(Serializer &ser) const
{
  const_cast<ValueLink *>(this)->ensureMaster();

  auto begPos = ser.tell();
  const auto count = master_ ? registeredCount_ : list_.size();
  if (!master_)
  {
    for (size_t i = 0; i < count; ++i)
    {
      if (!list_[i]->serialize(ser))
      {
        ser.seek(begPos);
        return false;
      }
    }
    return ser.writeBits(BBZero, ValueInterface::BaseBits);
  }

  const auto base = reinterpret_cast<std::uintptr_t>(const_cast<ValueLink *>(this));
  for (size_t i = 0; i < count; ++i)
  {
    const auto *val =
        reinterpret_cast<const ValueInterface *>(base + master_->offsets[i]);
    if (!val->serialize(ser))
    {
      ser.seek(begPos);
      return false;
    }
  }
  return ser.writeBits(BBZero, ValueInterface::BaseBits); // terminate
}

//
// 差分を保存
//
bool ValueLink::serializeDiff(Serializer &ser, const ValueLink &other) const
{
  const_cast<ValueLink *>(this)->ensureMaster();
  auto &rhs = const_cast<ValueLink &>(other);
  rhs.ensureMaster();

  const auto lhsCount = master_ ? registeredCount_ : list_.size();
  const auto rhsCount = rhs.master_ ? rhs.registeredCount_ : rhs.list_.size();
  if (lhsCount != rhsCount)
  {
    return false;
  }

  auto begPos = ser.tell();
  const auto count = lhsCount;
  const auto base0 =
      reinterpret_cast<std::uintptr_t>(const_cast<ValueLink *>(this));
  const auto base1 = reinterpret_cast<std::uintptr_t>(&rhs);
  for (size_t i = 0; i < count; ++i)
  {
    const auto *lhs =
        master_ ? reinterpret_cast<const ValueInterface *>(base0 + master_->offsets[i])
                : list_[i];
    const auto *rhsVal = rhs.master_
                             ? reinterpret_cast<const ValueInterface *>(
                                   base1 + rhs.master_->offsets[i])
                             : rhs.list_[i];
    if (!lhs->serializeDiff(ser, *rhsVal))
    {
      ser.seek(begPos);
      return false;
    }
  }
  return ser.writeBits(BBZero, ValueInterface::BaseBits); // terminate
}

//
// 差分保存して成功時にコピー
//
bool ValueLink::serializeDiffAndCopy(Serializer &ser, const ValueLink &other)
{
  ensureMaster();
  auto &rhs = const_cast<ValueLink &>(other);
  rhs.ensureMaster();

  const auto lhsCount = master_ ? registeredCount_ : list_.size();
  const auto rhsCount = rhs.master_ ? rhs.registeredCount_ : rhs.list_.size();
  if (lhsCount != rhsCount)
  {
    return false;
  }

  auto begPos = ser.tell();
  const auto count = lhsCount;
  const auto base0 = reinterpret_cast<std::uintptr_t>(this);
  const auto base1 = reinterpret_cast<std::uintptr_t>(&rhs);
#if defined(RECORD_FAST_DIFF_COPY)
  for (size_t i = 0; i < count; ++i)
  {
    auto *lhs =
        master_ ? reinterpret_cast<ValueInterface *>(base0 + master_->offsets[i])
                : list_[i];
    const auto *rhsVal = rhs.master_
                             ? reinterpret_cast<const ValueInterface *>(
                                   base1 + rhs.master_->offsets[i])
                             : rhs.list_[i];
    if (!lhs->serializeDiffAndCopy(ser, *rhsVal))
    {
      ser.seek(begPos);
      return false;
    }
  }
  if (!ser.writeBits(BBZero, ValueInterface::BaseBits))
  {
    ser.seek(begPos);
    return false;
  }
#else
  for (size_t i = 0; i < count; ++i)
  {
    const auto *lhs =
        master_ ? reinterpret_cast<const ValueInterface *>(base0 + master_->offsets[i])
                : list_[i];
    const auto *rhsVal = rhs.master_
                             ? reinterpret_cast<const ValueInterface *>(
                                   base1 + rhs.master_->offsets[i])
                             : rhs.list_[i];
    if (!lhs->serializeDiff(ser, *rhsVal))
    {
      ser.seek(begPos);
      return false;
    }
  }
  if (!ser.writeBits(BBZero, ValueInterface::BaseBits))
  {
    ser.seek(begPos);
    return false;
  }

  // 出力成功後にのみ現在値を更新(atomic behavior)
  copy(other);
#endif
  return true;
}

//
// 丸ごと更新
//
bool ValueLink::deserialize(Serializer &ser)
{
  ensureMaster();

  auto begPos = ser.tell();
  const auto count = master_ ? registeredCount_ : list_.size();
  auto base = reinterpret_cast<std::uintptr_t>(this);
  for (size_t i = 0; i < count; ++i)
  {
    auto *val =
        master_ ? reinterpret_cast<ValueInterface *>(base + master_->offsets[i])
                : list_[i];
    auto prevPos = ser.tell();
    if (!val->deserialize(ser))
    {
      if (val->isSeparator())
      {
        // セパレータ(バージョン区切り)なら
        // 過去バージョン(=正常終了)
        ser.seek(prevPos);
        break;
      }
      // 失敗したのでポインタもどす
      ser.seek(begPos);
      return false;
    }
  }
  return checkTerminate(ser);
}

//
// 差分を読んで更新
//
bool ValueLink::deserializeDiff(Serializer &ser)
{
  ensureMaster();

  auto begPos = ser.tell();
  const auto count = master_ ? registeredCount_ : list_.size();
  auto base = reinterpret_cast<std::uintptr_t>(this);
  for (size_t i = 0; i < count; ++i)
  {
    auto *val =
        master_ ? reinterpret_cast<ValueInterface *>(base + master_->offsets[i])
                : list_[i];
    auto prevPos = ser.tell();
    if (!val->deserializeDiff(ser))
    {
      if (val->isSeparator())
      {
        // セパレータ(バージョン区切り)なら
        // 過去バージョン(=正常終了)
        ser.seek(prevPos);
        break;
      }
      // 失敗したのでポインタもどす
      ser.seek(begPos);
      return false;
    }
  }
  return checkTerminate(ser);
}

//
// クラス別シリアライザ
//

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
bool ValueBool::serializeDiff(Serializer &ser,
                              const ValueInterface &other) const
{
  // booleanはotherを出力
  return other.serialize(ser);
}
//
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
  if (const auto *oval = valueCast<ValueString>(other))
  {
    if (val_ == oval->val_)
    {
      // 同じなので差分無し(BaseBit<Zero>のみ出力)
      return ser.writeBits(BBZero, BaseBits);
    }
    // 違うのでそのまま出力
    return other.serialize(ser);
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
  for (size_t i = 0; i < bytes; i++)
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
  for (size_t i = 0; i < bytes; i++)
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
  if (!ser.writeBits(bits >> 1ULL, ValueInterface::SizeBits))
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
  return ser.readBits(num, bits << 1ULL);
}

namespace
{

// array bits
std::array<uint64_t, 8> arrayBitList = {2, 4, 8, 16, 24, 32, 48, 64};

} // namespace

// read array
template <class NumType>
bool readArrayNumber(Serializer &ser, NumType &num)
{
  uint64_t type;
  if (!ser.readBits(type, ValueInterface::ArraySizeBits))
  {
    return false;
  }

  return ser.readBits(num, arrayBitList[type]);
}

} // namespace

//
bool ValueNumber::writeNumber(Serializer &ser, UIntType num, size_t bits)
{
  auto halfBit = bits >> 1ULL;
  if (num < (1ULL << halfBit))
  {
    // 値が小さいならより少ないビット数にする
    auto quarterBit = halfBit >> 1ULL;
    bits = num < (1ULL << quarterBit) ? quarterBit : halfBit;
  }
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
  for (uint64_t idx = 0; idx < arrayBitList.size() - 1; idx++)
  {
    auto bits = arrayBitList[idx];
    if (num < (1ULL << bits))
    {
      if (!ser.writeBits(idx, ValueInterface::ArraySizeBits))
      {
        // failed
        return false;
      }
      // success
      return ser.writeBits(num, bits);
    }
  }
  // 64bits
  if (ser.writeBits(7, ValueInterface::ArraySizeBits))
  {
    return ser.writeBits(num, 64);
  }
  return false;
}

//
bool ValueNumber::writeArrayValue(Serializer &ser, IntType num)
{
  auto anum = std::abs(num);

  for (uint64_t idx = 0; idx < arrayBitList.size() - 1; idx++)
  {
    auto bits = arrayBitList[idx];
    if (anum < (1ULL << (bits - 1)))
    {
      if (!ser.writeBits(idx, ValueInterface::ArraySizeBits))
      {
        // failed
        return false;
      }
      // success
      return ser.writeBits(num, bits);
    }
  }
  // 64bits
  if (ser.writeBits(7, ValueInterface::ArraySizeBits))
  {
    return ser.writeBits(num, 64);
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
