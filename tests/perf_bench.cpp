#include "record.h"
#include "serialize.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <vector>

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

constexpr size_t kDefaultItemCount = 256;
constexpr size_t kDefaultIterations = 2000;
constexpr size_t kDefaultBufferBytes = 1024 * 1024;

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

void prepareDataset(std::vector<TestVer2> &base, std::vector<TestVer2> &next)
{
  assert(base.size() == next.size());
  for (size_t i = 0; i < base.size(); ++i)
  {
    setupSample(base[i], i, 1);
    setupSample(next[i], i, 2);
  }
}

BenchResult runSerializeBench(const std::vector<TestVer2> &src, size_t iterations,
                              size_t bufferBytes)
{
  record::Serializer ser{bufferBytes};
  size_t payloadSize = 0;
  const auto total = measureNs(
      [&]()
      {
        for (size_t iter = 0; iter < iterations; ++iter)
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

BenchResult runDeserializeBench(const std::vector<TestVer2> &src,
                                size_t iterations, size_t bufferBytes)
{
  record::Serializer payloadSer{bufferBytes};
  for (const auto &v : src)
  {
    assert(v.serialize(payloadSer));
  }
  const size_t payloadSize = payloadSer.size();

  std::vector<TestVer2> dst(src.size());
  const auto total = measureNs(
      [&]()
      {
        for (size_t iter = 0; iter < iterations; ++iter)
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

BenchResult runSerializeDiffBench(const std::vector<TestVer2> &base,
                                  const std::vector<TestVer2> &next,
                                  size_t iterations, size_t bufferBytes)
{
  assert(base.size() == next.size());
  record::Serializer ser{bufferBytes};
  size_t payloadSize = 0;
  const auto total = measureNs(
      [&]()
      {
        for (size_t iter = 0; iter < iterations; ++iter)
        {
          ser.reset();
          for (size_t i = 0; i < base.size(); ++i)
          {
            assert(base[i].serializeDiff(ser, next[i]));
          }
          payloadSize = ser.size();
        }
      });
  return {"serializeDiff", payloadSize, total};
}

BenchResult runDeserializeDiffBench(const std::vector<TestVer2> &base,
                                    const std::vector<TestVer2> &next,
                                    size_t iterations, size_t bufferBytes)
{
  assert(base.size() == next.size());
  record::Serializer diffSer{bufferBytes};
  for (size_t i = 0; i < base.size(); ++i)
  {
    assert(base[i].serializeDiff(diffSer, next[i]));
  }
  const size_t payloadSize = diffSer.size();

  std::vector<TestVer2> current(base.size());
  for (size_t i = 0; i < base.size(); ++i)
  {
    setupSample(current[i], i, 1);
  }

  const auto total = measureNs(
      [&]()
      {
        for (size_t iter = 0; iter < iterations; ++iter)
        {
          for (size_t i = 0; i < base.size(); ++i)
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

void printResult(const BenchResult &result, size_t itemCount, size_t iterations)
{
  const auto ops = static_cast<double>(iterations) * static_cast<double>(itemCount);
  const auto nsPerItem = static_cast<double>(result.totalNs) / ops;
  const auto usPerIter =
      static_cast<double>(result.totalNs) / static_cast<double>(iterations) /
      1000.0;

  std::cout << std::format(
                   "{:>15}  size={:>7} bytes  total={:>10} ns  {:>8.2f} ns/item"
                   "  {:>8.2f} us/iter\n",
                   result.name, result.payloadBytes, result.totalNs, nsPerItem,
                   usPerIter);
}

bool parseArg(const char *arg, size_t &out)
{
  char *end = nullptr;
  const auto val = std::strtoull(arg, &end, 10);
  if (end == arg || *end != '\0' || val == 0)
  {
    return false;
  }
  out = static_cast<size_t>(val);
  return true;
}

void printUsage(const char *progName)
{
  std::cout << "Usage: " << progName
            << " [items] [iterations] [buffer_bytes]\n";
  std::cout << std::format("  defaults: items={} iterations={} buffer_bytes={}\n",
                           kDefaultItemCount, kDefaultIterations,
                           kDefaultBufferBytes);
}

} // namespace

int main(int argc, char **argv)
{
  size_t itemCount = kDefaultItemCount;
  size_t iterations = kDefaultIterations;
  size_t bufferBytes = kDefaultBufferBytes;

  if (argc >= 2 &&
      (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help"))
  {
    printUsage(argv[0]);
    return 0;
  }
  if (argc >= 2 && !parseArg(argv[1], itemCount))
  {
    printUsage(argv[0]);
    return 1;
  }
  if (argc >= 3 && !parseArg(argv[2], iterations))
  {
    printUsage(argv[0]);
    return 1;
  }
  if (argc >= 4 && !parseArg(argv[3], bufferBytes))
  {
    printUsage(argv[0]);
    return 1;
  }

  std::vector<TestVer2> base(itemCount);
  std::vector<TestVer2> next(itemCount);
  prepareDataset(base, next);

  const auto ser = runSerializeBench(base, iterations, bufferBytes);
  const auto serDiff = runSerializeDiffBench(base, next, iterations, bufferBytes);
  const auto des = runDeserializeBench(base, iterations, bufferBytes);
  const auto desDiff =
      runDeserializeDiffBench(base, next, iterations, bufferBytes);
  const size_t rawStructBytes = sizeof(TestVer2) * itemCount;

  std::cout << std::format("items={} iterations={}\n", itemCount, iterations);
  std::cout << std::format("buffer_bytes={}\n", bufferBytes);
  std::cout << std::format("struct(TestVer2) size={} bytes\n", sizeof(TestVer2));
  std::cout << std::format("raw struct total size={} bytes\n", rawStructBytes);
  printResult(ser, itemCount, iterations);
  printResult(serDiff, itemCount, iterations);
  printResult(des, itemCount, iterations);
  printResult(desDiff, itemCount, iterations);

  const auto ratio = static_cast<double>(serDiff.payloadBytes) /
                     static_cast<double>(ser.payloadBytes);
  const auto fullVsRaw =
      static_cast<double>(ser.payloadBytes) / static_cast<double>(rawStructBytes);
  const auto diffVsRaw =
      static_cast<double>(serDiff.payloadBytes) / static_cast<double>(rawStructBytes);
  std::cout << std::format("diff/full size ratio: {:.3f}\n", ratio);
  std::cout << std::format("serialize/raw struct ratio: {:.3f}\n", fullVsRaw);
  std::cout << std::format("serializeDiff/raw struct ratio: {:.3f}\n", diffVsRaw);
  return 0;
}
