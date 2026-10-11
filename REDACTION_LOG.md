# 遮蔽紀錄（REDACTION LOG）

本檔記錄本次發布前對專案所做的**個資與洩漏憑證遮蔽**，包含每個受影響檔案的
遮蔽前後 SHA-256，以便任何人驗證處理過程可回溯、無隱匿。

遮蔽日期：**2026-10-11**　｜　受影響檔案：**134**　｜　置換規則：**17**

> **本檔刻意只顯示被遮蔽值的片段**（例如 `1901…`），
> 因為若在此列出完整原值，這份紀錄本身就會變成外洩來源。
> 完整原值保存在工作區外的 `~/rokid_root_unredacted_backup/`，不納入版控。

## 一、背景

本專案在製作對外影片與送審件時，已對**衍生檔案**（影片、字幕、redacted 附件）做過去識別化；
但**原始文件**（`docs/`、`reports/`、`RV101_Disclosure/` 部分檔案）與 `desktop/` 子專案仍保留
真實識別資訊與裝置綁定憑證。本次發布將其一併遮蔽，使 repo 內不再存在可識別個資或洩漏憑證。

## 二、置換對照表

| 佔位符 | 原值（遮蔽顯示） | 說明 |
|---|---|---|
| `<DEVICE_SERIAL>` | `1901…` | Rokid 眼鏡序號（`ro.serialno`），16 碼 |
| `<PHONE_SERIAL>` | `eeaa…` | 對應手機序號 |
| `<DEVICE_ALIAS>` | `Glas…` | 以眼鏡序號末四碼構成的裝置別名 |
| `<RESEARCHER_NAME>` | `—` | 研究者真名（中文與英文兩種寫法） |
| `<RESEARCHER_EMAIL>` | `zero…` | 研究者個人信箱 |
| `<RESEARCHER_HANDLE>` | `zero…` | 研究者帳號代稱 |
| `<USER>` | `lian…` | Windows 帳號名稱（出現於 `C:\\Users\\…` 路徑） |
| `<HOST>` | `Lian…` | Windows 主機名稱 |
| `<REDACTED_OTA_KEY>` | `E217…` | Rokid OTA API key（`ro.boot.key`） |
| `<REDACTED_OTA_SECRET>` | `3A29…` | Rokid OTA API secret（`ro.boot.secret`） |
| `<REDACTED_OTA_DEVICETYPEID>` | `B4BE…` | Rokid OTA deviceTypeId（`ro.boot.devicetypeid`） |
| `<REDACTED_GOOGLE_MAPS_API_KEY>` | `AIza…` | 自 Rokid App 洩漏的 Google Maps API key |
| `<REDACTED_PAYMENT_APPKEY>` | `GLAS…` | 洩漏的付款 AppKey |
| `<REDACTED_PAYMENT_APPID>` | `GLAS…` | 洩漏的付款 AppId |
| `<REDACTED_PAYMENT_SIGNATURE>` | `c272…` | 洩漏的付款簽章 `signV2` |
| `<REDACTED_REFERENCE_ID>` | `9935…` | 付款請求中的參照 ID |
| `<REDACTED_UTDID>` | `aX8u…` | 裝置指紋 UTDID |

## 三、因遮蔽而修改的程式碼（非單純文字替換）

為避免「送出不完整憑證 → 得到難懂的伺服器錯誤」，下列程式碼除了替換值之外另加了防護：

| 檔案 | 變更 |
|---|---|
| `desktop/src/zero_light_desktop.c` | 佔位符仍在時直接 `log_msg` 提示並跳過 OTA 檢查，不送出注定失敗的請求 |
| `desktop/tools/i18n_master.json` | 新增字串 `LOG_OTACHECK_NOCRED`（14 語系），說明憑證已遮蔽 |
| `desktop/src/i18n_strings.h` | 由 `tools/gen_i18n.py` 重新產生（242 字串 × 14 語系） |
| `deploy/ota_api.py`、`scripts/rokid_ota_*.py` | 原以環境變數為優先、內建值為後備；內建值改為佔位符，仍可用 `KEY=`／`SECRET=` 等環境變數覆寫 |

驗證：`desktop/build_mingw.sh` 已在本機重新編譯通過（PE32+ / Windows GUI 子系統），
且 `tools/check_logfmt.py` 回報 147 個多語系格式化呼叫引數全部相符。

## 四、逐檔雜湊（遮蔽前 → 遮蔽後）

> `遮蔽前` 取自 `~/rokid_root_unredacted_backup/` 的備份檔。

| 檔案 | 遮蔽前 SHA-256 | 遮蔽後 SHA-256 |
|---|---|---|
| `MANIFEST.md` | `926b4f6605b3ab2ecb1ee33496a41d32a10106507ccbba3afbdd936c796c11e8` | `a6939b06c040707061303b0f9f1c57e50a4fc7155cc28e202b32cc0dcde7e8cf` |
| `README_ORGANIZED.md` | `28faf18d98b1b48ca06c319de0c2c3c549a5095f9fc5e82c0c941f4402c2c4e4` | `7b2d79a39c830adc298f2bc590aae9a729f501cd146696568819bf5cf440aaab` |
| `RV101_Disclosure/01_Executive_Summary/Executive_Summary_EN.md` | `cefda0ac82a3f123f082679e13951de8f15ff16f7196f825058ad01bdb74f3f6` | `d11e319251e98382af2a46df096757d19a2afdda20c5d567d54b2c0ada46dcd7` |
| `RV101_Disclosure/02_Device_Information/00_adb_devices.txt` | `eec62ea04a0b5971bc106b978e1911513a53c3e89f72763b01839c31e3f65867` | `7819b8d4b20dafb03c27f506983d5a0c46c49ffabf57eebbdf0c7c8f11e31a06` |
| `RV101_Disclosure/04_Exploit_Execution/GHOSTLOCK_HANDOFF_V13.md` | `a030cd2b0862b37adfc1875032f76b12cca15799d596c1c4efc26e2e00e42836` | `0c1e629a76ecda1788da6e7901129fcc071b5a5f41cddbcce55e64694a8e8ce2` |
| `RV101_Disclosure/04_Exploit_Execution/GHOSTLOCK_ROOT_操作說明.md` | `df520e15c06e0144c0222ec51bb67b49ad357bcb99aad8625e1255bc6eea1c69` | `6368b0baf8b798e38f6bfdbcb28cd564abe0cca38e144299a079b088a92eb986` |
| `RV101_Disclosure/11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md` | `2854746fcf754d31fe720c933746dcb797270c00be544364645852477b364f72` | `f841c642503249137a8124f989f78330189c02de97112cc9a2614b7bb656748c` |
| `RV101_Disclosure/12_Hashes/SHA256SUMS.txt` | `5d9c756d1a7c5039ca5103a32b96b9cc69ad569bf17428e6b9f7e3cc78b7bae4` | `81947d3b7b4346c03cc3f15802722606a6ae22ca9604d438187e8626fd4b9097` |
| `RV101_Disclosure/14_TWCERT_Report/TWCERT_佐證PDF_內容清單.md` | `f683f43e45731cfd0eb86576f6b3df9f18b9a1f827601bc23112a8985b222707` | `5cee22d26d54672d5fac6cfd0f1391f55bed7373afaad64139b43865b0b8ee96` |
| `RV101_Disclosure/Evidence_Checklist.md` | `59c98873a3b15532a91a26e75e77fdc81ca21a15951807e6d4f4996156d4c95a` | `9886cbfdfaa391bf3ee895ee889f33a44dc4745b47ac78c5c09db33fe7985d6d` |
| `RV101_Disclosure/Evidence_Index.md` | `25ebe0a91fd99d0f29d13cad5df0baa780984e4029e0b36ed17ee570fcceac89` | `77dd497cca23353326d5cad73b99f69dc1cc58c344b58b54673883145cbcb5e4` |
| `RV101_Disclosure/RV101_Vulnerability_Report.md` | `43cedae4fd44e93d43593fb7980899004af7536685d037875794f3973c83c6b8` | `51cafceb905a1eac49f53c2df303402583f0887b0b4f219086327a920badf1a1` |
| `RV101_Disclosure/Redaction_Checklist.md` | `7e486a40fb58c92c3c15b17f0a215704589f16db015670229457886fdf2d3cf8` | `5f62b6f941ba36dcb4b1543a0aaacac6a54b5e60c5fc5a88cc665e1f1af40a2a` |
| `RV101_Disclosure/SHA256SUMS.txt` | `1e588b0ef1957f43bc2038d8354a34e7a7163250fa02fd10183f9bbcee369495` | `ce1dee2819536a8b5408eae1a5fbf74ea61b7bb71786c6d4466fcf12c00cbbe5` |
| `deploy/adb_wifi_verify.sh` | `19be108221a3e1ad2c4837ca098bf23c990a60455a83882efe9f6dadd946a4b2` | `a2a13c2414b30c8d82d7ca5bba1e4778379f6b827443905f31834faf388435a2` |
| `deploy/build_adb_relay.sh` | `a24170e7932528a93f0efaeb449da907ee4b1b4ec3f42c732679882b9e774142` | `882b3abc0a78d72bd673b98002382e10e07392d288f54cad3b9c6a9ddbf7b321` |
| `deploy/build_kgsl_probe.sh` | `834f5ea137130f76c7a4169ede16fd98b8774ae47a2c02187113d7a9757c5f2b` | `849975a0b7f044b631fcf1acc09d92c7f74c06c79acb5e062996cb765211ac01` |
| `deploy/build_ring_input_daemon.sh` | `3f560388501cebd5f957201a0c5c352f4c4585919c20507f0ef32fac394b60c8` | `60f160279e285b286b9d5f1eefaf1065de5a7dccbc7009c8e9230a2a9e46a180` |
| `deploy/build_root_daemon.sh` | `e57ddcb032194729409a2b9a95b430c657937ea94c511dca89e607eea15ed50b` | `69c5df2796353b022147a8132d23f2f176ae8ff200c51bdedde83c1edaf55b24` |
| `deploy/check_ota.sh` | `8daf0fdabdcc7b54206ce4e4775a7b52f91d322f56391c6309f738e8c5a7a2af` | `39aae3f56574e387a851d779f49fda53d4a03b3b4a692f971c44165e8dac5e0b` |
| `deploy/env.sh` | `8aa8615e1c8e1dd1e86af2896ee862d2996332cc127a127df5ad9518b926f1ad` | `6a7b6809c45e70f0b4f86441a1b541a007c5e7d0a98fdd1c7549de720726d9b4` |
| `deploy/get_root_now_run2.out` | `5f393bea4b755e5ee66e3f90222c9921d47b8cc2b3cd20865ce720e64b86c86d` | `cc12536de8978db81b08c026753a0ddfea60a9de22bcd3030d965b9dca2c8a72` |
| `deploy/ghostlock_patch_check.py` | `b6a24895e8e25226911d874caf24f916c7efcb0b9b37786d33deb2d3bcd3063a` | `f7e12a491f0226225fe04efd879983d7d91e37488875730143e1d1d4c6c27781` |
| `deploy/lock_ota.sh` | `0238558629ace4be92643a27f7f9cc33de910b69948a2112f5de6b8ffa1e492c` | `086e58628d4c521d83b140ab622cc2af68d1cfa92a01fa6eb1915dd7c14b9dd3` |
| `deploy/ota_api.py` | `9f41a8df54ab5c80daa058a21798d54dc1305831e390d7a36282468678c1fad5` | `2c5114e8b96737d67fb95c2217a2180852a1067c852f445d78dae9f5a4147d83` |
| `deploy/start_shizuku.sh` | `51e034d776350b7b62764d61c6e4076ae292f38504c7bf6a0bbb60b52314acbe` | `6f7ebb4477f5f8316f9a50fc2570e85fc4526384b16d0ec44a958e42967f4248` |
| `deploy/test_shizuku_e2e.sh` | `ef4dc69cdcd7c14ba2a4f94c2213350a6685afa6284a34e137a71016cba362e1` | `1635587160c5024aa0764a70969d663077477341893f45aec36fbad2b17df87b` |
| `deploy/usbdebug_hider_ctl.sh` | `3d0948dc088877da832527cfd25fb2c3ce96867883c6fa729d1be7aa791f00a8` | `7b8828d168937015c50b11ecac8562e65efbad562ee674e389c37fd884a854a8` |
| `dist/README.md` | `f3e9f2f0814a3ac1323bcc820cea00eef4c29d46b541ff83c54b5c76a1719cce` | `46ac4ece71b90e982c331ac50e7711b1c5fe16c48294c898ba9336f996164855` |
| `docs/RV101_NARRATION_v2.md` | `aaf013a8632afeb0c74790c1cd2759b1a7ae1c0ef44f47dc8f2991386569a51d` | `780103dc6d7609c56c52ee48925621bad04f1338ee2701e3d88a4151913e772b` |
| `docs/VIDEO_PRODUCTION_PLAN_v2.md` | `13b8fae03137a7a4a2a52aa9ca4db151889f28c0c8e05b1c552b8b88a69a43c0` | `cf5735104a8c8910b42b31bfd6c496a9d3cff2af5b52725040391349c504a785` |
| `docs/capture-verification-report.md` | `2c182a3272e595eeb31f9ca43b09e9c16864811b8d2b73872de857fc7d64bae2` | `b5ff1790ff388b3d4adb1c050c0de784f607d56b6cd2e3b5c357b0997dd54b6a` |
| `docs/deploy.md` | `2c57cb094906c284ab19179a3f041e8e0754b0f8b566849b08fe2787e24a156a` | `a7b792c285a8404e81d139b9388c5a821dadf180b60a5fa80c1d3105562d3c75` |
| `docs/device-debug-notes.md` | `a09ae9d83f489d0ec075bc518338a56d4b6461f703e0c188863219f1e14bd251` | `ea3cded3e3b5e5879c6e24fd0020ba20cfa5e76e47aaaa01663d97d79539d88e` |
| `docs/privacy-audit-report.md` | `9a03637f3375210e8a8409cb854a0704ab7d55ae2715275fbf3a0280b94d0e47` | `df5a24a4e5765d89b0bd7a30bd66a758c5fd24d664ae52a2b5255bede9ca7e7e` |
| `docs/reply-to-rokid-2026-09-06.md` | `a5374ce30b392970aae05ba3ad1feb97b0f11f441819b4d0579347307120cce6` | `6f2c08311ab79b1bcc39808ebeaa3f05cd4b5df553d76056e6e8063539780b35` |
| `docs/reply-to-rokid-2026-09-16.md` | `99bcc4e464bca743ccc8a797a25de88d4fea1538ac58a5fc486ffaddbe5cdf37` | `fd4925b6c331a5b61d9349d160c8c513b70683fcb66fec221cf4d1fe3c64e61f` |
| `docs/root-from-zero-guide.md` | `246e49549537149274ab93cbd1ea1dfdfc1d07c9d8f90f88f756d4824d7d8c10` | `c6cb58213a504b976cdc2ce351395ef853cd857b81f041802e6cd3f8d428ded3` |
| `docs/shizuku-feasibility-report.md` | `27291642b171d86845050d9874ff6e8d6fd6fe2f2e51a86b44e4a4544140cc89` | `0ee6a6634f899521df2ca0c7afa891bf2e6488e42ecba198062595d76d3bc32b` |
| `docs/wireless-adb-solution-design.md` | `20c6b69ddf7d47c5e6ab03e66028c0cc18b31e1446b101167a3999c3f2066bc1` | `879426e24dbe8fd7d4b3779191028a8f8973fb7ba1aaca3bf8e698a993f43c40` |
| `reports/5.2_VERIFICATION_2026.md` | `705c5f50b0a869c40a0c36a0c0a0db786c88b51c206a53358936b938d5c1a592` | `f850387151b701098926446a9bf8474e22fdc803e045818c86ad50a8a2ff053e` |
| `reports/5.2_VERIFICATION_LIVE_ADB_2026.md` | `6d4043dfffa75e8f6d0d42962e0d9a8c94a9c96fce958380612af55eb3f5bb3f` | `0dc3f9c18ee84b6b2659c81fb8ed3e9bcfef09565c7f9e893e23bd499ccab4ae` |
| `reports/ADB_Media_Exfiltration_Report.md` | `a95638a1dc730d3ab14d25ca1cd75eb4680f79ec6fff2df2d1232aaa9d575a8c` | `a4c9a3c03750b9e40554b46a1332b474100979de5eebc3726429a9d7d27acf61` |
| `reports/APK_TW_Tech_Report_Rokid_Root.md` | `cb78200aaf893115c66126f82b2de2884d51355f40a84f98a523c078e6c4f7ef` | `9abda446ff0b1d872e587a592a50cf238ac0f2d33ce9283573aea7d791a275ff` |
| `reports/APK_Vulnerability_Analysis_Report.md` | `01f217fbe68f584a6ca0200934a3ee107c4963e238368a6fec51e705fa8a7d18` | `9af36262b33a9af1ad01eae8f68bdd78bf887eace8653811f3a932183f89a0a4` |
| `reports/ATTACK_SUMMARY.md` | `99b6d2ca514301c0a007bc57a699817435b03f50c8ce9b50c87c5b59947db1f5` | `a8db624a1a580b93ebb2e2449a57544b5414ae3359be8c9ebc720ed4c21b1ea9` |
| `reports/CURRENT_STATUS_20260822.md` | `b4f96440cc1207cfd068f097244b0aa2b105b9292d7d68d735fd97e04162a838` | `a2115844e829d6a02c2a717b5875e6d0a127647d4909bebd8fee62e96a6b72c3` |
| `reports/CVE-2024-36971_analysis.md` | `f12655e8e06f82685f24ac361a4e9f28858a42af03619c280d0018c10d9d1a69` | `0e468ace709f615f44ce401880d4bff96152617b28d19bd65c3a60db4480a580` |
| `reports/EXPLOITATION_REPORT.md` | `5ed9c7376d5f5b5d00ca37093b430f6ecab9875493199aa3f34c4777378dd54c` | `5c22b4158a4d0394579491160ed63666a5b57a0b78328e95a82ef58ab60ff009` |
| `reports/FACTS.md` | `627750f97349cc67ea646df27f7a9f12db07458bbd7d49e572052be69fd367b5` | `9d33d444c56a0c9929b85f16fae8b08513c96d683abc732662400c965aaf6a1d` |
| `reports/GHOSTLOCK_HANDOFF_V10.md` | `ac91e0df9e3d811e2d1746eb6be977223beeebd9b0e861a565ba0e8696fd147d` | `133227626d654a8f621f4c07fc96bea62ec4c052ed3e975e092024c8d8e0188c` |
| `reports/GHOSTLOCK_HANDOFF_V11.md` | `ae56669514ac26401c0cb85c50a1284aaaff4d42f94497307e57369f5b569413` | `07126edcd6c4060943e7a7008ed93364d0397f7606be527446abff34d32be0d8` |
| `reports/GHOSTLOCK_HANDOFF_V12.md` | `59c4008ade45f32e28b6d3ce9b946a18fa255ad861569f6476f1c5b826f5997a` | `8ff7bd9e6dc0768d1eaf0075a1e815900a614cc5484c24ad6b20ba52cd681e0c` |
| `reports/GHOSTLOCK_HANDOFF_V13.md` | `a030cd2b0862b37adfc1875032f76b12cca15799d596c1c4efc26e2e00e42836` | `0c1e629a76ecda1788da6e7901129fcc071b5a5f41cddbcce55e64694a8e8ce2` |
| `reports/GHOSTLOCK_HANDOFF_V2.md` | `245f77b3f3d93082f935e2489510cba6e97bf3a6068aca55221f804fc77f7e85` | `b937982529d03b964d07eb4807cd46c5703e9c11e227ad3b7beaccb137e7f0ee` |
| `reports/GHOSTLOCK_HANDOFF_V3.md` | `e5bf5a276205c214fb156d8b9fd103db7f45493ef72a6e8e45ec3ef71c659f1c` | `2edf9d48f5992d109a13c814ec4a49915cc7abe3621601eaf33cc7ae4f250125` |
| `reports/GHOSTLOCK_HANDOFF_V4.md` | `8797b2db4b20c2e7f66933984c8431f1faae15bdb2b198b2a04ae7bffeb56e28` | `19790ab459cb438d4451598858566eb25ac28814a46be93f6d5fc0b1c277fdd4` |
| `reports/GHOSTLOCK_HANDOFF_V5.md` | `042cada3f3beec0b0f68590181c95fa28f06cb8d903bd64cc0ce326ce87631a1` | `63485c6f512eb062f0d32d66d3f568ffd4d087608b46cffba5e8add6da880f33` |
| `reports/GHOSTLOCK_HANDOFF_V6.md` | `57d04990ba7b04624a93338c8e02ea4b02302bb9265604ea38d2508ba43c70d8` | `9e4bbe7524549845d7b0867f89efea673fb2bb48958bc2ac2e98021714465def` |
| `reports/GHOSTLOCK_HANDOFF_V7.md` | `4a784de2b9619a4f7990e4bd933ae96b60848c1e874982f7b1c18ee1d0023d2f` | `f928fe7e4132125de5c4c5e995e88f013801f925e4d53dd27fb910aa6f400f46` |
| `reports/GHOSTLOCK_HANDOFF_V8.md` | `fe612798779aa810005b3538fb28e198eaa4817435f8d6277d63b698241d8f34` | `4d0fe020f0453b90f3ddadac4f79a7e32caf4abb7407ac01b7de81d22372a33d` |
| `reports/GHOSTLOCK_HANDOFF_V9.md` | `75034e7367932176bb3b80b80f242b74ec105bf530b1f98ac04b9f9786dce274` | `d58e07e046c4dbd568e73e56f6614d3b6e696f7c3e4f533b38e9e7ced350a69b` |
| `reports/GHOSTLOCK_HANDOVER_PROMPT.md` | `79ae056ee5b46be6f2fb8b7a75dfe6e8e3d6a8a43575a2059de0888e466ccc79` | `3a9afbfeeb3b8cfb41c63551f6c0eb676b489754bbafc8d2e364975d90f2653e` |
| `reports/GHOSTLOCK_ROOT_操作說明.md` | `5ec722fdecf2fbd2bc597827055d43d87cafabf5746b09fca64b451751f6ca1d` | `26e0531d067084d4634b37da440ddbe340f14055b364399878917f2ef18f4983` |
| `reports/GHOSTLOCK_W2_CONFIGFS_READ_BLOCKER.md` | `2f6f87e2039a9c4eecbadfdf8e8b3f2ed694fbf940ff4e2ea1690a468e58f383` | `1f9ea15f420918b6d09bf13dff23ca56a9787d894cfa0e36524bdfa01e5101d3` |
| `reports/HANDOVER_SESSION_20260131.md` | `3986045e47ff652da455f2b76fec5f6f924cba71a6b219dbbbc9a7e463f4d6f0` | `82f2acc70aa4e73548f67f27a023a32ea34d50f83eabe5c534c94fdaecb87202` |
| `reports/NEXT_AGENT_PROMPT.md` | `16ba3b04ad5ccbfb501d182635109f520382993c2ec0f0bd5cf80c56cc62de75` | `dbd591bf787f3619cd984aed5618a5e21b129d4857013087b164cc902eb5f92a` |
| `reports/NEXT_AGENT_PROMPT_V4.md` | `f069cb0e49c42d03e8f455721d8b99e41b2fd5afdb9ce769bd7a0dc532892402` | `43b5d333dfb2374e36bccca389912c4a5c11ddbab7b34ffe9dd06dc209006efa` |
| `reports/NEXT_AGENT_PROMPT_V7.md` | `2f8c382333545426b6a4380de091b653b7a8a5f1ae6809b273ab4e02d014020b` | `51f343104c9311a060eed4fde1a79442b79fc19433a5d956f08c1df1a23596a4` |
| `reports/NEXT_AGENT_PROMPT_V8.md` | `1e4af065ef04ebeec1d54889ddd81ae5827cf8115dde45a0cd23b7010e802f68` | `6bc0c21ccb87da08709a86dddde9369729229ee39f58cab0d4fd072435507f7e` |
| `reports/OTA_Authentication_Analysis_Report.md` | `05524da5fa3c36f62edaf16aaf71692f8c43c2c1544ca4cf5c3804df6a4438c6` | `d047ff993246d2a6201897be6f8f86da4a364e3ecaa3f93221435a30c5694492` |
| `reports/PARASITIC_INJECT_ATTACK.md` | `aa29b944a5f82912645ebcf2c7d94dfbcfacf2baf401e131ad775c71a672db60` | `c93f6e0d7ad078dbc2f7b6e7bc04813d1e9e359ab0715d2efd7eada9a7a6701d` |
| `reports/ROOT_PROCEDURE.md` | `fda7db1e33320bea24e0f08818c39f4dfbea161eb14260d4e13252373f144df7` | `014e3a469959698456fbe21b99564590ce452fb5e076557b15ead15f831c592c` |
| `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` | `8f9411fc2c57d1c25f51f413c8a1a0af71b213f2300c4e695ab89abf295664f8` | `62b9fb266018a636f0f18f74f3eb3f4fb09b47af1babfb062fe01d326076622d` |
| `reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md` | `c2460a4ebd8614ec80d06bded8d9caeff9f855084f8a533af3cd807477c7fb67` | `6923ce69c6b7cc9fa2dd41f47ad31648f2ae2e494b096aa43e9efb64543fad1f` |
| `reports/Rokid_Glasses_System_Report_EN.md` | `45338f471c576ac4b20500146b68981247391e56c030d405fc7064b2bea5e0e7` | `9cb2489ac8e9b2391e05dc98d839ac3373cd8bdc13c32c543e7edf4adfbe1b79` |
| `reports/Rokid_Glasses_系統資料報告.md` | `3d537559d56da875d1668899de23b083b97990e78a591d9ccdc0d241233b9512` | `4fbd61e43a43eb257621cdf5caed8f5b29af7ef197fcd9648798fc2dadeb2dc6` |
| `reports/Rokid_Glasses_系統資料報告_Marp.md` | `36994fd1ca1c322ac8188953ecf2d454c00d8c19662765ae0649cb6cdb697ba8` | `10f1b0c2ae1b82f04829e1c2487c472b2eeb813ddacef31fa05e922bf20eba88` |
| `reports/Rokid_RedTeam_Internal_Report.md` | `f7eafcde3bce507eec4bce135503f1230b57c110f86cee28e4a81f86df6f9bc2` | `614ee875e4b61bd36c01f74a6c73d8d0a1d852e53fdb66ad39d2817ba4ee10af` |
| `reports/Rokid_漏洞報告信件.md` | `7af26c1dfe0c22fdf6cbccbe1f731af5ca3cc454a6be389fe47212f61fc06063` | `b2daeddb2bf50d6404650acb1e963d2143c528f90a5941e3961b375d50d4e04d` |
| `reports/SESSION_15_SUMMARY.md` | `8fcaebf93829e951c70b1381e094e726b09710e8661af37848f1034b733faa18` | `6a920283c4deb6625041a3b0e47665d3d654eadfe0b8a49ac663b9d0d5a2c738` |
| `reports/adbreachprobe_untrusted_app_adbd_socket_2026-09-20.md` | `12f2e7fbc2ec6c68bdeb9c4d62f22ee269d2f7dafc31a4ba14aa43a06bde60bf` | `d0bc4605614a06f89cd829638cdd51e93e30f791066b4a2aca74e14de403a2be` |
| `reports/aivela_ring_connection_failure_2026-09-13.md` | `a86f05d57edc7e2fee16ec7692628fe7620def98a6f2d437605a83dc56d9e379` | `eb02523613d1b65174f6aacde1aa265cda04361da3942ce6a99e9edebe082856` |
| `reports/arm_safety_policy_2026-09-13.md` | `807d8783f6c4a4aa6b65834c11cda7bcdf3665a48ee8a32c34dd8fa4f5f80871` | `b41e9896216c5486fb832613fb19be3ed8e06cbe66df85391dd86b23f8392639` |
| `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` | `58674b9524ea50dc9e9e4d3a5329c22ad6b4d8a2afdb866b764b2021acd5f46b` | `10d189384bea25ea1d28e1f24facecf396efafff380933b3bc9c1abae8b16dce` |
| `reports/deploy_lease_fix_2026-09-13.md` | `789fb8e63d8e04e6491a4885fa28714e94327cab6e547c885f5118de766ac573` | `8faac400037b905a4f565036aba35e530edf81d78fda5169b55d9af12b6838ec` |
| `reports/device_facts_ramoops_uptime_2026-09-13.txt` | `46d56696fc89baa7773618026c428e97efe4372044431261168a626bc71c56b7` | `3b764422ed4d0c1d1c69a105b8fcd89f6c1227152935f8ef41f8e2a9b5f9acb8` |
| `reports/email.md` | `5d4114e48f5908e1bd834d1d0585724df9938db9982b4dceb3a8d178a29c246f` | `a15f5122ca0a339e087f7a08f7515e813847d2886fefa794983ce0466a8abdfe` |
| `reports/gate_dma_alloc_heapname_probe_2026-09-13.txt` | `4f028649010d968eb3c2cf2cdbd93b6ef47f7935ecf0694b1fa8702b38a079b9` | `73606948102d40ed42065586b2a26a11d04e02ab047962410d8075dfa666f99d` |
| `reports/gateserviced_probe_2026-09-13.txt` | `fbf1fd867557d4d9c216999d851ea3ca7c31158c0d6f8d928dfc5e1d328895b8` | `af14d73b0e2d8d720a7634d14f7d3fd132db59a04fba12a1ff660a666f2c9e38` |
| `reports/gateserviced_ta_load_2026-09-13.txt` | `f1bc4b228cc323f8a28167dc8d5a83d717b45057b5197c2d11056a4ebdbe6007` | `b5bbe0f20f6cb26b034122a456b25237504908a18f7e84586de8b72eae61437d` |
| `reports/glasses_reboot_after_adb_unplug_2026-09-13.md` | `463f42395c317ea909b83cb40da943279aa9b24f21390739932a4949ab35cbb8` | `1f3b58a55ef7ce4bd7c28a8835604b741ff5ddae78458857a42b635881baf07f` |
| `reports/hardcode_audit.md` | `ae74c8a2a2557e1323451b8b4c27770e727fadec64ae6e96ca2b85a8afc2e107` | `b850ffc4ce542219b0fa4ce302f32d6f89f8ab4b462da4e105bc54fcc7215fbe` |
| `reports/index_resilience_verification_2026-09-16.md` | `df38eecffeebcd9401ee705f0dda600fff5ad2c85151ab127c533afe23995571` | `92dfa97acc5b13165ddddc4e427b6fcf53b77aba0dfeed3167dd41ac91e73439` |
| `reports/kernelsnitch_mm_leak_clean_boot_2026-09-13.md` | `6ec122516f4ef95005a5c0447b11d150031f4f12a6501796f5f88d5020d5cd9c` | `99399cc8b8f86cf915f89532028921b13ba084dd521b4eac2a8542257367d5e6` |
| `reports/kgsl_ioctl_retest_2026-09-13.txt` | `0f50cf1df63c41cc65875bdc16c65a3b1ae389cf40efda1beb2ce86e7089c0df` | `7d171a74f832ab2236054531e8b60cc67be2112f8792083dd8c5b16442afc38a` |
| `reports/kgsl_micronode_retest_2026-09-13.txt` | `86034163cf2f742395d3775e52dafcaaa65d8da7a0644c4acc36cb7854b2b364` | `1bede17af3f6735fa432df0d0ea612b5d1f8bc1263a71bd6bb15ae01d0442492` |
| `reports/log蒐集.md` | `72fa0eeb004ea35d50a5a4a53441df2409b03d8e947d01a6795e8cfd239292e2` | `0be614bf9c7619e82bcd9c0bfe01d839d68894f98d9aff8200f137a7a94f5295` |
| `reports/missing_binaries_inventory_2026-09-13.md` | `da911447b88444ce5c6a1a712e0a241f1a801cac75c6da9e8f777e920a4672b0` | `a84f5c69e4bb1eaae17607335b36c4255db88df84a6c1390502daff9d7adc819` |
| `reports/phone_i18n_localization_report.md` | `f471c9376573bc65e227432b80c7df28da51ae051a906ff6113710bf66d5bff3` | `166102b344e0708ca30825d601f7c3f20be019a875f310b5d6a260fa36d76771` |
| `reports/rgr06_pairing_failure_2026-09-13.md` | `57f6746f09d4cb54002429687ca92c395412df0482011f87baf83d7a8084b338` | `f50e8286cc29a569fefc1316e5d4467a05049e56a88a66bcd9fb2db9317bec90` |
| `reports/ring_capture_security_audit_2026-09-13.md` | `a75774d5aece183d7b4390796fa643ddacf88b204c156cd1aa4d01f5b92afde3` | `8d7ea75396d5f88b5ffbbb4c0e4182e40eefc8dda2d25afe4d4875c8f86d65fa` |
| `reports/ring_p0_p1_fixes_test_2026-09-13.md` | `d96ce22671a0458ef3957db0ae75299acdca11ab46ba13866758be120ac62d20` | `2439ca55456a06791eb4c618d3f163acd1e1c7d13e2d017a11b0c038494702d3` |
| `reports/ring_reachability_test_2026-09-13.txt` | `0069413c9ab989c69b2a0a73445934899bc91a6e84068eb8d0e8fdfcbb405209` | `f04fbd40f4d607afe104a665eb8a2db95aa6c17033474474816837781483eccf` |
| `reports/shizuku_integration_test_report.md` | `c7bc2ccedb48873239ee2ccc6a9e351662a13e479572ac94901218d8fc365856` | `95271867709fb37f88e90ebcb03755652b48cc8352f50676762a27c88aef9b95` |
| `reports/silent_capture_mechanism.md` | `801f5e740897af0ee472ff9a69d19766729ee84fa3bd3ef9d6c0a833efacdee4` | `2ca070b45ca3b0abab48af28d13257c7dd8c52620f565b35fadfa8c928491016` |
| `reports/static_re_gate_codes_and_rescue_2026-09-13.md` | `c0b5310feb03a4df3c9a2fc9d7e3b59a632ac0ae1c171f602eb16645a230f2a5` | `32a2b9d2d3136865d53bb4c78996008ec5bf1cee262b7f7935960b724c0d132f` |
| `reports/ui_transport_2026-09-13/README.md` | `41637e23ece690058fb7bfec8eeb71f2ff9a30645809331ca5248c42c69e1e39` | `38b9cca1b1f7721b02227216cd13fcd77722433fa7206fa5b1e5b89f290a3783` |
| `reports/ui_v11/README.md` | `dae8ba31158c20d294df40e75bc8fca0e1d7be97e9f65f1f7c8e07cd30258a7c` | `cbca59e6499b4327d47e740a4fd1e020add2c5e2639512fdceeda28c96f35822` |
| `reports/usbdebug_antidetect_module_2026-09-30.md` | `961205c5fed09dd6fffb568761c5d470755b2fdd5d7384eee80b7b74b52d9319` | `e40e92f22585fcec0b410963033663026fbf1eba6d976e1a1389502f733c45a6` |
| `reports/wifi_transfer_fix_2026-09-13.md` | `a4df74dfb1a62ce58272c94e3dd55659f71b5251816530c452203e2341f551bb` | `3550a450dc304e3531c4d2cf5d1b5f8d20ee2ee7fd91b2aedf5668c7e98c6fcf` |
| `reports/wifi_transfer_test_report.md` | `839302023c31d2a13145abaea4f0506ce51c42aa07c1112700aeeacaf8d8f447` | `c463f10ed80ce6e141e8894f7e5dfc8fe916a3463169bdbaba80d29de3b24613` |
| `reports/wireless_adb_feasibility_2026-09-20.md` | `09710da5c890d6226351ac5e276e28a74c6d1f69e64705ddbd3ca6eb4b5ada4d` | `671afec711859e129dd6be5b8fe7e14428e9b0e9ae95b0819f38534bce898f1b` |
| `reports/zero_light_desktop_test_report.md` | `9fef8ea7aefe392ed70a9576ae5bf361fa4e58909e55d0aa33e369786b199c10` | `d2e0033d0c9782aa690755e1f82112a9e249f9ddac922f90d27fc81f1687f969` |
| `reports/zero_light_desktop_ui/README.md` | `8f060d835372f147af09be6289e73e553e85b6399ee5037ad024cadb81f56292` | `e32463845265f3ca1917726c2ac9060e573f5b6819a26a8078be153c747aef41` |
| `reports/便紀錄.md` | `8aad1283c29887ef4a9c1a605ad39fca0b9350e35f395ff9745ce7db4f10ee9a` | `c6792a8a1ebc5e19d3c70c324e8da62c21fbaf016b36bdd948f0d91553a2e72d` |
| `research/shizuku/alternatives_and_ota.md` | `898e9f22ae96a343a0a334fe18745a0108c0b3dd9d55dec2df994c783b22c7d8` | `629f8bee8a3e4842c2b720f9bf285ec6d688a23e1fab515795803f5b58e5168d` |
| `scripts/check_1.19_vulns.py` | `f845225af27071808a02aa85f7343854057411ef4e0f5872a4ff465248550aa4` | `0636bf8d56152b9c1c59002eb5c2678a59bf83e6a45bc851b2a08998757eba77` |
| `scripts/check_index_state.sh` | `f1e533ffa84529504c6bc40b67b2019eae6267aaf7924d766bc5a3a383894c89` | `9b6d82af9445cbc68646443434216e71b62cf869f968b75dfc3ea71554086fd3` |
| `scripts/extract_offsets.py` | `c2bc1ba7222f90d9d4df78954108920eecd6ff6c622b32649eae871d5df0247c` | `e24cfb679407a366fe5f67424350fc2199bce0c632fe4ada45702ae5523c6347` |
| `scripts/gen_vuln_report_20260523.py` | `5a3596eda8ae776cc6e2219b8492d2da7a540705ccb1af4b7d7e5ad5c8921560` | `3b6e1c5aca82c4a16a4d08fdf75cdbd22e1472a5c7ca6d153ed2fd453bd69872` |
| `scripts/ghostlock_reboot_run.ps1` | `43422198fe5dc1b54aeaa99cad58a405fa76ceaadd47269fdac255945ae7cd00` | `5f99d4725b94536d24018ecd87d7019cb3dffdc27d65e68b19ec52b0428ec38a` |
| `scripts/rokid_ota_auth.py` | `5a70730ac12993be4204b0da30f56e4581b7740b78a9fddcc51b841f23b68363` | `995b7dfa9fc1666cd843354fe3091c6037c50ee449ef581fc2318fb792e914b6` |
| `scripts/rokid_ota_check.py` | `e1b16842afb796dca9b9ddb3fa05e20175142d0c8d2c0c992d7efda17003b4b1` | `fea833cddced15d690486da54732a673e8c3418277a7f1d2a8c7adea36500ca0` |
| `scripts/run_led_orchestrator.sh` | `e2c377987535e121989e18760b26d80db97309f3dea6ec6e80e96c217e2a1089` | `56a1d5d64228c0b68f4cce98debc9d8f111d03152a429555c183855b58c2eb18` |
| `scripts/run_pathd_orchestrator.sh` | `ab6b3d068355f5d6682a092a43acb0ee12afe035067085edd4e7079fb55219ce` | `b4bfbb26b735d4709d5692032fee49e431164b2a52d7370502b5c3c6bce5e577` |
| `scripts/run_v11.ps1` | `8bc5ba33bbe7fcf8dc211f5ac9966da3ef1d112f90341041ec209ffafe9c4ac1` | `0fe4383d4431a7c174e18efa2d63bfe258d96439046e87a33ceae3ead115dc0f` |
| `scripts/scan_kernel_phys_load.sh` | `8f61b78909f53cdc9b330819427da7d30daf2008e203e2ff94308a68a6c85f3b` | `194bc3d8cf4770e09d3ca3a69794a30526eb5e8db4b27b41dd729fb8cd147fee` |
| `scripts/verify_index_resilience.sh` | `9eb61e560a5be68bbaf8ddf3c7a5b36d96d609a3aec203feca0262df36df9fa4` | `fce922c8f3e4dab2bdb6602f03ecba70f4d03b9bc8c85a43a3f6b4c5058247dd` |
| `source_code/ashmem_overflow_exploit.c` | `820dbb9d02acbba6f54434ffc3a5ef652cd6a14b781cd8d6d8fc61e757f7f204` | `797b8060cb34d64bdadd5073f47f2ff2e951104ed0a12b29e49ee6b31f644e4b` |
| `source_code/ashmem_probe.c` | `54e8c22aedceeb2e60fe19257429e9d145abcd09574ef60cda6f548ec9818075` | `40ce755ba796dafd7eeff9db48c71cdb375e7e12f28ad115fc7b20f726afe27b` |
| `source_code/binder_probe.c` | `ec437b8734f88f1793cba5e7a1200d5c4281b62a9a7442c2a097ece3498f1f6f` | `0bf5c5253a282c3cec0285e999bedfc87585ae2494198194cafa90a87672cc4b` |
| `source_code/ghostlock_rv101/Makefile` | `23675d7de3f218fd1ecd3d28f55714622b548b88b62fa4990fe516ad56ef678e` | `bdb870fdcc4e998150459f457020255fbe5ffa1c537c514f9280c7a5ff810617` |
| `source_code/slabprobe/Makefile` | `e32c0fa3fb4b8045f52fc1a545780c6ae5faa5f06758b035404686f0b488d1e8` | `f9ddab4ed806ff65a3b642949740584d96abaf18a20d1ed61760cc8708e77f63` |

## 五、驗證方式

驗證用的原始樣式清單**刻意不寫在本檔**（否則本檔就成了外洩來源）。
請改用下列通用檢查，它不需要知道原值：

```bash
cd rokid-research

# 1. 佔位符應存在，代表遮蔽確實套用過
grep -rl '<DEVICE_SERIAL>\|<REDACTED_' --include='*' . | wc -l

# 2. 不應再有 32 位十六進位字面值（本專案僅憑證與雜湊長這樣；雜湊集中於
#    SHA256SUMS.txt 與本檔的 64 位欄位，故以 32 位精確比對）
grep -rEn '\b[0-9A-F]{32}\b' --exclude=SHA256SUMS.txt --exclude=REDACTION_LOG.md . | head

# 3. 完整樣式清單請至工作區外讀取（不納版控）
ls ~/rokid_root_unredacted_backup/    # 遮蔽前原始檔
```

## 六、後續建議（不在本次範圍）

下列憑證係自裝置或 Rokid App 取得，**非本專案所有**；本 repo 已遮蔽，但憑證本身仍然有效：

- Rokid OTA API `key`／`secret`／`deviceTypeId`（裝置 system prop `ro.boot.*`）
- Google Maps API key
- Alipay 付款 AppKey／AppId／`signV2` 簽章

建議由 Rokid 端輪替或撤銷。相關風險已在 `reports/EXPLOITATION_REPORT.md` 中標示。

## 七、未遮蔽但已評估的項目

| 項目 | 處置 | 理由 |
|---|---|---|
| 手機型號 `21091116UG`（Redmi） | 保留 | 量產機型代號，非唯一識別 |
| 韌體版本 `1.22.009-20260710-150201` | 保留 | 研究核心事實，`FACTS.md` 據此判定漏洞狀態 |
| 公開第三方聯絡 `security@rokid.com`、`service@zeroday.hitcon.org` | 保留 | 官方通報信箱，非個人 |
| 廠商聯絡人 `degang.xu@rokid.com` | 保留 | 見下方說明 |
| 周邊 MAC（D-Link／Gemtek／Xiaomi 等） | 保留 | 屬第三方廠商 OUI；如需一併遮蔽請告知 |

## 八、關於廠商聯絡人

`degang.xu@rokid.com` 出現在通報往來紀錄中。你先前指示「署名聯絡資訊一併遮蔽」，
但**你的**署名已遮蔽；此位為 Rokid 方處理本案的對外窗口，屬公務聯絡身分，
且移除後會使 `RV101_Disclosure/11_Vendor_Communication/` 的往來紀錄失去可追溯性。
因此予以保留。若你要一併遮蔽，告訴我即可（會一併更新相關 SHA-256 紀錄）。
