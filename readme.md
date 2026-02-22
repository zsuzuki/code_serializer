# code_serializer

C++20 で書かれたバイナリシリアライズ実験用ライブラリ。

## 構成

- `include/`: 公開ヘッダ
- `src/record.cpp`: ライブラリ実装
- `examples/main.cpp`: サンプル実行コード
- `tests/main_test.cpp`: `examples/main.cpp` ベースの挙動テスト

## ValueLink のマスター共有

`ValueLink` はオプションで「レイアウトキー」を受け取り、同じキーを持つ同一レイアウト構造体間で
メンバー順序のマスター情報を共有できます。

```cpp
struct MyRecord
{
  record::ValueLink link{"MyRecord"};
  record::Value<uint32_t> id_{0, link};
  record::ValueString name_{"", link};
};
```

同一プロセス内で最初に使われた個体がマスターを構築し、以降の個体はその情報を再利用します。
キーを指定しない場合は従来どおり個体ごとのリスト管理です。

## ビルド

```bash
cmake -S . -B build
cmake --build build
```

`serializeDiffAndCopy` はデフォルトで安全(atomic)実装です。高速(非atomic)版を使う場合:

```bash
cmake -S . -B build -DRECORD_FAST_DIFF_COPY=ON
cmake --build build
```

## 実行

```bash
./build/code_serializer_example
```

## テスト

```bash
ctest --test-dir build --output-on-failure
```

## 性能計測

`serialize / serializeDiff / deserialize / deserializeDiff` の速度・サイズをまとめて計測:

```bash
./build/code_serializer_perf
```

出力にはシリアライズサイズだけでなく、`TestVer2` のオンメモリサイズとの比率も含まれます。

任意で `items` `iterations` `buffer_bytes` を指定可能:

```bash
./build/code_serializer_perf 512 5000 2097152
```
