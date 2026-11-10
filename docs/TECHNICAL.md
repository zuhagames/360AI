# Technical guide

[Back to 360AI](../README.md)

Build and command examples below run from the repository root.

## Run on Xbox 360

After launching the app:

1. Home opens Assistant, Notes, Timers or Models. Assistant uses predefined replies and tools without loading a model. For GGUF chat, open Models and set context, sampling, chat and server options before choosing **Load model**.
2. Leave **Compute backend: Auto** and **Weight storage: Auto** selected. Auto compares a small real-weight matrix on CPU/GPU and uses the GPU only if its result matches and it is at least 20% faster. Storage Auto keeps the model in RAM when weights, scratch/KV buffers and a memory reserve fit. CPU, forced GPU and explicit RAM/streaming overrides remain available.
3. Enable **OpenAI / MCP server** before loading the model to access the console over your LAN. The chat screen displays its actual address and port (80, or 8080 if 80 cannot be bound).

A console capable of running unsigned XEX files is required. New installs default to a 256-token context and 48-token reply cap; saved preferences are preserved. RAM mode keeps all weights in memory; HDD/USB streaming keeps matrix weights on disk with an 8 MB cache. The tokenizer, scratch buffers, small norm/bias tensors, KV cache, UI and GPU buffers still share the console's 512 MB RAM. Start at context 256–512 and raise it only when memory allows. The model's own context limit still applies.

## 360 Assistant and native UI (2.0)

Select **Assistant on Home**, or press Home X to open input directly. Home Y opens Quick tools. The assistant works with no GGUF files. Its 106 predefined phrase variants use an exact normalized index built once; `hello` gets a fixed reply immediately. It also supports saved text answers and local tools. Type `help` for commands. This is text input/output; speech recognition and spoken replies are not implemented.

The native UI uses a 256x512 ARGB atlas (512 KiB) with the existing bitmap font and Xelu's CC0 Xbox 360 button prompts. Draws batch up to 512 quads, preserving draw order. The decoded CPU atlas is released after upload on Xbox. Chat layout updates on transcript/generation changes and wraps words. Dedicated Notes and Timers screens operate on exact IDs, with confirmation before deletion/cancellation. Guide-keyboard cancellation consumes its input event. See [icon provenance](../assets/input/SOURCE.txt).

Assistant persistence runs on one worker with a single bounded latest-snapshot queue. The UI thread owns Assistant; the worker owns file writes. A completion clears dirty state only when its snapshot matches current data. Save failures retain data in memory and surface Not saved; Saved confirms success. Atomic replacement, validated backups and the 64 KiB bound remain. Wait for Saved before closing after an edit. Native tests cover coalescing, stale acknowledgements, failure handling and controller navigation. Host preview mode renders the same atlas/layout to BMP using `build/tests/native.exe --ui-preview home build/ui/home.bmp`; it does not validate Xbox GPU output or frame rate.

The assistant supports bounded expression parsing with precedence, parentheses, powers, `ans`, `sqrt`, `abs` and `round`; English numbers through thousands; unit conversion with follow-up values; eight session timers with named pause/resume/restart/extend controls; 32 searchable/editable notes; a saved name and up to 16 taught text answers. App status/help, model presets, export and navigation remain available. Note operations require exact IDs; duplicate timer labels ask for clarification. Clearing all notes/chat and cancelling all timers require a yes/no follow-up. Negation and spelling suggestions do not execute commands. Empty/oversized requests cancel waiting confirmations. Unknown questions get an explicit fallback. It processes one supported command per message.

Timers use the monotonic app clock, appear in a ten-second banner on app screens, and retain the last eight completed alerts in session history. They require this app to remain running and are not persisted or scheduled as system alarms. Paused timers retain their remaining time; expired timers cannot be restarted. Notes/name/taught answers use validated JSON, temporary-file replacement and a previous valid backup in `360AI-assistant.json(.bak)`. Version 2 migrates old notes and stores a high-water counter so deleted/undone note IDs are not reused. Invalid controls/NUL and duplicate IDs/answer phrases are rejected transactionally. `undo` restores one saved-data change within the session; it does not reverse timers or app actions. A save failure leaves in-memory data available and reports the failure. Assistant scrollback is bounded; new chat resets dialog/math/conversion state and timer pronouns while retaining personal data, undo and timers. Inference and assistant chat share the transcript mutex when timer alerts arrive during a GGUF reply.

`teach favorite game: Halo 3` saves an answer directly on the console; `list learned answers` and `forget answer favorite game` manage it. This is explicit text knowledge, not neural training. Optional `assistant-rules.json` supplies additional text-only FAQ answers, with whole-phrase matching and the longest phrase taking precedence. Ties favor taught answers, then custom file answers, then built-in FAQ entries; built-in commands always take priority. The entire file is validated before replacing loaded rules; it cannot execute scripts, actions or network requests. See [ASSISTANT.txt](../release/ASSISTANT.txt) for formats, limits and examples. The USB package includes assistant-rules.example.json and the guide, excluding user notes/settings/custom rules. A valid assistant-rules.json takes priority; otherwise the example is loaded. Copy the example to that custom filename before editing so updates preserve it. Source builds can copy the example beside 360AI.xex; built-in commands work without it.

Native tests exercise arithmetic/overflow errors, dimension checks, unit follow-ups, word boundaries, polite prefixes, clarification/cancellation, negation, typo suggestions, named timer controls/limits/ambiguity, note search/edit/undo, taught answers, interrupted confirmations, save migration, corrupt-data rejection and backup recovery, plus 2,000 deterministic malformed-input cases. `build/tests/native.exe --assistant` provides a JSON-line transcript harness; `--bench-assistant` measures only host intent processing, not console keyboard or UI latency. Physical Xbox input, timer banners and note persistence still require hardware validation.

Additional debugging regressions cover 78 combinations of read-only commands and pending prompts, complete timer-label matching before optional grammar removal, invalid selectors, math-like taught phrases, and full-notebook multiline save/undo round-trips. JSON emits standard compact escapes for common controls so a valid notebook is not expanded past the 64 KiB persistence limit. Recognized follow-up interruptions use normalized words while explicit note payloads retain their text.

### Saved answers controller screen

Home Y → Saved answers, or `open saved answers`, opens the native editor. A edits the selected response; X collects a phrase and then response, committing only after both validate. Duplicate normalized phrases select the existing entry rather than replacing it from the new-entry flow. Y requires a separate delete confirmation. START uses the existing single-level saved-data undo; LB/RB scroll long text. B cancels a keyboard/confirmation or returns to Quick tools. Edits keep the existing 16-entry, 80-byte normalized phrase and 512-byte response limits and use background persistence without changing the version-2 save format.

Right-stick click previews saved-answer matching. It calls a const, bounded text matcher, with whole-word matching and the longest saved phrase winning. It never calls command dispatch, app actions, model inference, timers, network access or save operations. The preview is restricted to saved answers; it does not evaluate file/built-in FAQ priority. Normal chat still gives built-in commands priority. Controller tests cover cancelled two-step drafts, duplicates, literal command-like/multiline replies, preview isolation, delete/cancel/undo and both entry routes. Physical keyboard/menu/save behavior still needs console validation.

## Saved settings and chat controls

Settings are saved to `360AI.json` beside the executable after an edit or loading a model. This includes context, sampling, reply length, compute/storage choices, logging and chat/server toggles. Values are validated before use; missing fields use defaults. Saves write a temporary file before replacing the main file, and keep the previous valid settings in `360AI.json.bak`. A damaged main file can recover from that backup. Neither configuration file is bundled in updates, so copying a new release preserves your preferences. Remove both files to restore defaults. The app folder must be writable to save preferences or exports.

While the console chat is idle, **X** starts a fresh conversation and clears its KV history while keeping the model loaded. If an API request holds the engine, it reports busy. **START** saves the current console conversation as UTF-8 in `360AI-chat.txt`, replacing the previous export. Nothing is exported automatically. **Y** still cancels an active console reply and **B** returns to the model browser. Controller menus and export persistence require physical-console verification.

The browser highlights the model named by the optional `last_model` preference after restarting, when that file is present. Only a filename beside the executable is accepted; paths and control characters are rejected. Older preferences without this field remain valid. Selection is remembered without automatically loading a model.

D-pad **up/down** scrolls chat by line; **LB/RB** moves by page. Click the **right stick** to return to the newest text. Scrolling up keeps the viewport fixed while a reply streams; returning to the bottom resumes following. A new message or new chat returns to following. Line wrapping is reused while the text is unchanged. The ASCII console font displays a placeholder for unsupported characters; exports preserve UTF-8.

Console prompt fitting starts with the latest user message and adds complete earlier turns while they fit. It uses `encode_prompt` so BOS and template tokens count against the budget. Reply space is reserved (or half the context when the requested reply exceeds it). Very long messages are shortened at UTF-8 boundaries while preserving the template; input/history trimming is reported on screen. The original transcript remains available for scrolling and export. This fitting applies to console chat; API context handling is unchanged.

## Console reply controls

Select **Reply preset** in the Xbox Settings menu and press **A** to cycle the presets. Manually changing a matching value makes the label **Custom**. Existing settings keep top-p and repetition control disabled until selected; upgrades do not apply a preset automatically.

| Preset | Context | Reply cap | Temperature | Top-k | Top-p | Repeat penalty |
| --- | --- | --- | --- | --- | --- | --- |
| Quick | 256 | 48 | 0.60 | 32 | 0.90 | 1.10 |
| Balanced | 512 | 128 | 0.80 | 40 | 0.95 | 1.08 |
| Creative | 512 | 192 | 0.95 | 60 | 0.95 | 1.05 |

Each preset selects compute/storage Auto and preserves logging, model selection and chat/server toggles. Quick provides shorter replies and a smaller context budget. It does not imply a measured increase in hardware decode speed.

**Top-P** accepts 0.05-1.00. After top-k and temperature, it retains the smallest descending candidate prefix reaching that fraction of probability mass, then samples from the retained weights. A value of 1 disables the cutoff; greedy sampling ignores it. **Repeat penalty** accepts 1.00-1.50 and examines only the last 64 tokens of the current generated reply. Each distinct token's positive logit is divided by the penalty; a negative logit is multiplied. Prompt tokens are excluded. A value of 1 disables it. The `top_p` and `repeat_penalty` preferences are saved to `360AI.json`; these controls are for console chat.

The console footer shows layer progress during prompt and decode work. **Y** is checked between attention, feed-forward and layer operations. A current matrix, disk read or GPU operation finishes before the next check. Cancelling retains completed reply text. KV history includes only complete forward evaluations; partial cache writes are overwritten when that position is evaluated again. Native tests cover cancellation during prompt and decode layers, then compare recovered output tokens and valid cache rows against fresh generation. Hardware responsiveness and sampling quality remain console validation tasks.

## HDD streaming and model downloads

The 1.5 USB ZIP includes **Gemma 3 270M Q8_0**, **SmolLM2 135M Instruct Q8_0**, **TinyStories 15M Q4_0** and **TinyStories 260K F32**. Use Auto/RAM for these models. Their pinned revisions, exact sizes and checksums are in `release/models.json` and the ZIP's `models.json`.

| Model | Exact model file size | Reserve free disk space | Status |
| --- | --- | --- | --- |
| TinyStories 260K F32 | 1,185,376 bytes | 0.01 GB | Bundled story toy; mostly nonsense; not an assistant |
| TinyStories 15M Q4_0 | 19,077,344 bytes | 0.03 GB | Bundled simple story completion; 128-token context |
| SmolLM2 135M Q8_0 | 144,811,072 bytes | 0.2 GB | Bundled; RAM/streaming host output parity verified |
| Gemma 3 270M Q8_0 | 291,545,600 bytes | 0.4 GB | Bundled; RAM/streaming host output parity verified |
| Qwen2.5 0.5B Q4_K_M | 491,400,032 bytes | 1 GB | Optional HDD model |
| Qwen2.5 1.5B Q4_K_M | 1,117,320,736 bytes | 2 GB | Experimental HDD model; not console-benchmarked |
| Qwen2.5 7B Q4_K_M | 4,683,073,632 bytes across two files | **5 GB on HDD** | Experimental; extremely slow; not console-benchmarked |

Disk sizes use decimal GB. Reservations are for each extracted model, not extra RAM; add file sizes together if keeping several models. Larger models are optional downloads rather than bundled weights.

The [TinyStories checkpoints](https://huggingface.co/karpathy/tinyllamas) are Llama-architecture base models trained for simple stories, not instruction-following chat. The unchanged [ggml-org GGUFs](https://huggingface.co/ggml-org/models-moved/tree/499bc8821c6b12b4e53c5bffcb21ec206f212d81/tinyllamas) are bundled with MIT license/provenance notices. 360AI recognizes their shape/vocabulary fingerprints and formats turns as plain text continuation without `[INST]` markers. Try **Once upon a time**. The model menu and keyboard explain their limited purpose. 15M clamps the cache to 128 tokens even when Settings requests more; shortening the prompt/reply budget leaves more room. Use SmolLM2 for ordinary conversation.

On a Windows PC, use the included `Download-Models.ps1 -List` to see the catalog, then `Download-Models.ps1 -Model qwen25-05b` (or `qwen25-15b`, `qwen25-7b`). Run it through PowerShell with `-ExecutionPolicy Bypass -File` if needed. In a source checkout, use `tools/fetch-model.ps1` instead. Every download is pinned to a source revision and verified against SHA-256 before activation.

Copy all files to the app folder on HDD/USB and launch `360AI.xex` from that folder. Leave **Weight storage: Auto** selected, or explicitly choose **HDD/USB stream** when needed. Auto makes its decision after reading model configuration and loading the tokenizer, reserving memory for the chosen context and scratch buffers plus 32 MiB. The browser shows combined file size and rounded free-space requirements, and hides non-first GGUF shards. For the 7B model keep both short, console-friendly shard filenames unchanged.

Streaming is weight paging, not operating-system swap. It reads the model repeatedly as tokens are generated, so HDD/USB bandwidth and CPU work can make large models extremely slow. RAM is still required for attention/KV data and the tokenizer; a large HDD does not remove those limits. Physical-console memory fit and speed need validation. The cache is fixed at 8 MB; decoded weights are processed in batches rather than retained as a full float model.

The loader uses 64-bit file positions and standard split-GGUF metadata. Each shard is a complete GGUF file with its own header, not an arbitrary split of raw bytes. Missing/inconsistent shards fail loading. A disk read error stops generation, appears in the UI and `/health`, and returns a storage error to API/MCP clients.

## Model compatibility

| GGUF architecture | Execution path | Tokenization and chat |
| --- | --- | --- |
| `gemma2`, `gemma3` | Gemma embedding scaling, GELU, post norms, existing local/global attention | SentencePiece score merges; Gemma turn markers |
| `llama` | Dense Llama, grouped-query attention, SiLU, consecutive-pair RoPE, tied or separate output weights | SentencePiece for Llama 2 style vocabularies; byte BPE for Llama 3; corresponding instruction/header markers |
| `llama` / SmolLM2 | Dense Llama path, tied output weights | SmolLM byte BPE with individual digits, case-sensitive contractions, incomplete byte vocabulary and ChatML |
| `qwen2` | Dense Qwen2 and Qwen2.5 GGUFs, Q/K/V biases, SiLU, split-half RoPE | Qwen2 byte BPE and ChatML |

The loader validates tensor shapes and rejects unknown architectures, MoE, unequal key/value dimensions, partial RoPE, and unsupported RoPE scaling. Shared Llama 3 frequency-factor tensors are supported. Unscaled models and Llama 3 files carrying their frequency factors are accepted; YaRN, linear scaling and LongRoPE are not implemented. Qwen3, Qwen VL, recurrent/hybrid models, vision and audio are not implemented.

Supported weight formats: F32, F16, Q4_0, Q4_1, Q5_0, Q5_1, Q8_0, Q8_1, Q2_K, Q3_K, Q4_K, Q5_K, Q6_K and Q8_K. These decoders now handle signed values, packed scales, and every block in a row. IQ, BF16 and other unlisted formats are rejected.

Chat formatting selects the built-in format from the vocabulary's control tokens. Arbitrary Jinja `tokenizer.chat_template` programs are not interpreted. Use a model with one of the formats in the table. BPE pre-tokenizers supported here are `qwen2`, `llama-bpe`, `llama3` and `smollm`.

## GPU calculations

The Xenos backend uses Direct3D 9 vertex/pixel shaders for matrix-vector products in attention projections, feed-forward layers and the output head. Each shader pass computes up to 64 output rows and 256 input columns. A fixed 1 MB CPU staging buffer keeps the model quantized in RAM.

This is **partial GPU offload**: CPU work still includes weight unpacking, tokenization, normalization, rotary transforms, attention scores/KV-cache operations, sampling and networking. Shader submissions share a lock with the UI and restore its rendering state.

Results are resolved into a tile-aligned, tiled R32F texture and untiled before CPU readback, as required by Xenos. GPU uploads remain linear. Pixel-center sampling keeps each output aligned with its weight row.

At startup, a GPU readback check compares 65 rows × 260 columns against CPU results, exercising both tile boundaries. Initialization or dispatch failure falls back to CPU. Compute defaults to Auto: after loading, it times 256 rows of the first feed-forward matrix on CPU/GPU, checks real-weight numerical parity and chooses the GPU only for a measured improvement of at least 20%. CPU timing uses the best of three warm runs; GPU timing includes upload, dispatch, synchronization and readback. The chat footer, `/health` and `360AI.log` report the selection. GPU forced remains experimental.

This backend is experimental. Uploads, GPU/CPU synchronization and readbacks may make it slower than the CPU for some models. The startup shader/readback self-test has passed on a user's physical console. Numerical parity on real weights and measured tokens/second comparisons still need hardware validation. No GPU speedup is claimed. For a performance comparison, use the same model and prompt with CPU/forced GPU and set the debug level to **warning** to avoid verbose per-layer file logging.

## Performance and response delay

For normal chat, start with **SmolLM2 135M**, **Reply preset: Quick**, and compute/storage **Auto**. Fresh installs highlight SmolLM2 and use context 256/reply cap 48. Upgrades preserve your selected model/settings; select Quick to reduce old budgets and reset forced GPU/streaming to Auto. TinyStories offers much lower capability for less computation. The included models normally fit RAM; streaming them unnecessarily adds disk latency to every token. Large HDD models can remain extremely slow.

Version 1.5 adds a two-row Q8 CPU candidate that shares input-vector loads. On first CPU use of each quantization/input width, it compares 256 rows using both kernels, checks numerical agreement and takes the best of three warm runs. It selects paired rows only with a measured improvement of at least 10%; otherwise the original kernel is retained. The result is cached for the process (up to 16 shapes) and reported as `CPU:` log lines. Initial calibration adds one-time work. F32 matrix rows on Xenon now swap little-endian floats in VMX registers instead of writing/reading an intermediate float row; float dot products use independent accumulators. Actual console speed must be measured on hardware.

The 1.1.1 CPU Q4/Q5/Q8 kernels multiply signed quantized weights directly, avoiding a full float row conversion and intermediate buffer on every dot product. Xenon uses VMX unpack/conversion operations. Q4_K/Q5_K decoding reuses group scales. Attention dot products, weighted sums and RMS normalization use vector operations. Inference and its two workers run on hardware threads 0, 2 and 4, one per physical core; worker completion uses an event instead of polling/yielding. The old workers inherited their parent's hardware-thread assignment, as described in [Microsoft's Xbox threading guidance](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/coding-for-multiple-cores#creating-threads).

Version 1.3 computes rotary sine/cosine tables once per token position (local and global tables for Gemma 3), then reuses them across heads/layers. Native tests compare the cached transform exactly against the previous split-half and interleaved calculations at multiple dimensions/positions. Both bundled models retain the same greedy sample text as 1.2. This reduces repeated trigonometry; overall Xbox speed has not been measured.

Each KV cache retains the token sequence it has actually evaluated. Subsequent requests reuse the matching prefix and recompute the final prompt token for fresh logits. Changed/truncated prefixes, model reloads, backend changes and explicit cache clearing invalidate the appropriate data. Cancelled generation retains only completed token evaluations. History that no longer fits the context must still be processed again after trimming.

Disk cache misses read the requested matrix batch rounded to 64 KiB, bounded by the 8 MiB cache. A single embedding row no longer causes an 8 MiB read. The chat screen shows prompt tokens processed and elapsed time, followed by decode tokens/second. One `perf:` log line records prompt/cache counts, first-token time, decode time, backend and storage per request. Console replies default to 48 tokens, adjustable from 16 to 512 in Settings; API token limits are unchanged.

Windows host measurements with the same compiler, weights, 16-token prompt and greedy output:

| Model | 1.1 generation | 1.1.1 generation | Output |
| --- | --- | --- | --- |
| SmolLM2 135M Q8_0 | 7.10 s | 2.14 s | Same 12 tokens |
| Gemma 3 270M Q8_0 | 5.49 s | 1.87 s | Same 3 tokens |

A SmolLM2 follow-up reused 27 of 52 prompt tokens and reached its first token in 1.79 s versus 3.69 s fresh. Gemma reused 19 of 41 prompt tokens: 1.76 s versus 3.15 s fresh. Both produced identical output tokens and valid KV entries. Run `build/tests/native.exe --bench-cache MODEL.gguf` to repeat these cache checks. These are host CPU measurements; Xbox VMX speed, core scaling and Auto backend selection still require physical-console validation.

The optional Qwen2.5 0.5B Q4_K_M model uses mostly Q5_0 weights. Its same 10-token host reply (15 prompt tokens) improved from 30.37 s to 17.65 s with the direct Q5 kernel. It remains substantially slower than the bundled models; RAM and streamed results match exactly.

## HTTP API

With the server enabled:

- `GET /v1/models` reports the loaded architecture.
- `POST /v1/chat/completions` accepts text `messages` and the request options below. The `developer` role maps to `system` for the local model's template.
- `POST /v1/completions` accepts `prompt` with the same limits and streaming option.
- `POST /mcp` exposes the MCP endpoint.
- `GET /health` (or `/`) checks connectivity and reports the compute backend and GPU diagnostic.

Requests run sequentially and share the inference lock with local chat. Sampling defaults to the console settings; a request can override it without changing saved preferences. Native OpenAI `tool_calls`, image inputs, nucleus/top-p sampling and per-request context changes are not implemented.

| Request option | Accepted values | Default |
| --- | --- | --- |
| `max_tokens` or `max_completion_tokens` | Integer 1–512; both must agree if supplied together | Console reply limit (128 initially) |
| `temperature` | Number 0–2; 0 selects greedy output | Console setting |
| `top_k` | Integer 1–200; 360AI extension | Console setting |
| `stop` | One string or 1–4 strings, each 1–256 UTF-8 bytes | No extra stop strings |
| `stream` | Boolean | `false` |
| `stream_options.include_usage` | Boolean; requires streaming | `false` |

Matched stop strings are excluded from returned text, including matches across tokens. Streaming holds incomplete UTF-8 characters and stop prefixes until they can be resolved. It begins with an assistant-role chunk for chat, sends progress comments between prompt-token evaluations when processing is slow, and stops inference after a failed streaming send. A single expensive model evaluation can still exceed a client's timeout.

Responses distinguish `finish_reason: "stop"` from `"length"` (reply or context limit). Non-streamed responses include token `usage`; with streaming, `include_usage: true` requests a final usage chunk with empty `choices` before `[DONE]`. Token counts use the loaded model's tokenizer and include generated tokens consumed by a stop match. These fields follow the supported subset of the [official Chat Completions format](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create) and [stream format](https://developers.openai.com/api/reference/resources/chat/subresources/completions/streaming-events).

The final response includes a 360AI `timings` object with input, actual prompt, cached-prefix and generated token counts, first-token/prefill/decode time, decode speed and sampling settings. Input and prompt counts can differ when a request is trimmed to the context. `/health` reports request defaults and a snapshot of the last API/MCP generation. This endpoint is served sequentially, so it does not provide live polling during an active request.

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

The optional source-only companion bridge runs on a PC with **Node.js 22 or later**, with no npm dependencies. It connects to desktop **stdio** servers or **HTTP/HTTPS Streamable HTTP** servers, including servers that respond with SSE. It performs initialization, keeps session headers, follows tool-list pagination, and cleans up processes/sessions.

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

## Hugging Face model library

On the native Models screen, Y opens the model library. X edits a model-name search or accepts an exact `owner/repository`. Searches request GGUF + text-generation repositories from the [Hub API](https://huggingface.co/docs/hub/api). Y cycles parameter limits (200M, 500M, 1B, 3B, Any); LB switches candidate/all families and RB fetches the next server page. Parameter counts come from GGUF metadata and filters apply per page. Unknown counts are excluded by bounded parameter filters; use Any or an exact repository if needed. Known candidate architectures do not guarantee that every file's tokenizer, dimensions, quantization or memory requirements will work.

START cycles family search presets (SmolLM2, TinyStories, Qwen2.5, Llama, Gemma), enabling candidate filtering and a 200M/1B parameter limit. Search text, parameter limit and candidate filter persist in the existing version-1 settings file; older files load their previous defaults. D-pad Left restores a previous cached result page; RB or D-pad Right advances. Up to eight pages are kept in RAM, cleared when starting a fresh search or changing filters. Returning through cached pages avoids HTTPS. A failed next-page request leaves current results available to retry.

Download progress separates transfer and verification: received bytes include reused, verified shards; speed counts only newly received bytes over active request time, excluding disk hashing and reused shards. An approximate remaining transfer time appears after a sample and disappears during verification. Receiving 100% does not mean checks have finished. Local Models uses `std::sort` with deterministic size/name ordering; LB switches order while preserving the selected file and saves the preference. Default ordering is smallest first, with the existing last-model/SmolLM2 initial selection retained.

A repository's files show combined shard sizes and free-space requirements (package size plus 32 MiB). Green Comfortable means under 200,000,000 bytes; yellow Tight is 200,000,000-299,999,999; orange Disk stream starts at 300,000,000. These labels describe weight-file size, not measured console performance or a promise of RAM fit. Compatibility is separate. Dense Llama/SmolLM2, Qwen2/2.5 and Gemma 2/3 candidates are supported; GLM/MoE, newer architectures and unsupported quantizations do not become runnable through HDD streaming.

Downloads pin the repository commit, require LFS SHA-256 metadata, stream to `.part` files, verify hashes on disk and inspect GGUF architecture/dimensions/tensor types. Names include a source-derived hash and fit FATX's 42-character filename limit, including the temporary suffix. Split groups keep shared prefixes and their standard shard suffixes; the first shard is promoted last. Matching already-completed shards can be reused. Existing mismatched files are never overwritten. Cancellation/failure removes newly created files; interrupted partial files are replaced on a later attempt. An `HF-*.json` receipt records source/revision/file hashes and license metadata. No model code is executed.

Files must be smaller than 4 GiB and groups no larger than 32 GiB. Metadata responses and GGUF allocations are bounded. Large transfers have an inactivity timeout, a four-hour per-connection deadline and cancellation checks during disk hashing. Public ungated models only; no account/token flow is implemented. The HTTPS model policy accepts Hugging Face's main host and specific CDN/Xet hosts, separately from the updater's GitHub-only policy. The CA table is initialized before workers start so both clients can run concurrently.

Completed downloads appear in Local models. The library's Use model action selects HDD streaming for packages at least 300 MB; the existing engine uses an 8 MiB weight cache. Activations, tokenizer and KV cache stay in RAM. New model files can still fail tokenizer/memory checks during loading. See [MODEL_LIBRARY.txt](../release/MODEL_LIBRARY.txt). Physical Xbox network/storage behavior and large-model performance remain hardware validation tasks.

## GitHub updates on Xbox

Press **START on Home** or type `open updates` in Assistant. Automatic checks run once per app launch after the dashboard is ready; Y saves an on/off preference. The worker runs at below-normal priority on hardware thread 5. A checks/downloads, then A on the verified staged update restarts into the installer. X cancels network work or discards a staged/failed download. Offline failures leave local app features usable. There is no unattended restart.

Install the complete 2.1 USB bundle once when coming from 2.0 or earlier. Keep its `360AI-updater.xex` beside the app and at least 32 MB free. Later compatible releases provide a `360AI-VERSION-Update.zip` alongside the complete USB ZIP. This deliberately small, uncompressed, single-entry ZIP contains only `360AI.xex`; it is not a first-install bundle. Models, documentation and the installer itself are updated through the complete USB ZIP when needed.

The native client uses vendored [BearSSL](https://bearssl.org/) TLS 1.2 with OS cryptographic entropy, certificate chain/hostname/time validation and checked-in [Mozilla CA roots via curl](https://curl.se/docs/caextract.html). It requires a correct console clock. HTTPS redirects are limited to GitHub's API, release and asset hosts; HTTP fallback and arbitrary hosts are refused. The [latest stable release API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release) supplies the exact app-package URL, bounded size and GitHub SHA-256 digest. Drafts, prereleases, invalid versions, downgrades and missing checksums are rejected. No account, API key, PC bridge or Xbox Live login is needed. Router/DashLaunch rules can still block Internet access; the updater does not change those settings.

Downloads are capped at 16 MiB and streamed to the drive. The worker verifies SHA-256 before parsing the allowlisted ZIP entry, CRC and XEX magic. It stages `360AI-next.xex` and atomically writes `360AI-update.json` containing candidate/current hashes. The app flushes assistant saves and ends model/server work before launching the separate installer, after releasing the GPU frame lock. Installation verifies the candidate again, renames the current app to `360AI-previous.xex`, promotes the candidate, and verifies the installed result. Errors attempt rollback. Timers and model sessions end at restart; notes/settings remain on disk.

If power is lost between renames, launch `360AI-updater.xex` to finish or recover the staged transaction. If necessary, launch `360AI-previous.xex` or copy the full USB ZIP again. This is recovery logic, not a claim of an atomic two-file transaction on FATX. Physical-console TLS, DashLaunch compatibility, file replacement, power-loss recovery and relaunch still need hardware validation. See [UPDATES.txt](../release/UPDATES.txt).

Builds use pinned, checked-in crypto sources and trust anchors without fetching dependencies. `tools/build-trust-anchors.py` regenerates the header from the reviewed bundle and requires Python's `cryptography`; ordinary builds use the generated header. `tools/package-update.py` uses Python's standard library. `tools/build-bearssl.ps1` caches separately compiled host and Xenon libraries. Third-party notices are in the bundle's NETWORK_LICENSES.txt.

## Build

Requirements: Windows, Visual Studio 2010 Professional, and the full Xbox 360 SDK at its default installation path.

```powershell
.\build.bat
```

This compiles and packages `360AI.xex` and `360AI-updater.xex` in the project root. Shader source is embedded and compiled by D3DX on the console; startup numerical validation guards the GPU path.

Fetch all four pinned bundled models with `powershell -ExecutionPolicy Bypass -File tools/fetch-bundled-model.ps1`, then install Python 3 and create and verify the USB ZIP with `powershell -ExecutionPolicy Bypass -File tools/package-release.ps1`. Model sources, sizes and hashes are in `release/models.json`; weights are stored under the ignored `build/models` directory. Release ZIPs are written to `build/releases/v2.4`, with weights, notices, model catalog and optional-model PC downloader. Packaging verifies the executable and all four GGUFs inside the ZIP and prints its SHA-256 for the release notes. The optional MCP PC bridge remains available as source in `bridge/`.

Top-k sampling selects candidates directly from logits and calculates exponentials only for the retained tokens. Generation stops before an unused forward pass once its token limit is reached or cancellation is requested. The default debug level is **warning**; select **everything** when collecting detailed diagnostic logs.

## Verification

With Node.js, Python 3 and the Windows compiler installed (set `GX_PYTHON` to your Python executable if it is not on PATH):

```powershell
node tests/make-fixtures.mjs
.\tests\build-host.bat
node --test tests/*.test.mjs
```

Native tests cover all supported quantized block layouts, dense Llama/Qwen loading and generation, rotary/activation operations, stop/BOS handling, JSON parsing, and MCP dispatch. Protocol tests exercise stdio, JSON/SSE HTTP, sessions, pagination, external tool feedback, allowlists, and the native HTTP server. Tokenizer tests compare 780 inputs against a separate regex/BPE reference.

Synthetic fixtures validate execution paths, not output quality of full pretrained models. Console inference and GPU benchmarks remain hardware validation tasks.

Both bundled models and the optional Qwen2.5 0.5B have generated text with the host build. `build/tests/native.exe --compare-model MODEL.gguf` compares RAM and streamed generation, including the complete KV cache; all three models match exactly. Native tests also cover cache-boundary reads, disk read failure, split-GGUF parity and missing/mismatched shards. Tokenizer reference tests cover 780 randomized/Unicode cases across Qwen2, Llama 3 and SmolLM. HTTP tests run in both RAM and streamed modes. Physical-console performance is not inferred from these host checks.

Implementation references: [GGUF/GGML quantization layouts](https://github.com/ggml-org/llama.cpp/blob/master/ggml/src/ggml-common.h), [Llama execution](https://github.com/ggml-org/llama.cpp/blob/master/src/models/llama.cpp), [Qwen2 execution](https://github.com/ggml-org/llama.cpp/blob/master/src/models/qwen2.cpp), and [MCP 2025-06-18 transport](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports).

## Help and support

Found a bug or having trouble getting started? [Open an issue](https://github.com/zuhagames/360AI/issues) with your 360AI version, model filename, relevant `360AI.log` lines, and what happened. For API connectivity, start with the [`/health` check](#make-a-completion-request).

If you enjoy the project and want to support future development, you can [support ZuhaGames on Ko-fi](https://ko-fi.com/zuhagames).

## Credits

Developed by **ZuhaGames & Canyons**. 360AI is a fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex); thanks to Canyons and the original contributors for the Xbox 360 foundation.

The bundled models are **Gemma 3 270M Instruct**, created by Google, using [Second State's GGUF conversion](https://huggingface.co/second-state/gemma-3-270m-it-GGUF), and **SmolLM2 135M Instruct**, created by Hugging Face, using [lmstudio-community's GGUF conversion](https://huggingface.co/lmstudio-community/SmolLM2-135M-Instruct-GGUF). Their attribution, provenance, and terms are included in the download.

<p align="center">
  <a href="https://github.com/zuhagames/360AI/releases/latest">Get 360AI</a> ·
  <a href="https://github.com/zuhagames/360AI/issues">Report an issue</a> ·
  <a href="https://ko-fi.com/zuhagames">Support on Ko-fi</a>
</p>

