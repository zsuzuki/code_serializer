#include "record.h"
#include "record_bits.h"
#include "serialize.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>

namespace
{

struct Test
{
  record::ValueLink valLink;

  using vuint8_t = record::Value<uint8_t>;
  using vuint32_t = record::Value<uint32_t>;
  using vint16_t = record::Value<int16_t>;
  using vstring = record::ValueString;
  using vbool = record::ValueBool;
  using vsep = record::ValueVersion;
  using varray32 = record::ValueArray<uint32_t, 16>;
  using vbits32 = record::ValueBits<uint32_t>;

  vbool enabled_{false, valLink};
  vuint32_t count_{1000, valLink};
  vstring name_{"Namae", valLink};
  vuint8_t age_{20, valLink};
  varray32 points_{0, valLink};
  vbits32 bits_{0, valLink};
  vint16_t code_{-2, valLink};

  bool serialize(record::Serializer &ser) const { return valLink.serialize(ser); }
  bool serializeDiff(record::Serializer &ser, const Test &other) const
  {
    return valLink.serializeDiff(ser, other.valLink);
  }
  bool serializeDiffAndCopy(record::Serializer &ser, const Test &other)
  {
    return valLink.serializeDiffAndCopy(ser, other.valLink);
  }
  bool equal(const Test &other) const { return valLink.equal(other.valLink); }
  bool deserialize(record::Serializer &ser) { return valLink.deserialize(ser); }
  bool deserializeDiff(record::Serializer &ser)
  {
    return valLink.deserializeDiff(ser);
  }
};

struct TestVer2 : public Test
{
  vsep ver_1{valLink};
  vuint32_t number_{100, valLink};
};

struct Bit1
{
  uint64_t enable_ : 1;
  uint64_t count_ : 20;
  uint64_t number_ : 10;
  uint64_t hour_ : 5;
  uint64_t min_ : 6;
  uint64_t sec_ : 6;
  uint64_t month_ : 4;
  uint64_t day_ : 5;
};

struct Bit2 : public Bit1
{
  uint64_t year_ : 12;
};

void test_bool_io()
{
  record::Serializer ser{8};
  assert(ser.writeBool(true));
  assert(ser.writeBool(false));
  assert(ser.writeBool(true));

  ser.reset();
  bool b1 = false;
  bool b2 = true;
  bool b3 = false;
  assert(ser.readBool(b1));
  assert(ser.readBool(b2));
  assert(ser.readBool(b3));
  assert(b1);
  assert(!b2);
  assert(b3);
}

void test_cross_version_serialize_deserialize()
{
  Test test;
  TestVer2 test2;
  test2.enabled_ = true;
  test2.name_ = "Watashi";
  test2.age_ = 25;
  test2.count_ = 100;
  test2.number_ = 1024 * 1000;
  test.bits_ = 0x02;
  test2.bits_.set(5, true);

  record::Serializer ser{10 * 1000};
  record::Serializer ser2{10 * 1000};
  assert(test.serialize(ser));
  assert(test2.serialize(ser2));
  ser.terminate(0xffffffff);
  ser2.terminate(0xffffffff);

  ser.reset();
  ser2.reset();
  test2.enabled_ = false;

  // Current behavior in src/main.cpp:
  // older type <- newer payload fails return value but updates leading fields.
  assert(!test.deserialize(ser2));
  assert(test2.deserialize(ser));

  assert(test.enabled_());
  assert(test.name_() == "Watashi");
  assert(test.age_() == 25);

  assert(!test2.enabled_());
  assert(test2.name_() == "Namae");
  assert(test2.age_() == 20);
  assert(test2.number_() == 1024U * 1000U);
}

void test_diff_roundtrip()
{
  Test base;
  Test diff;

  base.enabled_ = true;
  base.count_ = 222;
  base.name_ = "DiffTarget";
  base.age_ = 31;
  base.bits_ = 0x4;

  record::Serializer ser{10 * 1000};
  assert(diff.serializeDiff(ser, base));
  ser.reset();
  assert(diff.deserializeDiff(ser));

  assert(diff.enabled_());
  assert(diff.count_() == 222);
  assert(diff.name_() == "DiffTarget");
  assert(diff.age_() == 31);
  assert(diff.bits_() == 0x4U);
}

void test_diff_and_copy()
{
  Test prev;
  Test next;

  next.enabled_ = true;
  next.count_ = 321;
  next.name_ = "NextState";
  next.age_ = 44;
  next.bits_ = 0x12;

  record::Serializer ser{10 * 1000};
  assert(prev.serializeDiffAndCopy(ser, next));
  assert(prev.equal(next));

  record::Serializer ser2{10 * 1000};
  assert(prev.serializeDiff(ser2, next));
  Test applied;
  applied.valLink.copy(next.valLink);
  ser2.reset();
  assert(applied.deserializeDiff(ser2));
  assert(applied.equal(next));
}

void test_bitfield_size_migration()
{
  std::array<Bit1, 10> bittest1{};
  std::array<Bit2, 8> bittest2{};
  for (size_t i = 0; i < bittest1.size(); ++i)
  {
    auto &btt1 = bittest1[i];
    btt1.enable_ = i & 1U;
    btt1.count_ = 100 + i;
    btt1.number_ = 22;
    btt1.hour_ = 15;
    btt1.min_ = 41;
    btt1.sec_ = 5;
    btt1.month_ = i + 1;
    btt1.day_ = 13;
  }

  record::Serializer bser{100 * 100};
  assert(record::serializeBitField(bser, bittest1.data(), bittest1.size()));
  bser.reset();
  size_t brnum = bittest2.size();
  assert(record::deserializeBitField(bser, bittest2.data(), brnum));
  assert(brnum == bittest2.size());
  assert(bittest2[0].count_ == 100);
  assert(bittest2[0].month_ == 1);
  assert(bittest2[7].count_ == 107);
  assert(bittest2[7].month_ == 8);

  for (size_t i = 0; i < bittest2.size(); ++i)
  {
    bittest2[i].count_ = i + 1000;
    bittest2[i].hour_ = i * 2 + 1;
    bittest2[i].number_ = 16;
  }

  bser.reset();
  assert(record::serializeBitField(bser, bittest2.data(), bittest2.size()));
  bser.reset();
  brnum = bittest1.size();
  assert(record::deserializeBitField(bser, bittest1.data(), brnum));
  assert(brnum == bittest2.size());
  assert(bittest1[0].count_ == 1000);
  assert(bittest1[7].count_ == 1007);
  assert(bittest1[7].number_ == 16);
  assert(bittest1[8].count_ == 108);
  assert(bittest1[9].count_ == 109);
}

} // namespace

int main()
{
  test_bool_io();
  test_cross_version_serialize_deserialize();
  test_diff_roundtrip();
  test_diff_and_copy();
  test_bitfield_size_migration();
  return 0;
}
