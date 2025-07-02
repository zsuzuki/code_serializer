//
// copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
//
#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <string>

namespace record
{

class Serializer;

//
//
//
class ValueInterface
{
public:
  //
  // Base Bits: 初期識別用ビット数
  // 00: <BOOL>false or <Number>0 or <差分>変化なし
  // 01: <BOOL>true or <Number>1
  // 10: version separate
  // 11: other type -> continue
  //
  static constexpr size_t BaseBits = 2;
  // SizeBits : other type 時バイト数
  // = 0 -> array(next byte = array size)
  // 単体で保存できるのはSizeBitsバイト(6なら63バイト)まで
  static constexpr size_t SizeBits = 6;
  //
  static constexpr size_t ByteBits = 8;

public:
  ValueInterface() = default;
  virtual ~ValueInterface() = default;

  // シリアライズ時に同じ値かどうかのチェック
  [[nodiscard]] virtual bool
  equal([[maybe_unused]] const ValueInterface &other) const
  {
    return false;
  }
  // シリアライズ
  virtual bool serialize(Serializer &) const = 0;
  virtual bool serializeDiff(Serializer &, const ValueInterface &) const = 0;
  virtual bool deserialize(Serializer &) = 0;
  virtual bool deserializeDiff(Serializer &) = 0;

  // コピー
  virtual void copy([[maybe_unused]] const ValueInterface &other) {}
  // 各型チェック
  [[nodiscard]] virtual bool isBool() const { return false; }
  [[nodiscard]] virtual bool isSeparator() const { return false; }
  [[nodiscard]] virtual size_t getByteSize() const { return 1; }
  [[nodiscard]] virtual size_t getArraySize() const { return 1; }
};

//
// 変数をつなげて管理
//
class ValueLink
{
  std::list<ValueInterface *> list_;

public:
  ValueLink() = default;

  // 追加
  void add(ValueInterface *val) { list_.push_back(val); }

  // データーバージョン
  [[nodiscard]] uint32_t getDataVersion() const
  {
    uint32_t version = 0;
    for (const auto *val : list_)
    {
      if (val->isSeparator())
      {
        version++;
      }
    }
    return version;
  }

  // 比較
  [[nodiscard]] bool equal(const ValueLink &other) const
  {
    if (list_.size() != other.list_.size())
    {
      return false;
    }

    auto beg0 = list_.begin();
    auto beg1 = other.list_.begin();
    bool result = true;
    for (; beg0 != list_.end(); beg0++, beg1++)
    {
      if (!(*beg0)->equal(**beg1))
      {
        result = false;
        break;
      }
    }
    return result;
  }

  // コピー
  void copy(const ValueLink &other)
  {
    if (list_.size() != other.list_.size())
    {
      return;
    }

    auto beg0 = list_.begin();
    auto beg1 = other.list_.begin();
    for (; beg0 != list_.end(); beg0++, beg1++)
    {
      (*beg0)->copy(**beg1);
    }
  }

  // シリアライズ
  bool serialize(Serializer &ser) const;
  bool serializeDiff(Serializer &ser, const ValueLink &other) const;
  bool deserialize(Serializer &ser);
  bool deserializeDiff(Serializer &ser);

  // 必要ビットサイズ
  [[nodiscard]] size_t getTotalBitSize() const
  {
    size_t bitSize = 0;
    for (const auto &val : list_)
    {
      bitSize += ValueInterface::BaseBits;
      if (!val->isBool() && !val->isSeparator())
      {
        bitSize += ValueInterface::SizeBits;
        auto singleSize = val->getByteSize() * ValueInterface::ByteBits;
        auto numarray = val->getArraySize();
        bitSize += numarray * singleSize;
        if (numarray > 1)
        {
          // 情報領域分追加(array size)
          bitSize += ValueInterface::ByteBits;
        }
        else
        {
          // パディング分(最大7bit)が挟まる可能性がある
          bitSize += ValueInterface::ByteBits - 1;
        }
      }
    }
    return bitSize;
  }
  // 必要バイトサイズ
  [[nodiscard]] size_t needTotalSize() const
  {
    constexpr auto ByteBits = ValueInterface::ByteBits;
    return (getTotalBitSize() + ByteBits - 1) / ByteBits;
  }
};

//
// version separator
//
class ValueVersion : public ValueInterface
{
public:
  ValueVersion(ValueLink &link) { link.add(this); }
  ~ValueVersion() override = default;

  //
  bool serialize(Serializer &ser) const override;
  bool serializeDiff(Serializer &ser, const ValueInterface &) const override;
  bool deserialize(Serializer &ser) override;
  bool deserializeDiff(Serializer &ser) override;

  //
  [[nodiscard]] bool isSeparator() const override { return true; }
  [[nodiscard]] size_t getByteSize() const override { return 0; }
  [[nodiscard]] size_t getArraySize() const override { return 0; }
};

//
// boolean
//
class ValueBool : public ValueInterface
{
  bool val_;

public:
  ValueBool(bool init, ValueLink &link) : val_(init) { link.add(this); }
  ~ValueBool() override = default;

  //
  [[nodiscard]] bool equal(const ValueInterface &other) const override
  {
    if (auto *oval = dynamic_cast<const ValueBool *>(&other))
    {
      return val_ == oval->val_;
    }
    return false;
  }
  void copy(const ValueInterface &other) override
  {
    if (auto *oval = dynamic_cast<const ValueBool *>(&other))
    {
      val_ = oval->val_;
    }
  }
  [[nodiscard]] bool isBool() const override { return true; }
  [[nodiscard]] size_t getByteSize() const override { return 0; }
  [[nodiscard]] size_t getArraySize() const override { return 0; }

  //
  bool serialize(Serializer &ser) const override;
  bool serializeDiff(Serializer &ser, const ValueInterface &) const override;
  bool deserialize(Serializer &ser) override;
  bool deserializeDiff(Serializer &ser) override;

  //
  explicit operator bool() const { return val_; }
  bool operator()() const { return val_; }
  bool operator!() { return !val_; }
  bool operator=(bool val)
  {
    val_ = val;
    return val;
  }
  bool operator==(bool val) { return val_ == val; }
  bool operator!=(bool val) { return val_ != val; }
};

//
// string
//
class ValueString : public ValueInterface
{
  std::string val_;

public:
  ValueString(std::string init, ValueLink &link) : val_(std::move(init))
  {
    link.add(this);
  }
  ~ValueString() override = default;

  //
  [[nodiscard]] bool equal(const ValueInterface &other) const override
  {
    if (auto *oval = dynamic_cast<const ValueString *>(&other))
    {
      return val_ == oval->val_;
    }
    return false;
  }
  void copy(const ValueInterface &other) override
  {
    if (auto *oval = dynamic_cast<const ValueString *>(&other))
    {
      val_ = oval->val_;
    }
  }
  [[nodiscard]] size_t getByteSize() const override { return val_.size(); }

  //
  bool serialize(Serializer &ser) const override;
  bool serializeDiff(Serializer &ser,
                     const ValueInterface &other) const override;
  bool deserialize(Serializer &ser) override;
  bool deserializeDiff(Serializer &ser) override;

  //
  explicit operator std::string() const { return val_; }
  const std::string &operator()() const { return val_; }
  bool operator!() { return val_.empty(); }
  ValueString &operator=(const ValueString &other)
  {
    val_ = other.val_;
    return *this;
  }
  ValueString &operator=(const std::string &value)
  {
    val_ = value;
    return *this;
  }
  bool operator==(const ValueString &other) { return val_ == other.val_; }
  bool operator!=(const ValueString &other) { return !(*this == other); }
};

//
// number base
//
class ValueNumber : public ValueInterface
{
public:
  ~ValueNumber() override = default;

protected:
  using IntType = int64_t;
  using UIntType = uint64_t;

  static bool writeNumber(Serializer &ser, UIntType num, size_t bits);
  static bool writeNumber(Serializer &ser, IntType num, size_t bits);
  static bool readNumber(Serializer &ser, UIntType &num);
  static bool readNumber(Serializer &ser, IntType &num);

  static bool writeArrayHeader(Serializer &ser, size_t num);
  static bool readArrayHeader(Serializer &ser, size_t &num);
  static bool writeArrayValue(Serializer &ser, UIntType num);
  static bool writeArrayValue(Serializer &ser, IntType num);
  static bool readArrayValue(Serializer &ser, UIntType &num);
  static bool readArrayValue(Serializer &ser, IntType &num);
};

//
// number type
//
template <class NType>
class Value : public ValueNumber
{
protected:
  NType num_;

public:
  Value(NType num, ValueLink &link) : num_(num) { link.add(this); }
  ~Value() override = default;

  //
  [[nodiscard]] bool equal(const ValueInterface &other) const override
  {
    if (auto *oval = dynamic_cast<const Value<NType> *>(&other))
    {
      return num_ == oval->num_;
    }
    return false;
  }
  void copy(const ValueInterface &other) override
  {
    if (auto *oval = dynamic_cast<const Value<NType> *>(&other))
    {
      num_ = oval->num_;
    }
  }
  [[nodiscard]] size_t getByteSize() const override { return sizeof(NType); }

  //
  bool serialize(Serializer &ser) const override
  {
    if constexpr (std::is_signed_v<NType>)
    {
      IntType val = num_;
      return writeNumber(ser, val, sizeof(NType) * ByteBits);
    }
    else
    {
      UIntType val = num_;
      return writeNumber(ser, val, sizeof(NType) * ByteBits);
    }
  }
  bool serializeDiff(Serializer &ser,
                     const ValueInterface &other) const override
  {
    if (auto *oval = dynamic_cast<const Value<NType> *>(&other))
    {
      if constexpr (std::is_signed_v<NType>)
      {
        IntType diff = oval->num_ - num_;
        return writeNumber(ser, diff, sizeof(NType) * ByteBits);
      }
      else
      {
        UIntType diff = oval->num_ - num_;
        return writeNumber(ser, diff, sizeof(NType) * ByteBits);
      }
    }
    return false;
  }
  bool deserialize(Serializer &ser) override
  {
    if constexpr (std::is_signed_v<NType>)
    {
      IntType val;
      if (readNumber(ser, val))
      {
        num_ = val;
        return true;
      }
    }
    else
    {
      UIntType val;
      if (readNumber(ser, val))
      {
        num_ = val;
        return true;
      }
    }
    return false;
  }
  bool deserializeDiff(Serializer &ser) override
  {
    if constexpr (std::is_signed_v<NType>)
    {
      IntType diff;
      if (readNumber(ser, diff))
      {
        num_ += diff;
        return true;
      }
    }
    else
    {
      UIntType diff;
      if (readNumber(ser, diff))
      {
        num_ += diff;
        return true;
      }
    }
    return false;
  }

  //
  explicit operator NType() const { return num_; }
  NType operator()() const { return num_; }
  Value &operator=(const Value<NType> &other)
  {
    num_ = other.num_;
    return *this;
  }
  Value &operator=(NType num)
  {
    num_ = num;
    return *this;
  }
  bool operator==(const Value<NType> &other) { return num_ == other.num_; }
  bool operator==(NType num) { return num_ == num; }
  bool operator!=(const Value<NType> &other) { return !(this == other); }
  bool operator!=(NType num) { return num_ != num; }

  NType diff(const Value<NType> &other) { return num_ - other.num_; }
};

//
// bitset
//
template <class NType>
class ValueBits : public Value<NType>
{
public:
  ValueBits(NType init, ValueLink &link) : Value<NType>(init, link) {}
  ~ValueBits() override = default;

  //
  ValueBits &operator=(const ValueBits<NType> &other)
  {
    Value<NType>::num_ = other.num_;
    return *this;
  }
  ValueBits &operator=(NType num)
  {
    Value<NType>::num_ = num;
    return *this;
  }

  //
  // bit control
  //
  void set(NType bit, bool flag)
  {
    if (sizeof(NType) * 8 <= bit)
    {
      return;
    }

    auto bitMask = 1 << bit;
    if (flag)
    {
      Value<NType>::num_ |= bitMask;
    }
    else
    {
      Value<NType>::num_ &= ~bitMask;
    }
  }
  [[nodiscard]] bool get(NType bit) const
  {
    if (sizeof(NType) * 8 <= bit)
    {
      return false;
    }
    auto bitMask = 1 << bit;
    return (Value<NType>::num_ & bitMask) != 0;
  }
};

//
// array numbers
//
template <class NType, size_t Size>
class ValueArray : public ValueNumber
{
  std::array<NType, Size> array_;

public:
  ValueArray(NType init, ValueLink &link) : array_{init} { link.add(this); }
  ~ValueArray() override = default;

  //
  void fill(NType num) { array_.fill(num); }

  //
  [[nodiscard]] bool equal(const ValueInterface &other) const override
  {
    if (auto *oval = dynamic_cast<const ValueArray<NType, Size> *>(&other))
    {
      for (size_t i = 0; i < Size; i++)
      {
        if (at(i) != oval->at(i))
        {
          return false;
        }
      }
      return true;
    }
    return false;
  }
  void copy(const ValueInterface &other) override
  {
    if (auto *oval = dynamic_cast<const ValueArray<NType, Size> *>(&other))
    {
      array_ = oval->array_;
    }
  }
  [[nodiscard]] size_t getByteSize() const override { return sizeof(NType); }
  [[nodiscard]] size_t getArraySize() const override { return Size; }

  //
  bool serialize(Serializer &ser) const override
  {
    if (!writeArrayHeader(ser, Size))
    {
      return false;
    }
    for (auto item : array_)
    {
      if constexpr (std::is_signed_v<NType>)
      {
        IntType value = item;
        if (!writeArrayValue(ser, value))
        {
          return false;
        }
      }
      else
      {
        UIntType value = item;
        if (!writeArrayValue(ser, value))
        {
          return false;
        }
      }
    }
    return true;
  }
  bool serializeDiff(Serializer &ser,
                     const ValueInterface &other) const override
  {
    if (auto *oval = dynamic_cast<const ValueArray<NType, Size> *>(&other))
    {
      if (!writeArrayHeader(ser, Size))
      {
        return false;
      }
      for (size_t i = 0; i < Size; i++)
      {
        if constexpr (std::is_signed_v<NType>)
        {
          IntType diff = oval->at(i) - at(i);
          if (!writeArrayValue(ser, diff))
          {
            return false;
          }
        }
        else
        {
          UIntType diff = oval->at(i) - at(i);
          if (!writeArrayValue(ser, diff))
          {
            return false;
          }
        }
      }
      return true;
    }
    return false;
  }
  bool deserialize(Serializer &ser) override
  {
    size_t nbData;
    if (!readArrayHeader(ser, nbData))
    {
      return false;
    }
    if (nbData != Size)
    {
      return false;
    }
    for (auto &item : array_)
    {
      if constexpr (std::is_signed_v<NType>)
      {
        IntType val;
        if (!readArrayValue(ser, val))
        {
          return false;
        }
        item = val;
      }
      else
      {
        UIntType val;
        if (!readArrayValue(ser, val))
        {
          return false;
        }
        item = val;
      }
    }
    return true;
  }
  bool deserializeDiff(Serializer &ser) override
  {
    size_t nbData;
    if (!readArrayHeader(ser, nbData))
    {
      return false;
    }
    if (nbData != Size)
    {
      return false;
    }
    for (auto &item : array_)
    {
      if constexpr (std::is_signed_v<NType>)
      {
        IntType val;
        if (!readArrayValue(ser, val))
        {
          return false;
        }
        item += val;
      }
      else
      {
        UIntType val;
        if (!readArrayValue(ser, val))
        {
          return false;
        }
        item += val;
      }
    }
    return true;
  }

  //
  static constexpr size_t size() { return Size; }
  //
  NType at(size_t index) const { return array_.at(index); }
  void set(size_t index, NType num = 0) { array_.at(index) = num; }
};

} // namespace record
