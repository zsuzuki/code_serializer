#include "record.h"
#include "serialize.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
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

struct BenchResult
{
  std::string name;
  size_t payloadBytes;
  uint64_t totalNs;
};

constexpr size_t kItemCount = 256;
constexpr size_t kIterations = 2000;
constexpr size_t kBufferBytes = 1024 * 1024;

template <class Func>
uint64_t measureNs(Func func)
{
  const auto start = std::chrono::steady_clock::now();
  func();
  const auto end = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
      .count();
}

void setupSample(TestVer2 &v, size_t index, uint32_t seed)
{
  v.enabled_ = ((index + seed) % 2) == 0;
  v.count_ = 100 + static_cast<uint32_t>(index * 3 + seed);
  v.name_ = std::format("name_{}_{}", index, seed);
  v.age_ = static_cast<uint8_t>(18 + (index + seed) % 50);
  v.code_ = static_cast<int16_t>((index % 40) - 20);
  v.number_ = 1000 + static_cast<uint32_t>(index * 7 + seed * 11);
  v.bits_ = static_cast<uint32_t>((index * 17) ^ (seed * 13));
}

void prepareDataset(std::array<TestVer2, kItemCount> &base,
                    std::array<TestVer2, kItemCount> &next)
{
  for (size_t i = 0; i < kItemCount; ++i)
  {
    setupSample(base[i], i, 1);
    setupSample(next[i], i, 2);
  }
}

BenchResult runSerializeBench(const std::array<TestVer2, kItemCount> &src)
{
  record::Serializer ser{kBufferBytes};
  size_t payloadSize = 0;
  const auto total = measureNs(
      [&]()
      {
        for (size_t iter = 0; iter < kIterations; ++iter)
        {
          ser.reset();
          for (const auto &v : src)
          {
            assert(v.serialize(ser));
          }
          payloadSize = ser.size();
        }
      });
  return {"serialize", payloadSize, total};
}

BenchResult runDeserializeBench(const std::array<TestVer2, kItemCount> &src)
{
  record::Serializer payloadSer{kBufferBytes};
  for (const auto &v : src)
  {
    assert(v.serialize(payloadSer));
  }
  const size_t payloadSize = payloadSer.size();

  std::array<TestVer2, kItemCount> dst{};
  const auto total = measureNs(
      [&]()
      {
        for (size_t iter = 0; iter < kIterations; ++iter)
        {
          payloadSer.reset();
          for (auto &v : dst)
          {
            assert(v.deserialize(payloadSer));
          }
        }
      });
  return {"deserialize", payloadSize, total};
}

BenchResult runSerializeDiffBench(
    const std::array<TestVer2, kItemCount> &base,
    const std::array<TestVer2, kItemCount> &next)
{
  record::Serializer ser{kBufferBytes};
  size_t payloadSize = 0;
  const auto total = measureNs(
      [&]()
      {
        for (size_t iter = 0; iter < kIterations; ++iter)
        {
          ser.reset();
          for (size_t i = 0; i < kItemCount; ++i)
          {
            assert(base[i].serializeDiff(ser, next[i]));
          }
          payloadSize = ser.size();
        }
      });
  return {"serializeDiff", payloadSize, total};
}

BenchResult runDeserializeDiffBench(
    const std::array<TestVer2, kItemCount> &base,
    const std::array<TestVer2, kItemCount> &next)
{
  record::Serializer diffSer{kBufferBytes};
  for (size_t i = 0; i < kItemCount; ++i)
  {
    assert(base[i].serializeDiff(diffSer, next[i]));
  }
  const size_t payloadSize = diffSer.size();

  std::array<TestVer2, kItemCount> current{};
  for (size_t i = 0; i < kItemCount; ++i)
  {
    setupSample(current[i], i, 1);
  }

  const auto total = measureNs(
      [&]()
      {
        for (size_t iter = 0; iter < kIterations; ++iter)
        {
          for (size_t i = 0; i < kItemCount; ++i)
          {
            setupSample(current[i], i, 1);
          }
          diffSer.reset();
          for (auto &v : current)
          {
            assert(v.deserializeDiff(diffSer));
          }
        }
      });

  return {"deserializeDiff", payloadSize, total};
}

void printResult(const BenchResult &result)
{
  const auto ops = static_cast<double>(kIterations) * static_cast<double>(kItemCount);
  const auto nsPerItem = static_cast<double>(result.totalNs) / ops;
  const auto usPerIter =
      static_cast<double>(result.totalNs) / static_cast<double>(kIterations) /
      1000.0;

  std::cout << std::format(
                   "{:>15}  size={:>7} bytes  total={:>10} ns  {:>8.2f} ns/item"
                   "  {:>8.2f} us/iter\n",
                   result.name, result.payloadBytes, result.totalNs, nsPerItem,
                   usPerIter);
}

} // namespace

int main()
{
  std::array<TestVer2, kItemCount> base{};
  std::array<TestVer2, kItemCount> next{};
  prepareDataset(base, next);

  const auto ser = runSerializeBench(base);
  const auto serDiff = runSerializeDiffBench(base, next);
  const auto des = runDeserializeBench(base);
  const auto desDiff = runDeserializeDiffBench(base, next);

  std::cout << std::format("items={} iterations={}\n", kItemCount, kIterations);
  printResult(ser);
  printResult(serDiff);
  printResult(des);
  printResult(desDiff);

  const auto ratio = static_cast<double>(serDiff.payloadBytes) /
                     static_cast<double>(ser.payloadBytes);
  std::cout << std::format("diff/full size ratio: {:.3f}\n", ratio);
  return 0;
}
