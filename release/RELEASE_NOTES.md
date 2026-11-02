# 360AI 1.0

Download **360AI-1.0-USB.zip** under Assets for the ready-to-copy Xbox package. The automatic Source code archives are for development.

1. Extract the ZIP on your PC.
2. Copy the entire `360AI` folder to your USB drive.
3. Copy your model GGUF into that folder, beside `360AI.xex`. You can reuse your existing `gemma-3-270m-it-Q8_0.gguf`.
4. Connect the USB to your modded Xbox 360 and launch `360AI.xex` with your existing XEX launcher.

**Model weights are not bundled.** A console capable of running unsigned XEX files and a supported GGUF are required. No SDK, Visual Studio, Node.js or separate shader files are needed on the Xbox.

The USB ZIP includes the executable, start guide, changelog and credits. `360AI.xex` is also attached separately. `SHA256SUMS.txt` contains checksums for the executable and both ZIPs.

**360AI-1.0-PC-Bridge.zip is optional PC software**, for connecting external MCP servers to Xbox inference. It requires Node.js 22+ on the PC and does not go on the Xbox.

## Features and fixes

- Gemma 2/3, dense Llama and Qwen2/2.5 GGUF execution paths, subject to model compatibility and console RAM.
- Experimental Xenos pixel-shader matrix calculations with startup numerical validation and CPU fallback.
- Tiled R32F GPU resolve/readback and pixel-center sampling fixes, with detailed failure diagnostics.
- Corrected packed quantized-weight decoding.
- OpenAI-compatible completions/chat endpoints, streaming, and `/health`.
- Xbox LAN socket privilege/configuration and support for local browser clients.
- MCP endpoint and optional external-server PC bridge.

The Xbox SDK build and seven native/protocol/package test groups pass. **GPU correctness and speed still need physical-console validation.** The GPU path remains experimental; no speedup is claimed. More compatibility details are in the repository README.

360AI is a fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma. Credit for the original Xbox 360 port belongs to Canyons and the original contributors.
