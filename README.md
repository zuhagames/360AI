# 360AI

Local GGUF text inference for a modded Xbox 360, with Gemma 2/3, dense Llama and Qwen2-family execution paths, experimental Xenos GPU matrix calculations, an OpenAI-compatible HTTP API, and MCP integration.

**360AI is a fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex).** The original Xbox 360 port is the foundation of this project. This fork expands beyond Gemma with Llama and Qwen2/2.5 support, GPU matrix calculations, and MCP integration. Credit for the original project belongs to Canyons and its contributors.

## Run on Xbox 360

For the complete ready-to-copy package, download **360AI-1.0-USB.zip** from the [1.0 release](https://github.com/zuhagames/360AI/releases/tag/v1.0). Extract it and copy the `360AI` folder to your USB drive. **Gemma 3 270M Instruct Q8_0 weights are included beside `360AI.xex`**, with their source/checksum, Gemma terms and notices. No separate model download is needed. The separate PC-Bridge ZIP is optional and runs on a PC.

1. Copy the extracted `360AI` folder, including its model GGUF, onto the console or USB drive. If using the standalone [360AI.xex](360AI.xex), put a small supported GGUF beside it.
2. Launch `360AI.xex` using your existing unsigned-XEX launcher.
3. Select the model and set the context, sampling, chat and server options.
4. Leave **GPU matrix math** enabled to try the GPU backend, or disable it to use the CPU. If the GPU startup numerical check fails, the menu reports it unavailable and inference uses the CPU.
5. Enable **OpenAI / MCP server** to access the console over your LAN. The chat screen displays its actual address and port (80, or 8080 if 80 cannot be bound).

A console capable of running unsigned XEX files is required. Models, the tokenizer, KV cache, UI and GPU buffers all share 512 MB of RAM. Architecture support does not guarantee a model will fit. Start with a small quantized model and a 256–512 token context; raise the context only when memory allows.

## Model compatibility

| GGUF architecture | Execution path | Tokenization and chat |
| --- | --- | --- |
| `gemma2`, `gemma3` | Gemma embedding scaling, GELU, post norms, existing local/global attention | SentencePiece score merges; Gemma turn markers |
| `llama` | Dense Llama, grouped-query attention, SiLU, consecutive-pair RoPE, tied or separate output weights | SentencePiece for Llama 2 style vocabularies; byte BPE for Llama 3; corresponding instruction/header markers |
| `qwen2` | Dense Qwen2 and Qwen2.5 GGUFs, Q/K/V biases, SiLU, split-half RoPE | Qwen2 byte BPE and ChatML |

The loader validates tensor shapes and rejects unknown architectures, MoE, unequal key/value dimensions, partial RoPE, and unsupported RoPE scaling. Shared Llama 3 frequency-factor tensors are supported. Unscaled models and Llama 3 files carrying their frequency factors are accepted; YaRN, linear scaling and LongRoPE are not implemented. Qwen3, Qwen VL, recurrent/hybrid models, vision and audio are not implemented.

Supported weight formats: F32, F16, Q4_0, Q4_1, Q5_0, Q5_1, Q8_0, Q8_1, Q2_K, Q3_K, Q4_K, Q5_K, Q6_K and Q8_K. These decoders now handle signed values, packed scales, and every block in a row. IQ, BF16 and other unlisted formats are rejected.

Chat formatting selects the built-in format from the vocabulary's control tokens. Arbitrary Jinja `tokenizer.chat_template` programs are not interpreted. Use a model with one of the formats in the table. BPE pre-tokenizers supported here are `qwen2`, `llama-bpe` and `llama3`.

## GPU calculations

The Xenos backend uses Direct3D 9 vertex/pixel shaders for matrix-vector products in attention projections, feed-forward layers and the output head. Each shader pass computes up to 64 output rows and 256 input columns. A fixed 1 MB CPU staging buffer keeps the model quantized in RAM.

This is **partial GPU offload**: CPU work still includes weight unpacking, tokenization, normalization, rotary transforms, attention scores/KV-cache operations, sampling and networking. Shader submissions share a lock with the UI and restore its rendering state.

Results are resolved into a tile-aligned, tiled R32F texture and untiled before CPU readback, as required by Xenos. GPU uploads remain linear. Pixel-center sampling keeps each output aligned with its weight row.

At startup, a GPU readback check compares 65 rows × 260 columns against CPU results, exercising both tile boundaries. Initialization or dispatch failure falls back to CPU. The setting defaults to GPU enabled when available. The settings screen and `360AI.log` report the exact failed operation or first mismatching row instead of a generic unavailable message.

This backend is experimental. Uploads, GPU/CPU synchronization and readbacks may make it slower than the CPU for some models. Shader compilation and Xbox executable packaging were verified locally; GPU execution, rendering behavior, numerical parity on real weights, and tokens/second still require a physical console. No GPU speedup is claimed.

## HTTP API

With the server enabled:

- `GET /v1/models` reports the loaded architecture.
- `POST /v1/chat/completions` accepts text `messages`, `max_tokens` (1–512), and `stream`.
- `POST /v1/completions` accepts `prompt` with the same limits and streaming option.
- `POST /mcp` exposes the MCP endpoint.
- `GET /health` (or `/`) checks connectivity and reports the compute backend and GPU diagnostic.

Sampling uses the temperature and top-k chosen on the console. Requests run sequentially and share the inference lock with local chat. This is a limited OpenAI-compatible text API; native OpenAI `tool_calls`, image inputs and per-request sampling overrides are not implemented.

The service is an unauthenticated LAN endpoint. The XEX declares the insecure-socket privilege, and the server configures plaintext LAN access on both listening and accepted Xbox sockets. Public browser origins are rejected; localhost and private-LAN OpenAI web clients can use CORS and OPTIONS preflight. MCP browser origins remain disabled. Do not expose the service to the public internet.

### Make a completion request

Turn **OpenAI / MCP server** on before loading the model. Set an OpenAI client's base URL to `http://CONSOLE-IP:PORT/v1`, using the address displayed on the console. Get model IDs from `GET /v1/models`.

For the console at `192.168.1.21:80`, test connectivity with:

```powershell
Invoke-RestMethod 'http://192.168.1.21:80/health' -TimeoutSec 10
```

Then send a **POST**, with a JSON body:

```powershell
Invoke-RestMethod -Uri 'http://192.168.1.21:80/v1/completions' `
  -Method Post -ContentType 'application/json' `
  -Body '{"model":"gemma3","prompt":"Say hello.","max_tokens":16,"stream":false}' `
  -TimeoutSec 300
```

Opening `/v1/completions` in a browser sends GET, which is not a generation request. The server now returns an explanatory 405 response. Use `/v1/chat/completions` with `messages` for the model's chat formatting. Allow a long client timeout for console inference.

If `/health` times out, inspect the `HTTP:` lines in `360AI.log` and the status displayed in chat. Socket permission failures now report their error codes; the console's homebrew runtime must permit LAN sockets. If GPU validation fails, its log includes the failing operation or actual/expected result.

## MCP on the console

Connect an MCP client to `http://CONSOLE-IP/mcp` (include `:8080` if displayed). The server implements the **2025-06-18** protocol over stateless Streamable HTTP with JSON responses:

- `initialize`, `notifications/initialized`, `ping`
- `tools/list`
- `tools/call`: `model_info` reports architecture, context and compute backend; `generate` runs inference with `prompt` and optional `max_tokens`.

GET streams and DELETE sessions return 405; this stateless endpoint does not issue a session ID. Notifications return 202 with no body. Resources, prompts, subscriptions, authentication and newer MCP protocol revisions are not implemented.

## Connect external MCP servers

The included companion bridge runs on a PC with **Node.js 22 or later**, with no npm dependencies. It connects to desktop **stdio** servers or **HTTP/HTTPS Streamable HTTP** servers, including servers that respond with SSE. It performs initialization, keeps session headers, follows tool-list pagination, and cleans up processes/sessions.

The Xbox continues to run the model. The PC handles external server processes, TLS and tool execution. This does not run stdio executables directly on the Xbox.

1. Copy `bridge/mcp.example.json` to `bridge/mcp.json`.
2. Set `xbox` to the console address.
3. Keep only the server entries you use and replace their paths/URLs. Every configured entry is connected when the bridge starts. `headers`, `env`, `cwd` and `timeoutMs` are supported.
4. Discover tools:

```powershell
node bridge/cli.mjs --config bridge/mcp.json --list
```

Call a tool explicitly:

```powershell
node bridge/cli.mjs --config bridge/mcp.json --call local/add --arguments '{"a":2,"b":3}'
```

Let Xbox inference choose among explicitly allowed tools:

```powershell
node bridge/cli.mjs --config bridge/mcp.json --prompt "What is 2 plus 3?" --allow-tools local/add
```

Replace `local/add` with names from `--list`. Multiple allowed tools are comma-separated. A tool absent from this list cannot be executed by model output. The loop permits four tool rounds, passes tool results back to Xbox inference, and prints the final answer. Small models may produce invalid tool JSON; the bridge does not promise reliable tool use. Configuration files containing tokens are ignored by Git.

## Build

Requirements: Windows, Visual Studio 2010 Professional, and the full Xbox 360 SDK at its default installation path.

```powershell
.\build.bat
```

This compiles and packages `360AI.xex` in the project root. Shader source is embedded and compiled by D3DX on the console; startup numerical validation guards the GPU path.

Fetch the pinned bundled model with `powershell -ExecutionPolicy Bypass -File tools/fetch-bundled-model.ps1`, then create and verify release archives with `powershell -ExecutionPolicy Bypass -File tools/package-release.ps1`. The model's source, size and hash are recorded in `release/model.json`; weights are stored under the ignored `build/models` directory. Output is written to `build/releases/v1.0`, including the USB ZIP with weights and required notices, optional PC bridge ZIP, executable and SHA-256 checksums. Packaging verifies the GGUF checksum both before and after ZIP creation.

## Verification

With Node.js and the Windows compiler installed:

```powershell
node tests/make-fixtures.mjs
.\tests\build-host.bat
node --test tests/*.test.mjs
```

Native tests cover all supported quantized block layouts, dense Llama/Qwen loading and generation, rotary/activation operations, stop/BOS handling, JSON parsing, and MCP dispatch. Protocol tests exercise stdio, JSON/SSE HTTP, sessions, pagination, external tool feedback, allowlists, and the native HTTP server. Tokenizer tests compare 520 inputs against a separate regex/BPE reference.

Synthetic fixtures validate execution paths, not output quality of full pretrained models. Console inference and GPU benchmarks remain hardware validation tasks.

The bundled Gemma 3 270M Q8_0 model has also been loaded and used to generate text with the host build. Reproduce this check after fetching the model with `build/tests/native.exe --model build/models/gemma-3-270m-it-Q8_0.gguf`.

Implementation references: [GGUF/GGML quantization layouts](https://github.com/ggml-org/llama.cpp/blob/master/ggml/src/ggml-common.h), [Llama execution](https://github.com/ggml-org/llama.cpp/blob/master/src/models/llama.cpp), [Qwen2 execution](https://github.com/ggml-org/llama.cpp/blob/master/src/models/qwen2.cpp), and [MCP 2025-06-18 transport](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports).
