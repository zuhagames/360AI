# 360AI 1.1

Download **360AI-1.1-USB.zip** under Assets. Extract it, copy the entire `360AI` folder to your USB drive or HDD, and launch `360AI.xex` on your modded Xbox 360.

**Gemma 3 270M Instruct Q8_0 and SmolLM2 135M Instruct Q8_0 are included.** Select either model for offline chat; RAM loading is recommended for both. No separate weights, SDK or shader download is needed on the Xbox.

## What's new

- **SmolLM2 135M:** a smaller local chat model, with support for its tokenizer.
- **HDD/USB weight streaming:** keeps matrix weights on disk with an 8 MB cache, allowing models whose weights exceed available RAM to be loaded.
- **More local model choices:** optional Qwen2.5 0.5B, 1.5B and 7B downloads, pinned to exact revisions and verified with SHA-256. Interrupted downloads can resume.
- **Disk-space descriptions:** the model browser shows total model size and recommended free space. The model guide lists 1 GB for Qwen 0.5B, 2 GB for 1.5B and **5 GB for 7B**.
- **Split GGUF support and 64-bit file access:** standard model shards load together; missing or inconsistent shards are rejected.
- **Storage diagnostics:** disk read failures stop generation and appear in the console UI, `/health`, OpenAI-compatible endpoints and MCP responses.
- Default context reduced to **512 tokens** to leave more memory available.

## Using larger models

The ZIP includes `MODELS.txt` and `Download-Models.ps1`. Run the downloader on a Windows PC, then copy the downloaded GGUF files beside `360AI.xex`. Choose **HDD/USB stream** in Settings before loading a larger model. Keep all shards together and select the first shard.

HDD streaming pages model weights; the tokenizer, working buffers and KV cache still need console RAM. The 1.5B and 7B options are **experimental and have not been benchmarked on an Xbox 360**. Large models can be extremely slow. Start with a context of 256-512 tokens. The experimental GPU path is unchanged; this release makes no GPU speedup claim.

## Validation

Built with the Xbox 360 SDK. Native checks and all eight protocol/tokenizer/package test groups pass, including 780 tokenizer reference cases and HTTP generation in RAM and streaming modes. Real Gemma, SmolLM2 and Qwen2.5 0.5B models produce identical tokens and KV-cache values in host RAM and streaming tests. Cache-boundary reads, disk failures and split-model loading are covered. These checks do not replace physical-console testing.

The ZIP is the only uploaded release asset. It includes the executable, both models, guides, model catalog/downloader, credits and required model licenses/notices. Model files are pinned to the sources recorded in the ZIP; Gemma use remains subject to the included Gemma Terms of Use.

Made by **ZuhaGames & Canyons**. A fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma. AI tools assisted with development, debugging and documentation.
