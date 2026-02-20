# code_serializer

C++20 で書かれたバイナリシリアライズ実験用ライブラリ。

## 構成

- `include/`: 公開ヘッダ
- `src/record.cpp`: ライブラリ実装
- `examples/main.cpp`: サンプル実行コード
- `tests/main_test.cpp`: `examples/main.cpp` ベースの挙動テスト

## ビルド

```bash
cmake -S . -B build
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
