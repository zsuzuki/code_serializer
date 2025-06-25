//
// copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
//
#include "record.h"
#include "serialize.h"
#include <cstdint>
#include <iostream>

namespace
{
constexpr uint32_t Sec = 1000;
constexpr uint32_t Min = Sec * 60;
constexpr uint32_t Hour = Min * 60;
constexpr uint32_t Day = Hour * 24;
constexpr uint32_t Week = Day * 7;

constexpr uint32_t MaxTime = Week * 2 - 1;

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

  vbool enabled_{false, valLink};
  vuint32_t count_{0, valLink};
  vstring name_{"Namae", valLink};
  vuint8_t age_{20, valLink};
  varray32 points_{0, valLink};

  [[nodiscard]] uint32_t version() const { return valLink.getDataVersion(); }
  [[nodiscard]] size_t size() const { return valLink.needTotalSize(); }
};

//
struct TestVer2 : public Test
{
  vsep ver_1{valLink};
  vuint32_t number_{100, valLink};
};

} // namespace

int main(int argc, char **argv)
{
  Test test;
  TestVer2 test2;

  test2.name_ = "Watashi";
  test2.age_ = 25;
  test2.count_ = 100;
  test2.number_ = 1024 * 1000;

  Printf("Data Version 1={} 2={}", test.version(), test2.version());

  record::Serializer ser{10 * 1000};
  record::Serializer ser2{10 * 1000};

  test.valLink.serialize(ser);
  test2.valLink.serialize(ser2);
  ser.terminate(0xffffffff);
  ser2.terminate(0xffffffff);

  Printf("Default Pack Size 1={}/(need={})", ser.size(), test.size());
  Printf("Default Pack Size 2={}/(need={})", ser2.size(), test2.size());

  ser.reset();
  ser2.reset();

  if (test.valLink.deserialize(ser2))
  {
    Printf("deserialize1 success");
  }
  else
  {
    Printf("deserialize1 failed");
  }
  if (test2.valLink.deserialize(ser))
  {
    Printf("deserialize2 success");
  }
  else
  {
    Printf("deserialize2 failed");
  }

  Printf("Name 1={}(age={})", test.name_(), test.age_());
  Printf("Name 2={}(age={})", test2.name_(), test2.age_());

  ser.reset();
  test.valLink.serializeDiff(ser, test.valLink);
  Printf("Default Diff Pack Size={}", ser.size());

  return 0;
}
