//
// copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
//
#include "record.h"
#include "serialize.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#if __APPLE__
#include <dispatch/dispatch.h>
#endif

#include "record_bits.h"

namespace
{
constexpr uint32_t Sec = 1000;
constexpr uint32_t Min = Sec * 60;
constexpr uint32_t Hour = Min * 60;
constexpr uint32_t Day = Hour * 24;
constexpr uint32_t Week = Day * 7;

constexpr uint32_t MaxTime = Week * 2 - 1;

//
template <typename Func>
uint64_t MeasureTime(Func func)
{
  using namespace std::chrono;
  auto start = high_resolution_clock::now();
  func();
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(end - start);
  // auto duration = duration_cast<nanoseconds>(end - start);
  return duration.count();
}

//
template <typename... Args>
void Printf(std::format_string<Args...> fmt, Args &&...args)
{
  static std::mutex mtx{};

  std::lock_guard lock{mtx};
  std::cout << std::format(fmt, std::forward<Args>(args)...) << "\n";
}

//
//
//
struct Test
{
  record::ValueLink valLink;

  using vuint8_t = record::Value<uint8_t>;
  using vuint16_t = record::Value<uint16_t>;
  using vuint32_t = record::Value<uint32_t>;
  using vuint64_t = record::Value<uint64_t>;
  using vint8_t = record::Value<int8_t>;
  using vint16_t = record::Value<int16_t>;
  using vint32_t = record::Value<int32_t>;
  using vint64_t = record::Value<int64_t>;
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

  [[nodiscard]] uint32_t version() const { return valLink.getDataVersion(); }
  [[nodiscard]] size_t size() const { return valLink.needTotalSize(); }
  bool serialize(record::Serializer &ser) const
  {
    return valLink.serialize(ser);
  }
  bool serializeDiff(record::Serializer &ser, const Test &other) const
  {
    return valLink.serializeDiff(ser, other.valLink);
  }
  bool deserialize(record::Serializer &ser) { return valLink.deserialize(ser); }
  bool deserializeDiff(record::Serializer &ser)
  {
    return valLink.deserializeDiff(ser);
  }
};

//
struct TestVer2 : public Test
{
  vsep ver_1{valLink};
  vuint32_t number_{100, valLink};
};

//
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

} // namespace

int main(int argc, char **argv)
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

  Printf("Data Version 1={} 2={}", test.version(), test2.version());

  for (int i = 0; i < 8; i++)
  {
    Printf("  bits{}: {}, {}", i, test.bits_.get(i), test2.bits_.get(i));
  }

  record::Serializer ser{10 * 1000};
  record::Serializer ser2{10 * 1000};

  ser.writeBool(true);
  ser.writeBool(false);
  ser.writeBool(true);
  {
    bool r1, r2, r3;
    ser.reset();
    ser.readBool(r1);
    ser.readBool(r2);
    ser.readBool(r3);
    Printf("bool wr: {}, {}, {}", r1, r2, r3);
  }
  ser.reset();

  test.serialize(ser);
  test2.serialize(ser2);
  ser.terminate(0xffffffff);
  ser2.terminate(0xffffffff);

  Printf("Default Pack Size 1={}/(need={})", ser.size(), test.size());
  Printf("Default Pack Size 2={}/(need={})", ser2.size(), test2.size());

  ser.reset();
  ser2.reset();

  test2.enabled_ = false;
  Printf("test1 enabled: {}", test.enabled_());
  Printf("test2 enabled: {}", test2.enabled_());

  if (test.deserialize(ser2))
  {
    Printf("deserialize1 success");
  }
  else
  {
    Printf("deserialize1 failed");
  }
  if (test2.deserialize(ser))
  {
    Printf("deserialize2 success");
  }
  else
  {
    Printf("deserialize2 failed");
  }
  Printf("test1 enabled(unpacked): {}", test.enabled_());
  Printf("test2 enabled(unpacked): {}", test2.enabled_());

  Printf("Name 1={}(age={})/{}", test.name_(), test.age_(), test.code_());
  Printf("Name 2={}(age={})/{}", test2.name_(), test2.age_(), test2.code_());

  {
    Test diff;

    ser.reset();
    if (diff.serializeDiff(ser, test))
    {
      Printf("Default Diff Pack Size={}", ser.size());
    }
    else
    {
      Printf("Diff serialize failed.");
    }
    Printf("  Pre  Name D={}(age={})/{}/cnt:{}", diff.name_(), diff.age_(),
           diff.code_(), diff.count_());

    ser.reset();
    if (diff.deserializeDiff(ser))
    {
      Printf("  Post Name D={}(age={})/{}/cnt:{}", diff.name_(), diff.age_(),
             diff.code_(), diff.count_());
    }
  }

  //
  // bit field pack/unpack
  //
  std::array<Bit1, 10> bittest1{};
  std::array<Bit2, 8> bittest2{};

  for (int i = 0; i < bittest1.size(); i++)
  {
    auto &btt1 = bittest1[i];
    btt1.enable_ = i & 1;
    btt1.count_ = 100 + i;
    btt1.number_ = 22;
    btt1.hour_ = 15;
    btt1.min_ = 41;
    btt1.sec_ = 5;
    btt1.month_ = i + 1;
    btt1.day_ = 13;
  }
  for (int i = 0; i < bittest2.size(); i++)
  {
    auto &btt2 = bittest2[i];
    btt2.hour_ = i;
    btt2.year_ = 2020 + i;
  }
  record::Serializer bser{100 * 100};
  // より大きい構造体へのアンパック
  bser.reset();
  record::serializeBitField(bser, bittest1.data(), bittest1.size());
  bser.reset();
  size_t brnum = bittest2.size();
  record::deserializeBitField(bser, bittest2.data(), brnum);
  Printf("---: {}", brnum);
  for (int i = 0; i < bittest2.size(); i++)
  {
    auto &btt = bittest2[i];
    Printf("{}: e={}, h={}, m={}, y={}", (int)btt.count_, (int)btt.enable_,
           (int)btt.hour_, (int)btt.month_, (int)btt.year_);
    btt.count_ = i + 1000;
    btt.hour_ = i * 2 + 1;
    btt.number_ = 16;
  }
  // より小さい構造体へのアンパック
  bser.reset();
  record::serializeBitField(bser, bittest2.data(), bittest2.size());
  bser.reset();
  brnum = bittest1.size();
  record::deserializeBitField(bser, bittest1.data(), brnum);
  Printf("---: {}", brnum);
  for (const auto &btt : bittest1)
  {
    Printf("{}: e={}, h={}, m={}, num={}", (int)btt.count_, (int)btt.enable_,
           (int)btt.hour_, (int)btt.month_, (int)btt.number_);
  }

  //
  // option execute
  //
  if (argc < 2)
  {
    return 0;
  }

  std::string arg0 = argv[1];
  if (arg0 == "-bench")
  {
    //
    // bench mark
    //
    std::array<TestVer2, 100> testArray{};
    record::Serializer perfTest{1000 * 1000};
    auto count = MeasureTime(
        [&]()
        {
          for (int i = 0; i < 10000; i++)
          {
            perfTest.reset();
            for (const auto &tes : testArray)
            {
              if (!tes.serialize(perfTest))
              {
                Printf("buffer overflow");
                break;
              }
            }
          }
        });
    Printf("Perf(nano sec): {} size={}", count, perfTest.size());
#if __APPLE__
    auto jgroup = dispatch_group_create();
    auto jqueue = dispatch_queue_create("PerfTest", DISPATCH_QUEUE_CONCURRENT);
    count = MeasureTime(
        [&]()
        {
          for (int i = 0; i < 2000; i++)
          {
            perfTest.reset();
            for (const auto &tes : testArray)
            {
              dispatch_group_async(jgroup, jqueue, ^{
                record::Serializer local{1000};
                if (!tes.serialize(local))
                {
                  Printf("buffer overflow");
                }
              });
            }
          }
        });
    dispatch_group_wait(jgroup, DISPATCH_TIME_FOREVER);
    Printf("Perf(nano sec): {}", count);
#endif
  }

  return 0;
}
