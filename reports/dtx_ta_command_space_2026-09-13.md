# DTX / SMCInvoke gate TA — command space, ID→handler map, and risk classification

**Date**: 2026-09-13
**Scope**: static reverse engineering of the vendor TA `smcinvoke_gate_ta64.mbn` and its client
`GateServiced`, to enumerate the command surface reachable through `127.0.0.1:8341` → `EXEC_CMD` → TA.
**Method**: parsing only. Capstone 5.0 (aarch64) + pyelftools + raw ELF table parsing. **No device
connection, no adb, no emulation, no handler execution.**

## Provenance

| Item | Value |
| --- | --- |
| TA (device) | `/vendor/firmware_mnt/image/smcinvoke_gate_ta64.mbn`, 399,300 B, mtime 2025-05-09 |
| TA (local) | `build-cache/ta_load/pull/ta_smcinvoke_gate64.mbn` |
| TA sha256 | `1c1474273ed07da74db8b51dc2387d2c30459fb026be57b74f5d4a439692c5e2` |
| Client (local) | `build-cache/GateServiced` — sha256 `ecf72a60303aed8ecf7a21f99136572a1949862f7aeb1ee9ffbecbfb09e9fe75` |
| Pull provenance | `reports/gateserviced_ta_load_2026-09-13.txt` (root-daemon copy → `adb pull`) |
| Prior analyses consumed | `reports/static_re_gate_codes_and_rescue_2026-09-13.md`, `reports/FACTS.md` §5.2 |

**Headline structural fact that reframes this artifact**: the TA has **no section headers** but is
**not stripped**. `DT_SYMTAB` @ vaddr `0x565a0` + `DT_HASH` (nbucket=nchain=**796**) yield **754 named
`FUNC` symbols**; the code segment is `PT_LOAD` file `0x3000` → vaddr `0x0`, size `0x4dea0`, so
**vaddr = file_offset − 0x3000** throughout `.text`. In addition the binary carries a **720-entry
wasm-native registry** (name-offset / signature / function-pointer / size, 24-byte stride starting at
vaddr `0x565b8`) and a **48-entry public DTX native-API table** (32-byte stride at vaddr `0x4fa98`).
The command space is therefore recoverable by name, not by inference.

---

> **權威來源**：本檔的結論已折入 `reports/FACTS.md` §5.2（S21／S22）；若兩者不一致，以 **FACTS.md 為準**，
> 並請回頭修正本檔（引用規則見 FACTS §10）。

## 1. Command inventory

There are **four layers** of "command". Only layer B is reachable verbatim from the wire id.

### Layer A — wire ids consumed by the client (`GateServiced`)

`handle_client` reads `n` bytes, takes `cmd_id = LE32(buf[0..3])` and `payload = buf[4..n)`
(`0x216C` `ldr w28,[x8,#0x20]!`; `0x21B0` `sub x2,x27,#4`; `0x21D0` `mov w4,#0x5000` = 20480-byte TA
reply buffer). `cmd_dispatch` (`0x263C`) then:

| Wire id | Handler | Static evidence |
| --- | --- | --- |
| `0x00010004` | `run_unload_ta` → reply `"unload ta succeed"` (17 B) | `0x2660 mov w9,#4; movk w9,#1,lsl#16`; `0x272C bl run_unload_ta` |
| `0x00010003` | `run_load_ta` (payload = TA path, rejected if `len ≥ 0x80` → `0x3002`) | `0x268C mov w8,#3; movk w8,#1,lsl#16`; `0x2798 bl run_load_ta` |
| **anything else** | `run_exec_cmd_ta(cmd_id, …)` → forwarded **verbatim** to the TA | `0x275C` fall-through |
| `0x00010002` (internal) | post-load applet-version query issued *by the client itself* | `run_load_ta` `0x32C8 mov w0,#2` + `0x32D8 movk w0,#1,lsl#16`, input `00 01 00 00`, out 1024 B |

Tags `get_ta_version`, `dtxGetAppletInfo` and the string `"ta version = %s"` live in the client
(`0x16AC`, `0x1831`, `0xF14`) — *they are client-side names for layer-B id `0x00010002`*, not TA symbols.

### Layer B — TA `dispatch()` command ids (the real gate)

`dispatch(uint32 cmd, void *in, int32 in_len, void *out, uint32 *out_len)` @ **`0x3A14`** (840 B).
Group selector is `group = cmd >> 16` (`0x3A5C lsr w8,w23,#0x10`); the low 16 bits select within a
group. Full literal set recovered (`b.eq`/`b.ne` against `mov`/`movk`-materialised constants):

| cmd | group | Handler | Evidence |
| --- | --- | --- | --- |
| `0x00000000`–`0x0000FFFF` | 0 | **none** — accepted, returns 0, output untouched | `0x3A78 cbz w8,#0x3D00` (w22 pre-zeroed at `0x3A68`) |
| `0x0001xxxx` except below | 1 | **none** — returns 0, output untouched | `0x3A9C b.ne #0x3C1C` → `0x3C1C mov w22,wzr` |
| `0x00010001` | 1 | **no-op / ping** — sets `*out_len = 0`, returns 0 | `0x3A94 mov w10,#0x10001`; `0x3AA4 str wzr,[x20]` |
| `0x00010002` | 1 | **`dtx_get_appletinfo`** — copies `"25.05.06.01"` (12 B incl. NUL), `*out_len = 0xB` | `0x3A84…0x3A90 b.eq #0x3BD8`; `0x3BD8 adrp/add → 0x480B0 "25.05.06.01"`; `0x3BF0 str w11,[x20]` (=11); log tag `dtx_get_appletinfo` @`0x3C0C` |
| `0x000A0001` | 10 | `base_ta_function_set_entry` → **`base_minimum_func_set`** (JSON op 1/2/3) | `0x3B30 bl #0x3E68`; `0x3D5C` cmp `0xA0001` ✓ |
| `0x000A0002` | 10 | `base_ta_function_set_entry` → **`base_dynamic_access`** | `0x3E0C b #0xF3FC` |
| other `0x000Axxxx` | 10 | **error** — writes `0x000A0001` LE32 into out, `*out_len = 4`, returns `0xA001` | `0x3E34 mov w21,#0xA001`; `0x3E44 bl write32_ext`; `0x3E50 str w10,[x19]` |
| `0x000B0001` | 11 | **`stx_load_decode`** — load+instantiate a wasm module in the TEE | `0x3B70…0x3B80 b.eq #0x3B80` → `bl #0x7AA0` |
| `0x000B0002` | 11 | **`stx_exec_decode`** — invoke a function in the loaded module | `0x3B68 b.eq #0x3C98` → `bl #0x7FC0` |
| `0x000B0003` | 11 | **`stx_release`** — unload/destroy the module + VM | `0x3B58 b.eq #0x3C60` → `bl #0x81F8` |
| other `0x000Bxxxx` | 11 | "the cmd not support" → returns **2** | `0x3CDC` log, tag `dtx_stx_cmd`, line `0xF7`=247; `0x3CFC mov w22,#2` |
| `0x000C0001` | 12 | **`tee_token_entry`** — token object crypto (JSON op 1..5) | `0x3AF8 b.eq #0x3C24` → `bl #0x8784` |
| `0x000C0002` | 12 | **`tee_token_sfs_func_set`** — token SFS store (JSON op 1..3) | `0x3B08 b.ne #0x3C40` → `bl #0x92F4` |
| other `0x000Cxxxx` | 12 | "the cmd not support" → returns **2** | `0x3C40` log, tag `dispatch_tee_token`, line `0x10C`=268 |
| groups 2,3,6,7,8,9 and ≥ 0x000D | — | "the cmd not support" → returns **2** | `0x3A64 b.gt #0x3AD4`; `0x3A80 b.ne #0x3BB8`; `0x3AE8 b.ne #0x3BB8`; log line `0x15C`=348 |

**ID validation performed by `dispatch()`** (`0x3A44`–`0x3A58`, failure → "the param check failed",
line `0x11C`=284, returns 1):

```
in != NULL  &&  (int32)in_len >= 0   (tbnz w22,#0x1f rejects bit31)
out != NULL &&  out_len != NULL &&  *out_len != 0
```

There is **no capability check, no caller-identity check, no signature over the command**, and no
per-group length validation beyond the above. `CSMCIGate_open` (`0x330`) builds the gate object
`{invoke = 0x3D4, ctx = 8-byte handle}` with no credential inspection; `CSMCIGate_gateExecCmd` /
`CSMCIGate_gateSharedMemory` (`0x3D4`–`0x880`) check only the request framing.

**Request framing (CA → TA, built by `run_exec_cmd_ta` @ client `0x2A70`)** — the TA reads it back at
`0x638`–`0x6EC`:

```
offset 0   : uint8  magic = 0x55 ('U')          ; TA: 0x638 ldrb w8,[x27]; cmp w8,#0x55
offset 1   : uint32 cmd_id   (LE)               ; client: 0x2C0C stur w25,[x8,#1]
offset 5   : uint64 input_len (LE)              ; client: 0x2C14 stur x23,[x8,#5]
offset 13  : payload[input_len]                 ; client: 0x2C1C add x0,x8,#0xD ; memcpy
```
The TA then calls `dispatch_antdtx(cmd_id, payload, input_len, local_out, &local_out_len)` @ `0x2068`,
which logs `"dispatch_antdtx: 0x%8x, data_len:%d."` and calls `dispatch()`. **`dispatch_antdtx` always
returns 0 to the gate** (`0x215C mov w0,wzr`), so an applet-level error surfaces to the CA only as a
**zero-length reply** (client code `0x300E`), indistinguishable on the wire from a legitimate empty answer.

### Layer C — sub-command ("op") values inside layer-B JSON commands

| cmd | Key set (`cJSON_GetObjectItem`) | op | Effect | Evidence |
| --- | --- | --- | --- | --- |
| `0xA0001` | `op`(0x481BE), `fnm`(0x481E8), `wo`(0x4821C), `data`(0x4824F) | 1 | read SFS file `fnm` → `{"err":…,"data":…}` | `0x3F4C cmp w22,#1`; `0x3F50 b.ne #0x4254`; `0x3F80 bl persist_read_file` |
| | | 2 | write SFS file (`wo`=1 overwrite, `wo`=2 append) | `0x3F40 b.eq 0x40C0` (3=delete); `0x4168`; `0x4200 cmp w22,#2`; `0x4224 bl persist_write_file`; `0x43B0 bl persist_write_file_append` |
| | | 3 | delete SFS file `fnm` | `0x40C0`; `0x40D0 bl persist_delete_file` |
| | | other | `"op is invalid"` error JSON | `0x4254`→ log @`0x4268` |
| `0xA0002` | `op`, `data`, `callerInfo` | 1 | read SFS file **`F1EAAF6DB1723BF0BFB0E3160E62FEFF`** | `0xF484 cmp w0,#1`; `0xF4B0 → 0x4991E`; `0xF4BC bl persist_read_file` |
| | | 2 | base64-decode `data`, wrap with `callerInfo`, **overwrite** that SFS file | `0xF480 b.eq 0xF608`; `0xF6A0 bl mbedtls_base64_decode`; `0xFC60 → 0x4991E`; `0xFC6C bl persist_write_file` |
| `0xB0001/2/3` | binary "bts" bytecode container (not JSON) | — | see §3 | `0x7AB4 bl deserialization_load_command`; `0x10558 check_bc_header` (magic `b t s \0`) |
| `0xC0001` | outer: `addition`, `op`; per-op: `flag`, `srvFactor`, `plain`, `value`, `cipher`, `signData`; the `addition` value is itself a JSON string re-parsed downstream (keys incl. `update` @`0x48CE2` on the op=1 path) | 1 | `init_tee_enc_token` — writes the **fixed** SFS object `dtx_yjym_key` and a **caller-derived** object (name taken from the input string at the `&` separator) | `0x8AA8 cmp w27,#1`; `0x8E78 bl init_tee_enc_token`; `0x9A4C bl 0x4217C(x0=input, x1="&") → x27`; `0x9A88 str x27,[sp]`; `0x9B6C persist_write_file(x28=[sp])`; `0x9C30 persist_write_file("dtx_yjym_key")` |
| | | 2 | `tee_token_encrypt` | `0x8AB0`; `0x8BA8 bl #0x9D84` |
| | | 3 | `tee_token_decrypt` | `0x8858 cmp w27,#3`; `0x8CB4 bl #0xA2B0` |
| | | 4 | `tee_token_sign` | `0x8860 cmp w27,#4`; `0x8DC0 bl #0xA8D4` |
| | | 5 | init with `flag` | `0x8868 cmp w27,#5`; `0x8874 → 0x489BB "flag"` |
| | | other | `"not support the cmd: %x, op = %d"` | `0x8E98`; log `0x8EA4` |
| `0xC0002` | `op`, `fnm`, `wo`, `addition` | 1 | `tee_token_func_read` | `0x9464 cmp w23,#1`; `0x947C bl #0xB23C` |
| | | 2 | `tee_token_func_write` (`wo` selects overwrite/append) | `0x945C b.eq 0x95B8`; `0x9664 bl #0xB3A8` |
| | | 3 | `tee_token_func_delete` | `0x9454 b.eq 0x9598`; `0x95AC bl #0xB578` |
| | | 4+ | `"op is invalid"` | `0x9680`; log `0x969C` |

### Layer D — named DTX handlers (TA symbols + strings)

`#` = string-backing identifier; all others are `FUNC` symbols in the TA's own dynsym. File offsets are
TA file offsets; vaddr = offset − 0x3000.

| Identifier | String file off | String vaddr | Referenced at | Containing function |
| --- | --- | --- | --- | --- |
| `dispatch_antdtx` | `0x4ae93` | `0x47e93` | `0x20b0, 0x20f8, 0x2120, 0x214c` | `dispatch_antdtx` (`0x2068`) |
| `CSMCIGate_open` | `0x4a97d` | `0x4797d` | `0x3b4` | `CSMCIGate_open` (`0x330`) |
| `CSMCIGate_gateExecCmd` | `0x4a9d6` | `0x479d6` | `0x550, 0x5a0` | gate exec entry (`0x3d4`) |
| `CSMCIGate_gateSharedMemory` | `0x4aa4a` | `0x47a4a` | `0x4c8, 0x618, 0x66c, 0x70c, 0x7c4, 0x7f0` | gate shared-mem entry (`0x3d4`) |
| `dtx_get_appletinfo` | `0x4b0e2` | `0x480e2` | `0x3c0c` | `dispatch` (`0x3a14`) |
| `dtx_stx_cmd` | `0x4b0f5` | `0x480f5` | `0x3cf0` | `dispatch` |
| `dispatch_tee_token` | `0x4b101` | `0x48101` | `0x3c54` | `dispatch` |
| `base_ta_function_set_entry` | `0x4b133` | `0x48133` | `0x3d94, 0x3e24` | `base_ta_function_set_entry` (`0x3d5c`) |
| `base_minimum_func_set` | `0x4b1a8` | `0x481a8` | `0x4040, 0x4060, 0x4088, 0x4268, 0x4290, 0x4370` | `base_minimum_func_set` (`0x3e68`) |
| `base_ta_func_read` | `0x4b2fb` | `0x482fb` | `0x3fa0, 0x42b0, 0x4304` | `base_minimum_func_set` |
| `base_ta_func_write` | `0x4b397` | `0x48397` | `0x4244, 0x43d0, 0x43f4, 0x44b0, 0x44d8` | `base_minimum_func_set` |
| `base_ta_func_delete` | `0x4b414` | `0x48414` | `0x40f0, 0x42dc, 0x4340` | `base_minimum_func_set` |
| `base_dynamic_access` | `0x4c90a` | `0x4990a` | `0xf584, 0xf5a4, 0xf5d4, 0xf6f8` | `base_dynamic_access` (`0xf3fc`) |
| `base_dynamic_access_read` | `0x4cd3f` | `0x49d3f` | `0xf4dc, 0xf740, 0xf96c, 0xf98c, 0xf9ec` | `base_dynamic_access` |
| `base_dynamic_access_write` | `0x4cd84` | `0x49d84` | `0xf6cc, 0xf720, 0xf774, 0xf854, 0xf90c, 0xf9b8` | `base_dynamic_access` |
| `base_dynamic_access_prase_huawei` | `0x4c973` | `0x49973` | `0xfd8c, 0xfe08, 0xff48, …` | `base_dynamic_access_prase_huawei` (`0xfd24`) |
| `base_dynamic_access_prase_honor` | `0x4cb8a` | `0x49b8a` | `0x100e4, 0x1019c, 0x103ac, …` | `base_dynamic_access_prase_honor` (`0x1007c`) |
| `tee_token_entry` | `0x4b940` | `0x48940` | `0x8940, 0x8970, 0x899c, 0x8a08, 0x8a94, …` | `tee_token_entry` (`0x8784`) |
| `tee_token_sfs_func_set` | `0x4bc5d` | `0x48c5d` | `0x9498, 0x94b8, 0x94e0, 0x9500, 0x954c, 0x9584` | `tee_token_sfs_func_set` (`0x92f4`) |
| `init_tee_enc_token` | `0x4bccf` | `0x48ccf` | `0x97c4, 0x98c4, 0x9974, 0x99e8, 0x9a20, 0x9ad8` | `init_tee_enc_token` (`0x9710`) |
| `tee_token_encrypt` / `_decrypt` / `_sign` / `_delete` | `0x4bf80` / `0x4c0e8` / `0x4c220` / `0x4c282` | `0x48f80` / `0x490e8` / `0x49220` / `0x49282` | `0x9e64…`, `0xa390…`, `0xa99c…`, `0xaf70…` | resp. functions (`0x9d84`, `0xa2b0`, `0xa8d4`, `0xaeb8`) |
| `stx_load_decode` | `0x4b57f` | `0x4857f` | `0x7ad4, 0x7d78` | `stx_load_decode` (`0x7aa0`) |
| `stx_exec_decode` | `0x4b664` | `0x48664` | `0x8008, 0x803c, 0x80b0` | `stx_exec_decode` (`0x7fc0`) |
| `sfs_persist` | (dynstr `0x5d6b1`) | — | — | `sfs_persist` (`0x7814`) — **the single SFS choke point** |
| `dtx_sfs_write` / `_read` / `_delete` | `0x4b6cd` / `0x4b91e` / `0x4b92b` | `0x486cd` / `0x4891e` / `0x4892b` | `0x8610` / `0x8680` / `0x86c4` | **`stx_test` (`0x82c8`) — dead code, zero callers** |
| `dtx_sfs_write_file` etc. (public API names) | `0x4cf65`, `0x4cf78`, `0x4cf8a`, `0x4cfa9`, `0x4cfc4`, `0x4cfd8`, `0x4cff2` | — | table @ `0x4fb18` (32-byte stride) | wasm-native export table |
| `dtx_rsa_key_generate_path` | `0x4da58` | `0x4aa58` | `0x13f94, 0x13fbc, 0x1408c, 0x1412c, 0x14160, 0x1422c` | `dtx_rsa_key_generate_path` (`0x13ea4`) |
| `dtx_delete_rsa_keypair` | `0x4db05` | `0x4ab05` | `0x14278` | `dtx_delete_rsa_keypair` (`0x1423c`) |
| `dtx_sm2_keypair_gen_path` | `0x4e23b` | `0x4b23b` | `0x14a94` | `dtx_sm2_keypair_gen_path` (`0x14a24`) |
| `dtx_aes_encrypt` | `0x4d202` | `0x4a202` | `0x125d0, 0x12630, 0x12764, 0x12790, 0x12870` | `dtx_aes_encrypt` (`0x124fc`) |
| `dtx_sm4_encrypt` | `0x4e294` | `0x4b294` | `0x1545c` | `dtx_sm4_encrypt` (`0x1539c`) |
| `dtx_yjym_key` — **real fixed SFS object name** | `0x4bec5` | `0x48ec5` | `0x9c04 (is_file_exist), 0x9c28 (write), 0x9c68 (read), 0xa024, 0xa544, 0xab50, 0xb038` | `init_tee_enc_token`, `tee_token_encrypt/decrypt/sign/delete` |
| `token` — **NOT an SFS name**; label for `dump_to_hex_data` | `0x4be4c` | `0x48e4c` | `0x9b44` | `init_tee_enc_token` |
| `tmpkey` — **NOT an SFS name**; label for `dump_to_hex_data` | `0x4befc` | `0x48efc` | `0x9cb0` | `init_tee_enc_token` |
| `F1EAAF6DB1723BF0BFB0E3160E62FEFF` (SFS object) | `0x4c91e` | `0x4991e` | `0xf4b0, 0xfc60, 0xfd64, 0x100bc` | `base_dynamic_access`, `base_dynamic_access_prase_huawei/honor` |
| `1234567812345678` (SM2 default user-id) | `0x4e254` | `0x4b254` | `0x14b74, 0x14ba0` | `dtx_sm2_sig`, `dtx_sm2_verify` |

**Additional identifier sets recovered (not individually string-referenced — reached by pointer tables):**

* **wasm-native registry** @ vaddr `0x565B8` (24-byte stride, 720 entries; `{u32 name_off; u16 sig_a; u16 sig_b; void *fn; u64 size}`). **57 `dtx_*` names** are registered here, including `dtx_sfs_write_file_platform`, `dtx_rsa_key_generate_path`, `dtx_delete_rsa_keypair`, `dtx_rsa_sign/verify/encrypt/decrypt`, `dtx_rsa_keypair_gen`, `dtx_ecc_gen_keypair/sig/verify`, `dtx_sm2_*`, `dtx_sm3_hash/hmac`, `dtx_sm4_encrypt/decrypt`, `dtx_aes_encrypt/decrypt`, `dtx_hash`, `dtx_hmac`, `dtx_gen_random`, `dtx_*` memory/string helpers. The runtime resolves these by name at wasm instantiation (`wasm_native_lookup_libc_builtin_global` @ `0x31fc8` contains direct `b`/`bl` edges into `dtx_aes_decrypt` @`0x333d4`, `dtx_sm4_decrypt` @`0x34370`, `dtx_SM2_encrypt` @`0x33e04`, `dtx_sm3_hash` @`0x3416c`, `dtx_gen_random` @`0x32e10`/`0x33034`, …).
* **public DTX native-API table** @ vaddr `0x4FA98` (32-byte stride, 48 entries; `{char *name; void *wrapper; char *signature; 0}`): `dtx_sfs_write_file (*ii)i`, `dtx_sfs_read_file (*ii)i`, `dtx_sfs_read_file_seek (*iii)i`, `dtx_sfs_is_file_exist (*)i`, `dtx_sfs_delete_file (*)i`, `dtx_sfs_write_file_append (*ii)i`, `dtx_sfs_get_file_size (*i)i`, `dtx_gen_random (*~)i`, `dtx_hash (*~ii)i`, `dtx_hmac (*~*~ii)i`, `dtx_aes_encrypt/decrypt (i*~*~*~ii)i`, `dtx_rsa_keypair_gen (iii)i`, `dtx_rsa_key_generate_path ($iii)i`, `dtx_delete_rsa_keypair ($)i`, `dtx_rsa_sign/verify/encrypt/decrypt`, `dtx_ecc_gen_keypair/sig/verify`, `dtx_sm2_gen_keypair`, `dtx_sm2_keypair_gen_path ($*~*~)i`, `dtx_SM2_encrypt/decrypt`, `dtx_sm2_sig/verify[_userid]`, `dtx_sm2_ACKDpriv/pub`, `dtx_sm2_decode/encode_sign_asn1`, `dtx_sm3_hash/hmac`, `dtx_sm4_encrypt/decrypt`, plus `get_cur_time`, `stee_record_begin/end`, `fmax/fmin/sqrt/tanh/fabs/dense`. Wrappers convert wasm app addresses and tail-call the real DTX function (verified: wrapper `0x32e68` = `dtx_sfs_write_file` → `0x32EA8 b persist_write_file`).

---

## 2. ID → handler mapping

**Answer to the specific question**: the client's `EXEC_CMD` 32-bit id is **not translated** — it is
copied byte-for-byte into the TA request header (`run_exec_cmd_ta` `0x2C0C stur w25,[x8,#1]`) and is the
`cmd` argument of `dispatch()`. Hence:

* `0x00010001` → `dispatch` group 1, exact-match branch → `*out_len = 0`, return 0 ⇒ **client sees a
  0-byte TA reply and reports `0x300E`** ("TA response length == 0", `0x2D2C`). ✔ matches the measured
  "0x00010001 → 0-byte reply".
* `0x00010002` → `dispatch` `0x3BD8` block → `memcpy(out, "25.05.06.01", 12)`, `*out_len = 11` ⇒ client
  sends 11 bytes `"25.05.06.01"`. ✔ matches the measured `EXEC 0x00010002 → "25.05.06.01"`.
* `0x00010003` / `0x00010004` are **consumed by `cmd_dispatch` before the TA** (LOAD_TA / UNLOAD_TA) and
  can never reach `dispatch()` via `EXEC_CMD`.
* Any other id: reaches `dispatch()`, whose group logic either reaches a handler (groups 1/A/B/C) or
  takes the "the cmd not support" path returning **2**, which `dispatch_antdtx` suppresses to 0, producing
  a 0-byte reply ⇒ indistinguishable on the wire from a legitimate empty answer.

**Dispatch tables**: there is no array-indexed jump table — the compiler emitted a compare tree on
`cmd >> 16` followed by exact 32-bit compares within the group (`0x3A5C`–`0x3BB4`). Every id is
enumerated literally in §1 layer B. The only true *table-driven* dispatchers are (a) the wasm-native
registry (name → function pointer, §1 layer D) and (b) the 48-entry public DTX API table.

**Payload validation, per handler**:

| Handler | Validation | Evidence |
| --- | --- | --- |
| `dispatch()` prologue | non-NULL in/out/out_len, `in_len ≥ 0`, `*out_len ≠ 0` | `0x3A44`–`0x3A58` |
| `base_minimum_func_set` | `cJSON_Parse` non-NULL; `op`,`fnm` must be JSON strings passing `checkJsonValid_base_func`; `strlen(fnm) ≤ 0x40`; `data` base64/plain-string; `wo ∈ {1,2}` | `0x3EAC`, `0x3ECC`, `0x3F28 cmp x0,#0x40`, `0x4200` |
| `base_dynamic_access` | `cJSON_Parse`; `op` string; `op ∈ {1,2}` | `0xF43C`, `0xF47C`–`0xF488` |
| `tee_token_entry` | `addition` + `op` must be strings; op ∈ 1..5; per-op required keys (`flag`,`srvFactor`,`plain`,`value`,`cipher`,`signData`); internal buffer caps (`0x101`, `0x100`) | `0x8838`, `0x8850`–`0x886C`, `0x93B4 cmp x0,#0x101` |
| `tee_token_sfs_func_set` | `op`,`fnm` strings; `strlen(fnm) ≤ 0x101`; `op ∈ {1,2,3}` | `0x9364`, `0x93B4`, `0x9450`–`0x9464` |
| `stx_load_decode` | `deserialization_load_command` → `set_stx_name`; then `check_bc_header` (magic `bts\0` + version) and **`dtx_sm2_verify` against embedded `pub_key`** before `wasm_runtime_load` | `0x7AB4`, `0x10558`, `0x10C1C`–`0x10C70`, `0x10D2C` |
| `stx_exec_decode` | `ta_name` check, `stx name` check, `dsv_invoke` result | `0x8034`, `0x80A8`, `0x8198`, `0x81D8` |
| `sfs_persist` | `strlen(p1) ≤ 0x10`, `strlen(p2) ≤ 0x10`, `op ≤ 7`, `strlen(name) ≤ 0x100`; else `"param_check check failed"` + `0x130B` | `0x786C`, `0x787C`, `0x7890`, `0x7898` |

**No caller/ACL check exists anywhere on the path** `handle_client → cmd_dispatch → run_exec_cmd_ta →
CSMCIGate_gateExecCmd → dispatch_antdtx → dispatch`. The only identity-ish fields
(`callerInfo` in `base_dynamic_access` `0xFC30`, `callerInfoObj`/appname/hash strings in the token
module) are **stored as data**, not compared against the caller.

---

## 3. Risk classification

Legend: **RO** = read-only/query; **RC** = resource-creating; **MU** = mutating (persistent state or key
material); **UNK** = unknown — do not run.

| Command / handler | Class | Evidence |
| --- | --- | --- |
| `EXEC 0x00000000`–`0x0000FFFF` | **RO** (no-op) | group 0 → `0x3A78 cbz w8,#0x3D00`, `w22 = 0`, output untouched |
| `EXEC 0x00010001` | **RO** (no-op, `*out_len = 0`) | `0x3AA4 str wzr,[x20]` |
| `EXEC 0x00010002` (`dtx_get_appletinfo`) | **RO** (version string, hardcoded `"25.05.06.01"`) | `0x3BD8`–`0x3C20` |
| `EXEC 0x0001xxxx` (other) | **RO** (silent no-op) | `0x3A9C b.ne #0x3C1C` |
| `EXEC 0x000Axxxx` (other) | **RO** (4-byte error code `0xA001`; no state change) | `0x3E34`–`0x3E64` |
| groups `2,3,6..9, ≥0xD` | **RO** (refused, returns 2 → 0-byte reply) | `0x3BB8`, `0x3CFC` |
| `EXEC 0xA0001` op=1 (`base_ta_func_read`) | **RO — but information disclosure**: reads any SFS object by caller-supplied name | `0x3F80 bl persist_read_file`; name from `fnm` `0x481E8` |
| `EXEC 0xA0001` op=2 (`base_ta_func_write`) | **MU** — arbitrary SFS overwrite (`wo=1`) or append (`wo=2`) | `0x4224 persist_write_file`; `0x43B0 persist_write_file_append` |
| `EXEC 0xA0001` op=3 (`base_ta_func_delete`) | **MU** — arbitrary SFS deletion | `0x40D0 persist_delete_file` |
| `EXEC 0xA0002` op=1 (`base_dynamic_access` read) | **RO — but disclosure** of the fixed activation object | `0xF4BC persist_read_file` on `0x4991E` |
| `EXEC 0xA0002` op=2 (write) | **MU** — overwrites the fixed object `F1EAAF6DB1723BF0BFB0E3160E62FEFF` with attacker-supplied base64 `data` + `callerInfo` | `0xFC6C persist_write_file` |
| `EXEC 0xB0001` `stx_load_decode` | **RC/MU — highest risk**. Creates a wasm VM inside the TEE; the module can then call 57 registered `dtx_*` natives incl. `dtx_sfs_*`, RSA/SM2 key generation and key deletion. Gated by SM2 signature over an embedded key. | `0x7AB4`–`0x7C4C` (`wasm_runtime_load/instantiate/create_exec_env`); `0x10C20 adrp/add → 0x4F2E8 pub_key`; `0x10C70 bl dtx_sm2_verify`; failure → `"verify sign failed!"` line `0x89` |
| `EXEC 0xB0002` `stx_exec_decode` | **MU/UNK** — invokes an arbitrary exported function of the loaded module (`dsv_invoke`), i.e. arbitrary native calls under the module's signature trust | `0x80A8`, `0x7D98 dsv_invoke` |
| `EXEC 0xB0003` `stx_release` | **MU** (destroys VM/module state; frees) | `0x81F8` → `wasm_runtime_destroy/unload/deinstantiate` |
| `EXEC 0xB0xxxx` (other) | **RO** (refused → 0-byte reply) | `0x3CDC`, `0x3CFC` |
| `EXEC 0xC0001` op=1 (`init_tee_enc_token`) | **RC + MU** — generates a 16-byte random key if absent, SM4-CBC-wraps with embedded `sm4_key_cyp`/`sm4_iv_cyp`, **writes the fixed SFS object `dtx_yjym_key`** and a **caller-named** token object | `0x9C0C persist_is_file_exist("dtx_yjym_key")`; `0x9C1C dtx_gen_random`; `0x9C30 persist_write_file("dtx_yjym_key")`; `0x9B6C persist_write_file(<name from input>)` |
| `EXEC 0xC0001` op=2 (`tee_token_encrypt`) | **MU** — reads `dtx_yjym_key`, SM2/SM4-seals a token; may write token blobs | `0x9D84`; `0xA024 persist_read_file("dtx_yjym_key")` |
| `EXEC 0xC0001` op=3 (`tee_token_decrypt`) | **RO-ish but key-touching** — decrypts with `dtx_yjym_key` | `0xA544 persist_read_file`; `0xA2B0` |
| `EXEC 0xC0001` op=4 (`tee_token_sign`) | **RO-ish but key-touching** — SM3-HMAC/SM2 sign with stored key | `0xAB50 persist_read_file`; `0xA8D4` |
| `EXEC 0xC0001` op=5 | **UNK** — `flag`-gated initialisation variant, not fully traced | `0x8874 flag` |
| `EXEC 0xC0002` op=1 (`tee_token_func_read`) | **RO — disclosure** of any token object by caller-supplied `fnm` | `0x947C` → `0xB23C` → `persist_read_file` |
| `EXEC 0xC0002` op=2 (`tee_token_func_write`) | **MU** — writes/overwrites/appends a token object | `0x9664` → `0xB3A8` → `persist_write_file[_append]` |
| `EXEC 0xC0002` op=3 (`tee_token_func_delete`) | **MU** — deletes a token object | `0x95AC` → `0xB578` → `persist_delete_file` |
| `EXEC 0xC0002` op≥4 | **RO** (`"op is invalid"`) | `0x9680` |
| `dtx_rsa_key_generate_path` | **RC + MU** (wasm-only): reads existing keypair from SFS; if absent generates and **writes a 0x60-byte keypair blob** | `0x13F44 dtx_sfs_read_file_platform`; `0x141C4 dtx_sfs_write_file_platform`; `do_save_keypair` `0x149AC` |
| `dtx_delete_rsa_keypair` | **MU** (wasm-only): deletes keypair file | `0x1423C` → `dtx_sfs_is_file_exist_platform` + delete |
| `dtx_sm2_keypair_gen_path` | **RC + MU** (wasm-only) | `0x14A24`; `0x14AF8 do_save_keypair` |
| `dtx_rsa_keypair_gen`, `dtx_ecc_gen_keypair`, `dtx_sm2_gen_keypair` | **RC** (in-memory key generation, no I/O seen) | `0x12D08`, `0x13830`, `0x14910` |
| `dtx_sfs_write_file` / `dtx_sfs_delete_file` / `dtx_sfs_write_file_append` (wasm API) | **MU** | table `0x4FB18/0x4FB98/0x4FBB8` → wrappers → `persist_write_file` / `sfs_persist` |
| `dtx_aes_encrypt`, `dtx_sm4_encrypt`, `dtx_hmac`, … | **RO/RC** (pure computation, no storage) | no `persist_*`/`qsee_sfs_*` edges in their call graph |
| `stx_test` | **not reachable** (dead code, zero callers) | `callers(stx_test) = ∅` |
| `base_dynamic_access_prase_huawei` / `_honor` | **not wire-reachable**; registered as wasm natives only | registry entries `0x59920` / `0x59068` |

---

## 4. Persistence blast radius

**Yes — the TA has full, persistent secure-storage capability, and it declares it.** The TA metadata
block at vaddr `0x4E000` reads `"smcinvoke_gate_ta64"`, `"1.0"`, `"None"`, `"n=smcinvoke_gate_ta64;p=8:…"`
and the `ta_filesNoPersist` byte at `0x4E062` is **`0x00`** → persistent files are permitted.

**Storage backend**: the TA calls the imported QSEE SFS API (import stubs resolved from `libcmnlib.so`
via `.rela.plt`):

| Stub | GOT | Import |
| --- | --- | --- |
| `0x240` | `0x51138` | `qsee_sfs_open` |
| `0x250` | `0x51140` | `qsee_sfs_error` |
| `0x260` | `0x51148` | `qsee_sfs_close` |
| `0x270` | `0x51150` | `qsee_sfs_getSize` |
| `0x280` | `0x51158` | `qsee_sfs_read` |
| `0x290` | `0x51160` | `qsee_sfs_seek` |
| `0x2a0` | `0x51168` | `qsee_sfs_write` |
| `0x2b0` | `0x51170` | `qsee_sfs_rm` |

`dtx_sfs_*_platform` are thin wrappers over these (e.g. `dtx_sfs_read_file_platform` @ `0x1447C`:
`bl 0x240` open → `bl 0x280` read → error string `"qsee_sfs_read failed: 0x%08X"`). QSEE SFS is the
TEE's encrypted file store (RPMB/persist-backed on Qualcomm SoCs) — **it is not the Android filesystem**,
so nothing here is visible in `/data` and nothing here is backed up by ADB.

**The single choke point** is `sfs_persist(p1, p2, name, op, buf, len, out_len, …)` @ `0x7814`, which
builds the SFS name with `sprintf("%s.%s.%s.%s", "sec_storage/tee", p1, p2, name)` (`0x794C` fmt `0x48500`,
args `0x4847F "sec_storage/tee"`, `0x48475 "0000"`, `0x4847A "sync"`) and then dispatches on `op`:

| op | callee | persist wrapper |
| --- | --- | --- |
| 0 | `dtx_sfs_read_file_platform` | `persist_read_file` |
| 1 | `dtx_sfs_write_file_platform` | `persist_write_file` |
| 2 | `dtx_sfs_write_file_append_platform` | `persist_write_file_append` |
| 3 | `dtx_sfs_delete_file_platform` | `persist_delete_file` |
| 5 | `dtx_sfs_is_file_exist_platform` | `persist_is_file_exist` |
| 6 | `dtx_sfs_read_file_seek_platform` | — |
| 7 | `dtx_sfs_get_file_size_platform` | `persist_get_file_size` |
| 4 | *(rejected — falls to the error path)* | — |

**Correction on `"token"` / `"tmpkey"`**: those two literals (`0x48E4C`, `0x48EFC`) are passed to
`dump_to_hex_data` @ `0x762C`, which is a **4-byte no-op stub (`ret`)** in this release build. They are
debug labels, **not SFS object names**. The only fixed SFS object names in the token module are
`dtx_yjym_key` (and the activation record `F1EAAF…` in the `base_dynamic_access` module).

**Non-obvious return convention** (worth recording before anyone writes a probe): `persist_is_file_exist`
returns **0 when the object EXISTS** and `0x1302` when `qsee_sfs_open` fails (`dtx_sfs_is_file_exist_platform`
@ `0x142F4`: `w20 = 0` by default, set to `0x1302` only on open failure; `0x1309` on close failure).
`init_tee_enc_token` relies on this: `cbz w0,#0x9C64` ⇒ exists ⇒ read the existing key; non-zero ⇒
generate 16 random bytes (`dtx_gen_random`) and write `dtx_yjym_key` (`0x9C30`). The same convention
drives the `"token already exist, will not write token"` guard at `0x9BD8`.

**SFS names observed in the TA (all under `sec_storage/tee.0000.sync.<name>`):**

| Name | Written by | Read by | Meaning (inferred) |
| --- | --- | --- | --- |
| `dtx_yjym_key` | `init_tee_enc_token` (`0x9C30`) | `init_tee_enc_token` (`0x9C68`), `tee_token_encrypt` (`0xA024`), `tee_token_decrypt` (`0xA544`), `tee_token_sign` (`0xAB50`), `tee_token_delete` (`0xB038`) | **root key** (16 B) used to seal/unseal tokens; generated once with `dtx_gen_random`, guarded by `persist_is_file_exist` |
| *caller-derived name* (from the CA input string, split at `&`) | `init_tee_enc_token` (`0x9B6C`) | `init_tee_enc_token` (`0x9B9C` existence guard), `tee_token_encrypt` (`0x9F10`) | sealed token blob; the guard logs `"token already exist, will not write token"` (`0x9BE4`) |
| `F1EAAF6DB1723BF0BFB0E3160E62FEFF` | `base_dynamic_access` op=2 (`0xFC6C`) | `base_dynamic_access` (`0xF4B0`), `_prase_huawei` (`0xFD64`), `_prase_honor` (`0x100BC`) | device activation / authorisation record (JSON with `callerInfo`) |
| **any caller-chosen name** ≤ 0x40 chars | `0xA0001` op=2 | `0xA0001` op=1 | generic vendor key/value store |
| **any caller-chosen name** ≤ 0x101 chars | `0xC0002` op=2 | `0xC0002` op=1 | token store |
| keypair blobs (0x60 B: 0x20 priv + 0x40 pub) | `do_save_keypair` (`0x149AC`) via `dtx_rsa_key_generate_path` / `dtx_sm2_keypair_gen_path` | `do_read_keypair` (`0x14914`) | RSA/SM2 keypairs |

**Embedded key material in the TA image** (constants, at vaddr):

| Symbol | vaddr | size | bytes (head) |
| --- | --- | --- | --- |
| `pri_key_cyp` | `0x4F238` | 32 | `d3ef3691b240b06668f3eb24834c33007e1bdc8adc371c0b0ec46ac7b13d853a` |
| `pub_key_cyp` | `0x4F258` | 64 | `d3b23a349c2805c9986769bb9277d2c7d1eab5e8d62977ed0cc6462d75eacb14…` |
| `sm4_key_cyp` | `0x4F298` | 16 | `c9986769bb9277d2c7d14c33007e1bdc` |
| `sm4_iv_cyp` | `0x4F2A8` | 16 | `39096afeae3030fa1edd7013a8cd850a` |
| `pub_key` (STX bytecode verifier) | `0x4F2E8` | 64 | `0b77e987513f616b693287c664d4719e5d6ac6ada3d66a2bece7dd7e5c849ce1…` |

**What is lost / irreversible if a mutating command is run**

* `0xA0001` op=2/3 with a real object name (e.g. `dtx_yjym_key`, `F1EAAF6DB1723BF0BFB0E3160E62FEFF`)
  **overwrites or deletes the device's TEE-resident activation record / root token key**. There is no
  backup path in this TA: the only recovery primitives are `init_tee_enc_token` (regenerates `token`
  + `dtx_yjym_key` from a fresh random key → **all previously sealed tokens become undecryptable**) and
  vendor re-provisioning. Note `tee_token_func_write` and `init_tee_enc_token` use `persist_is_file_exist`
  and the log line `"token already exist, will not write token"` (`0x9BE4`) as a guard — but `0xA0001`
  op=2 with `wo=1` bypasses that guard entirely.
* `0xC0002` op=3 / `0xA0001` op=3 delete objects outright; `dtx_delete_rsa_keypair` destroys stored keypairs.
* `0xC0001` op=1 (re-)initialises `dtx_yjym_key` when absent; any data previously sealed with an older key is
  permanently unrecoverable (the key is random per initialisation — `dtx_gen_random` @ `0x9C1C`).
* `0xB0001`/`0xB0002` execute vendor-signed bytecode inside the TEE; the module can call any of the
  SFS/key natives above, so its blast radius is the union of all of them.
* **Factory reset**: the objects live in the TEE SFS (`qsee_sfs_*`, `sec_storage/tee.…`), **not** in
  `/data`. An Android factory reset / `userdata` wipe is therefore **not expected to restore them**, and
  the reverse also holds — a wiped SFS would break token/activation features until the vendor
  re-provisions. *This last point is a platform-behaviour inference from the API used, not something
  measured in this workspace; flagged in §6.*

---

## 5. Recommended safe probe order (not executed)

> **Nothing in this section was sent.** No connection to `:8341`, no adb, no device command was issued.

Wire encoding for every probe: TCP to `127.0.0.1:8341` on the device, one message =
`LE32(cmd_id) || payload`, server reply = raw bytes (0 bytes = TA produced nothing / refused).

| # | Request (hex) | Expected | Why it is safe |
| --- | --- | --- | --- |
| 1 | `00000000` + *(no payload)* | 0-byte reply (group 0 no-op) | id 0 is a pure no-op branch; confirms the instrument still shows "empty" for success-with-no-output |
| 2 | `**0200**0100` = `02 00 01 00` | 11 B `"25.05.06.01"` | already measured; the positive control — must still work before any other probe |
| 3 | `01 00 01 00` (`0x00010001`) | 0-byte reply (`0x300E` in logcat) | explicit no-op branch; harmless, distinguishes "empty because no-op" from "empty because refused" only via logcat |
| 4 | `02 00 01 00` with a 4-byte payload `00 01 00 00` | 11 B version | same as #2 but byte-identical to what the client itself sends — the most conservative repeatable probe |
| 5 | `ff ff 00 00` (`0x0000FFFF`) | 0-byte reply | top of the group-0 no-op range; bounds the "silent success" region |

**Stop there.** Do **not** probe:

* `0x000A0001` / `0x000A0002` **even with `op=1`** — the read paths succeed only by naming a real SFS
  object; a typo'd `fnm`/`op` is harmless but a correct one **discloses device activation/token material**,
  and any confusion between `op=1` and `op=2` in the encoder converts a read into an **irreversible
  overwrite** of `F1EAAF6DB1723BF0BFB0E3160E62FEFF` or of a caller-chosen object.

  If a read really must be attempted later, the *least* dangerous form is `0xA0001`, `op=1`, with a
  deliberately **non-existent** name, e.g. payload
  `7b226f70223a2231222c22666e6d223a22726f6f742d70726f62652d6e6f2d737563682d66696c65227d`
  (`{"op":"1","fnm":"root-probe-no-such-file"}`) — expected `{"err":…}` and **no state change**.
  Never send `"op":"2"` or `"op":"3"`.
* `0x000C0001` / `0x000C0002` — op 1 initialises/overwrites the token root key; ops 2/3 rewrite or delete
  token objects. Key-touching even on "read" (they decrypt with `dtx_yjym_key`).
* `0x000B0001` / `0x000B0002` / `0x000B0003` — the STX path instantiates a wasm VM in the TEE and runs
  bytecode; `0xB0003` destroys live VM state. **UNKNOWN — do not run.**
* Anything ≥ `0x000D0000`, groups 2/3/6–9 — no handler exists; they only return 2 (0-byte reply), so they
  carry no information but also no benefit.

Ordering rationale: probes 1→5 ascend only through the group-1/group-0 region, which the disassembly
proves contains **no storage, no key and no allocation side effects** (`0x3A68`–`0x3D44` touches only the
caller's own output buffer). They are safe by *static proof of the handler body*, not by convention.

---

## 6. Confidence and unknowns

**High confidence (direct instruction-level evidence)**
* The complete layer-B command-id set and its exact matching tree, including every literal constant and
  the "the cmd not support" return-2 path.
* `0x00010001` → empty reply and `0x00010002` → `"25.05.06.01"`, matching the live measurements in
  `reports/FACTS.md` §5.2 (S10/S11).
* Wire id pass-through (no translation) from `run_exec_cmd_ta` into the TA request header.
* The `sfs_persist` name format `sec_storage/tee.0000.sync.<name>`, the op→backend table, and the
  `qsee_sfs_*` import bindings via `.rela.plt`/GOT.
* The fixed SFS object names (`dtx_yjym_key`, `F1EAAF6DB1723BF0BFB0E3160E62FEFF`) and that `dtx_yjym_key` is created/read by the token module; and that `"token"`/`"tmpkey"` are *not* object names.
* The SM2 signature gate on the STX bytecode path (`pub_key` @ `0x4F2E8` is a hardcoded constant, not
  attacker-supplied) — so `0xB0001` is **not** a trivially usable arbitrary-script primitive.
* `stx_test` is dead code (its `dtx_sfs_write/read/delete` strings are not the live API; the live ones are
  the `_platform` functions and the registry entries).

**Medium confidence**
* op=5 of `tee_token_entry` (the `flag`-gated variant) — the branch was located but its full semantics were
  not traced; classified UNK.
* `base_dynamic_access`'s exact saved-JSON schema (`callerInfo` array handling at `0xFC20`–`0xFD14`).
* The exact SFS name `init_tee_enc_token` derives from the CA input (it is the substring located by a `&`
  scan at `0x9A4C`, so the CA supplies `"<bizID>&<name>"`-style strings); the precise grammar of that
  string was not fully reconstructed.
* The exact header field widths the TA reads back from the shared buffer are partially reconstructed
  (magic/cmd_id/input_len confirmed on the writer side); the CA-side layout is what a prober needs, and
  `GateServiced` builds it, so this does not affect §5.

**Unknown / not verifiable statically**
* **TEE-side access control on the gate TA itself.** This analysis covers only the TA's own logic. Whether
  a *non-root, non-system* Android app can reach `:8341` and have the SMCInvoke service load the TA is a
  property of `smcinvoke`/`qseecom` ACLs and SELinux on the device, not of this binary. FACTS §5.2 S12
  asserts "any local app" on the strength of a root-context experiment (R1 says SELinux was Permissive
  during the session) — treat that as **unconfirmed for untrusted_app**.
* Whether the TEE SFS survives an Android factory reset on this specific Rokid build (vendor
  configuration of the persist/RPMB partition). The API-level inference says "not restored by
  `userdata` wipe", but this was not measured.
* The meaning of the metadata tail `p=8:c47728cf3e4089,61,84:1001,b4` at `0x4E088` (looks like a QSEE
  service/permission descriptor; not decoded).
* Whether the `dispatch()` prologue's `*out_len != 0` check can be used to distinguish "refused" from
  "no-op" without logcat — statically it cannot; both yield a 0-byte reply.

---

### Reproducing this analysis (static only)

```
/home/kali/rokid_root/.venv/bin/python build-cache/ta_re/tadis.py 0x3a14      # dispatch()
/home/kali/rokid_root/.venv/bin/python build-cache/ta_re/tadis.py 0x2068      # dispatch_antdtx()
/home/kali/rokid_root/.venv/bin/python build-cache/ta_re/inventory.py 'dtx_,CSMCIGate,persist_'   # reachability
/home/kali/rokid_root/.venv/bin/python build-cache/ta_re/nativetab.py         # 48-entry DTX native API
/home/kali/rokid_root/.venv/bin/python build-cache/ta_re/nattab2.py           # 720-entry wasm registry
/home/kali/rokid_root/.venv/bin/python build-cache/ta_re/sfsnames.py          # constant SFS names per storage API
```
Helper scripts live under `build-cache/ta_re/` (workspace-write scope only).
