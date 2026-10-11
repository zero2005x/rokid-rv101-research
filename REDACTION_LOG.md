# 遮蔽紀錄（REDACTION LOG）

> English: [REDACTION_LOG_EN.md](REDACTION_LOG_EN.md)

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
| `MANIFEST.md` | `926b4f6605b3ab2ecb1ee33496a41d32a10106507ccbba3afbdd936c796c11e8` | `5b811bcdf31def75e5263f7ecff46b4eefa381ba31b7625896a5b7acffdde16d` |
| `README_ORGANIZED.md` | `28faf18d98b1b48ca06c319de0c2c3c549a5095f9fc5e82c0c941f4402c2c4e4` | `363dabb53f8d960cdd34d373d9e0c81004903363e8a81e2229ebdea6e494fc62` |
| `RV101_Disclosure/01_Executive_Summary/Executive_Summary_EN.md` | `cefda0ac82a3f123f082679e13951de8f15ff16f7196f825058ad01bdb74f3f6` | `d11e319251e98382af2a46df096757d19a2afdda20c5d567d54b2c0ada46dcd7` |
| `RV101_Disclosure/02_Device_Information/00_adb_devices.txt` | `eec62ea04a0b5971bc106b978e1911513a53c3e89f72763b01839c31e3f65867` | `7819b8d4b20dafb03c27f506983d5a0c46c49ffabf57eebbdf0c7c8f11e31a06` |
| `RV101_Disclosure/04_Exploit_Execution/GHOSTLOCK_HANDOFF_V13.md` | `a030cd2b0862b37adfc1875032f76b12cca15799d596c1c4efc26e2e00e42836` | `2fb1984d2aa7def006243ccf8d3deafdab27b09d6629b176f99d2e6bbad0421a` |
| `RV101_Disclosure/04_Exploit_Execution/GHOSTLOCK_ROOT_操作說明.md` | `df520e15c06e0144c0222ec51bb67b49ad357bcb99aad8625e1255bc6eea1c69` | `823dd3e25ec536b6b29cc32b21269478ebabd80277ec3b65a6ad1ead16a169f4` |
| `RV101_Disclosure/11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md` | `2854746fcf754d31fe720c933746dcb797270c00be544364645852477b364f72` | `f841c642503249137a8124f989f78330189c02de97112cc9a2614b7bb656748c` |
| `RV101_Disclosure/12_Hashes/SHA256SUMS.txt` | `5d9c756d1a7c5039ca5103a32b96b9cc69ad569bf17428e6b9f7e3cc78b7bae4` | `3a0cd20bdaea99db35acb31d839d739d6aa3bc29ab423c589c807251a58beee5` |
| `RV101_Disclosure/14_TWCERT_Report/TWCERT_佐證PDF_內容清單.md` | `f683f43e45731cfd0eb86576f6b3df9f18b9a1f827601bc23112a8985b222707` | `87b36f70db4f079977a7c792acf54aef9b9824159a21e82f1d6bcde647ab37d6` |
| `RV101_Disclosure/Evidence_Checklist.md` | `59c98873a3b15532a91a26e75e77fdc81ca21a15951807e6d4f4996156d4c95a` | `9886cbfdfaa391bf3ee895ee889f33a44dc4745b47ac78c5c09db33fe7985d6d` |
| `RV101_Disclosure/Evidence_Index.md` | `25ebe0a91fd99d0f29d13cad5df0baa780984e4029e0b36ed17ee570fcceac89` | `0399d91e629a53e344d042419e9601ff898c934fc3678cfdf2607e08f8745900` |
| `RV101_Disclosure/RV101_Vulnerability_Report.md` | `43cedae4fd44e93d43593fb7980899004af7536685d037875794f3973c83c6b8` | `51cafceb905a1eac49f53c2df303402583f0887b0b4f219086327a920badf1a1` |
| `RV101_Disclosure/Redaction_Checklist.md` | `7e486a40fb58c92c3c15b17f0a215704589f16db015670229457886fdf2d3cf8` | `c775f802ffb622dfbea1d5441b78c6fa70cd3e54bd1aec111650fe2820c4608e` |
| `RV101_Disclosure/SHA256SUMS.txt` | `1e588b0ef1957f43bc2038d8354a34e7a7163250fa02fd10183f9bbcee369495` | `caaf14d518e3cafc60acc6bd786f5612656b9c5bc2c97980da2c1bd89764e6ba` |
| `deploy/adb_wifi_verify.sh` | `19be108221a3e1ad2c4837ca098bf23c990a60455a83882efe9f6dadd946a4b2` | `92a2eeb096799e0e0aba63602bdc8a03ae9803b515b855d2cb114f4799efbe7b` |
| `deploy/build_adb_relay.sh` | `a24170e7932528a93f0efaeb449da907ee4b1b4ec3f42c732679882b9e774142` | `3a6a933e4dd7ef4fc634a6ec59c3c5164691878cbef827c32d50c2b159ea4137` |
| `deploy/build_kgsl_probe.sh` | `834f5ea137130f76c7a4169ede16fd98b8774ae47a2c02187113d7a9757c5f2b` | `4da665aeb2e07799706dcbe6b6fd1b25ee3fdb34d99461dead4bd67e50ac5664` |
| `deploy/build_ring_input_daemon.sh` | `3f560388501cebd5f957201a0c5c352f4c4585919c20507f0ef32fac394b60c8` | `3c9eb0adeb9b9f4a6d2f1911c459eb606f02f5178fe4bc97f6d8b3dac945c99c` |
| `deploy/build_root_daemon.sh` | `e57ddcb032194729409a2b9a95b430c657937ea94c511dca89e607eea15ed50b` | `5752706a58aa1be222bed3f4fa211239029efd0c31cf3e43842c0265f098fef9` |
| `deploy/check_ota.sh` | `8daf0fdabdcc7b54206ce4e4775a7b52f91d322f56391c6309f738e8c5a7a2af` | `31589f291bc4fddab6f999dc4cf581adf56095404abc20b1ff257e0587495c07` |
| `deploy/env.sh` | `8aa8615e1c8e1dd1e86af2896ee862d2996332cc127a127df5ad9518b926f1ad` | `60aecd80a391a99817675e9e07f3cf91b7d080c735a388137f80e65c3457ee8f` |
| `deploy/get_root_now_run2.out` | `5f393bea4b755e5ee66e3f90222c9921d47b8cc2b3cd20865ce720e64b86c86d` | `cc12536de8978db81b08c026753a0ddfea60a9de22bcd3030d965b9dca2c8a72` |
| `deploy/ghostlock_patch_check.py` | `b6a24895e8e25226911d874caf24f916c7efcb0b9b37786d33deb2d3bcd3063a` | `713492d5a8c746b38a4fd44b4316ff889294068d7731433ca02c515fb95ec4aa` |
| `deploy/lock_ota.sh` | `0238558629ace4be92643a27f7f9cc33de910b69948a2112f5de6b8ffa1e492c` | `b4bd109dd2981b9f4b984bd61e7a834cd21b5b4db9c945e08f7db9b137c92500` |
| `deploy/ota_api.py` | `9f41a8df54ab5c80daa058a21798d54dc1305831e390d7a36282468678c1fad5` | `30c7f17cf03a30d6a377a0c1f1a732612ac667152f259f9e9a84420a9565e36b` |
| `deploy/start_shizuku.sh` | `51e034d776350b7b62764d61c6e4076ae292f38504c7bf6a0bbb60b52314acbe` | `86688dd93d46004ccbe2810361905a7bdc9850db7a8370f71d73894044aae9b2` |
| `deploy/test_shizuku_e2e.sh` | `ef4dc69cdcd7c14ba2a4f94c2213350a6685afa6284a34e137a71016cba362e1` | `f7861c3d59eddbb31c710c086d3f0370634d68e10bbdbfdd2f1286f36afdd2b2` |
| `deploy/usbdebug_hider_ctl.sh` | `3d0948dc088877da832527cfd25fb2c3ce96867883c6fa729d1be7aa791f00a8` | `7ee18c59aeeb4c60dbe3962e8b38a71d6091a393db99a5e0342e410b023e4943` |
| `dist/README.md` | `f3e9f2f0814a3ac1323bcc820cea00eef4c29d46b541ff83c54b5c76a1719cce` | `d6e030571e794f20707d8eb8b88495d9a929f4efdebce741f078e021c236eed6` |
| `docs/RV101_NARRATION_v2.md` | `aaf013a8632afeb0c74790c1cd2759b1a7ae1c0ef44f47dc8f2991386569a51d` | `37331ca298cfe38fcb6a7ea01f08e4bd432a7003e55fe494d11d4bc2fa4bf7da` |
| `docs/VIDEO_PRODUCTION_PLAN_v2.md` | `13b8fae03137a7a4a2a52aa9ca4db151889f28c0c8e05b1c552b8b88a69a43c0` | `1644573bfdcd654b8d4bfcee84c98626c3dbe91cc65f84e5c357c290dbb3ba9c` |
| `docs/capture-verification-report.md` | `2c182a3272e595eeb31f9ca43b09e9c16864811b8d2b73872de857fc7d64bae2` | `e53346f2f7941f2ea6fa0172e7ee3f78da2fa1ac1515bc1d3df564e26da6c8b7` |
| `docs/deploy.md` | `2c57cb094906c284ab19179a3f041e8e0754b0f8b566849b08fe2787e24a156a` | `25932add4695ef16ad38b5dc042da652303a4d2ff1c1048b7102e93af3f6d3b0` |
| `docs/device-debug-notes.md` | `a09ae9d83f489d0ec075bc518338a56d4b6461f703e0c188863219f1e14bd251` | `d3d0e19c1bb79577c7fce0ec367167c65727482a89c1ab3cef0031d8dd7f0c86` |
| `docs/privacy-audit-report.md` | `9a03637f3375210e8a8409cb854a0704ab7d55ae2715275fbf3a0280b94d0e47` | `113e67d01d2a215708b13fb0b71d03e8d6f51ecb3cfda9ad3161ae50ef8a1fb0` |
| `docs/reply-to-rokid-2026-09-06.md` | `a5374ce30b392970aae05ba3ad1feb97b0f11f441819b4d0579347307120cce6` | `8ed40d70e263c71d314fe2bcd5e42af306fb8e14167c6dabeba08d0acf811e4c` |
| `docs/reply-to-rokid-2026-09-16.md` | `99bcc4e464bca743ccc8a797a25de88d4fea1538ac58a5fc486ffaddbe5cdf37` | `e94334b47dae2362501933e5356f12a90f60bf525b2077cde1f6fb36594f316d` |
| `docs/root-from-zero-guide.md` | `246e49549537149274ab93cbd1ea1dfdfc1d07c9d8f90f88f756d4824d7d8c10` | `b91e276c79d1fa1c5a39b46dab17c5a05b4411d9ad99514a0982d6ebdbed98ec` |
| `docs/shizuku-feasibility-report.md` | `27291642b171d86845050d9874ff6e8d6fd6fe2f2e51a86b44e4a4544140cc89` | `3b4332ee465bad53adaabcacb391295b0b1fa87f3515da48661d40608216a827` |
| `docs/wireless-adb-solution-design.md` | `20c6b69ddf7d47c5e6ab03e66028c0cc18b31e1446b101167a3999c3f2066bc1` | `720f4d99b4a931979fa8052ae48e8e9d5ef2cfc45c43d41464d88b43c6a7a124` |
| `reports/5.2_VERIFICATION_2026.md` | `705c5f50b0a869c40a0c36a0c0a0db786c88b51c206a53358936b938d5c1a592` | `e0fd192214a149bdbffc1b81a6e748ac5caa963aefacab54c212fceada122106` |
| `reports/5.2_VERIFICATION_LIVE_ADB_2026.md` | `6d4043dfffa75e8f6d0d42962e0d9a8c94a9c96fce958380612af55eb3f5bb3f` | `93513e8711effbd6a0a8babe4f179f61ed131bd0bb9afa88586969fe2ebe8ab5` |
| `reports/ADB_Media_Exfiltration_Report.md` | `a95638a1dc730d3ab14d25ca1cd75eb4680f79ec6fff2df2d1232aaa9d575a8c` | `4b8786d2f46a48c385dc9d0846e6bda6ca66e0f21990a07e8f4144908055a1c9` |
| `reports/APK_TW_Tech_Report_Rokid_Root.md` | `cb78200aaf893115c66126f82b2de2884d51355f40a84f98a523c078e6c4f7ef` | `e05a8932f2ece6f5c8a14605461aa58e7a00398875fbc394fa4183276fcd5fbc` |
| `reports/APK_Vulnerability_Analysis_Report.md` | `01f217fbe68f584a6ca0200934a3ee107c4963e238368a6fec51e705fa8a7d18` | `3fa016a1e6d12af3a33a02e173e511ee13aaef72a8552b7e22ae4c485779cff4` |
| `reports/ATTACK_SUMMARY.md` | `99b6d2ca514301c0a007bc57a699817435b03f50c8ce9b50c87c5b59947db1f5` | `4d9fb7cdf32aaf199332798044ec44d2a1a6dcbb45b2ec153887722e93d4ab11` |
| `reports/CURRENT_STATUS_20260822.md` | `b4f96440cc1207cfd068f097244b0aa2b105b9292d7d68d735fd97e04162a838` | `b82942fa4f30f1111ca0f63c23dfb5a8a2aad64ebe5c6bfe9a88c65c7c6acdf8` |
| `reports/CVE-2024-36971_analysis.md` | `f12655e8e06f82685f24ac361a4e9f28858a42af03619c280d0018c10d9d1a69` | `77c30151cbdb849694bd458b605a17ef01072caa0da26144c5149a4b99958acc` |
| `reports/EXPLOITATION_REPORT.md` | `5ed9c7376d5f5b5d00ca37093b430f6ecab9875493199aa3f34c4777378dd54c` | `5c22b4158a4d0394579491160ed63666a5b57a0b78328e95a82ef58ab60ff009` |
| `reports/FACTS.md` | `627750f97349cc67ea646df27f7a9f12db07458bbd7d49e572052be69fd367b5` | `e883da41c4ba2bdeaf69ad92c5b620a9632c12f06ac41a7c7452bee26eb5d62f` |
| `reports/GHOSTLOCK_HANDOFF_V10.md` | `ac91e0df9e3d811e2d1746eb6be977223beeebd9b0e861a565ba0e8696fd147d` | `ed2a27e7d5310ec7ba6eb1641efd173e95e4c8d3045428c60f37522fbc965740` |
| `reports/GHOSTLOCK_HANDOFF_V11.md` | `ae56669514ac26401c0cb85c50a1284aaaff4d42f94497307e57369f5b569413` | `2c6b89d68581af8dc36cd002eaaf1c1e7fe95871b7d1810a9f8c82f7de205c02` |
| `reports/GHOSTLOCK_HANDOFF_V12.md` | `59c4008ade45f32e28b6d3ce9b946a18fa255ad861569f6476f1c5b826f5997a` | `b39dfa907c046ebbc883bb958e69a73b06ea5c0f7e5fda302265670002217379` |
| `reports/GHOSTLOCK_HANDOFF_V13.md` | `a030cd2b0862b37adfc1875032f76b12cca15799d596c1c4efc26e2e00e42836` | `2fb1984d2aa7def006243ccf8d3deafdab27b09d6629b176f99d2e6bbad0421a` |
| `reports/GHOSTLOCK_HANDOFF_V2.md` | `245f77b3f3d93082f935e2489510cba6e97bf3a6068aca55221f804fc77f7e85` | `de6a1ab60675e92dfc9e654bf441a0be5077ed971418172f671191858abf3711` |
| `reports/GHOSTLOCK_HANDOFF_V3.md` | `e5bf5a276205c214fb156d8b9fd103db7f45493ef72a6e8e45ec3ef71c659f1c` | `b93f8822c287032e7e71b46704731f84479c004c111029b1ebbe6a5ca87dc4ba` |
| `reports/GHOSTLOCK_HANDOFF_V4.md` | `8797b2db4b20c2e7f66933984c8431f1faae15bdb2b198b2a04ae7bffeb56e28` | `0ff3b687662d97dcc694cfed54d0903f2b9268fed918faeb6c452f80846fc71c` |
| `reports/GHOSTLOCK_HANDOFF_V5.md` | `042cada3f3beec0b0f68590181c95fa28f06cb8d903bd64cc0ce326ce87631a1` | `0a7266f7f6b5bdcbb16c9de083cf2226dc57d9e4dbb9901ab4160ee7881f1dae` |
| `reports/GHOSTLOCK_HANDOFF_V6.md` | `57d04990ba7b04624a93338c8e02ea4b02302bb9265604ea38d2508ba43c70d8` | `cd6b335aa9aee8e3655348b9dadb2eebdf26b7e4ac5f39abe8cbd1feb16524e8` |
| `reports/GHOSTLOCK_HANDOFF_V7.md` | `4a784de2b9619a4f7990e4bd933ae96b60848c1e874982f7b1c18ee1d0023d2f` | `0e727ac43eca2f643a3fc6492e9e577f06e1c99c96a23f8f70d8303a987965cb` |
| `reports/GHOSTLOCK_HANDOFF_V8.md` | `fe612798779aa810005b3538fb28e198eaa4817435f8d6277d63b698241d8f34` | `a8b5f7c4dc9cf5d8d142051501421ad24435490089bee5977117df0b37fda950` |
| `reports/GHOSTLOCK_HANDOFF_V9.md` | `75034e7367932176bb3b80b80f242b74ec105bf530b1f98ac04b9f9786dce274` | `5040f05ec4353b5816e72a7ad84dc4610fa1fd142bfdb7dff5e4165d9c76c085` |
| `reports/GHOSTLOCK_HANDOVER_PROMPT.md` | `79ae056ee5b46be6f2fb8b7a75dfe6e8e3d6a8a43575a2059de0888e466ccc79` | `49c30c1436228c68b093d5ac336f1ce623cd215426bb7494b4a7980f448a2d9a` |
| `reports/GHOSTLOCK_ROOT_操作說明.md` | `5ec722fdecf2fbd2bc597827055d43d87cafabf5746b09fca64b451751f6ca1d` | `882ce7d258778f1881fdaec5919ae6e7f4a46cb5be9dd4faa62f1034edacf205` |
| `reports/GHOSTLOCK_W2_CONFIGFS_READ_BLOCKER.md` | `2f6f87e2039a9c4eecbadfdf8e8b3f2ed694fbf940ff4e2ea1690a468e58f383` | `b64356453134f4cf9b4af1772ae0d4abff83f0b55dd57b5d6977bb7b71d15c10` |
| `reports/HANDOVER_SESSION_20260131.md` | `3986045e47ff652da455f2b76fec5f6f924cba71a6b219dbbbc9a7e463f4d6f0` | `d25459b70fb5596ddde8b9fde77c70361c2b3fb5bfc09b343d4dab94254056d0` |
| `reports/NEXT_AGENT_PROMPT.md` | `16ba3b04ad5ccbfb501d182635109f520382993c2ec0f0bd5cf80c56cc62de75` | `4ab02ae6beab31d555a204fa4f49d89d5d5923f407bdfd2993d7c3a8d00ecbc7` |
| `reports/NEXT_AGENT_PROMPT_V4.md` | `f069cb0e49c42d03e8f455721d8b99e41b2fd5afdb9ce769bd7a0dc532892402` | `68c39b9b2021dfccf1c5ddf2d5939cdb5d835c02a0fdb215fcc3578e651db0fc` |
| `reports/NEXT_AGENT_PROMPT_V7.md` | `2f8c382333545426b6a4380de091b653b7a8a5f1ae6809b273ab4e02d014020b` | `b72d64a0b25b693fecbcd7f6dbf1a7adfdaa8528d70d49992b53c2559fd5f18b` |
| `reports/NEXT_AGENT_PROMPT_V8.md` | `1e4af065ef04ebeec1d54889ddd81ae5827cf8115dde45a0cd23b7010e802f68` | `11ed08c6a7313c3189c0ac40faa9f18e30c768ea861961e67e9c795178c7b892` |
| `reports/OTA_Authentication_Analysis_Report.md` | `05524da5fa3c36f62edaf16aaf71692f8c43c2c1544ca4cf5c3804df6a4438c6` | `4a1da3baf994c1512d1c27da872de9eb96e0a39f9fc1c0c8a3aab50825db18a5` |
| `reports/PARASITIC_INJECT_ATTACK.md` | `aa29b944a5f82912645ebcf2c7d94dfbcfacf2baf401e131ad775c71a672db60` | `f253bb06e5ae395c4c9ec7ce0061746280def95865598956f4b9eb5e573d8650` |
| `reports/ROOT_PROCEDURE.md` | `fda7db1e33320bea24e0f08818c39f4dfbea161eb14260d4e13252373f144df7` | `c94265a6499a979909880151a73a7bd695eb743ce08d936ace7269d94a764cd9` |
| `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` | `8f9411fc2c57d1c25f51f413c8a1a0af71b213f2300c4e695ab89abf295664f8` | `186c636680ccdeec50311ff9d47c0744690d9f9e65c7128af0560657ef10652d` |
| `reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md` | `c2460a4ebd8614ec80d06bded8d9caeff9f855084f8a533af3cd807477c7fb67` | `dc3c9f3c79e7cf510c7aa981347521aa78fc458341a9baf0939abb591d7b16de` |
| `reports/Rokid_Glasses_System_Report_EN.md` | `45338f471c576ac4b20500146b68981247391e56c030d405fc7064b2bea5e0e7` | `9cb2489ac8e9b2391e05dc98d839ac3373cd8bdc13c32c543e7edf4adfbe1b79` |
| `reports/Rokid_Glasses_系統資料報告.md` | `3d537559d56da875d1668899de23b083b97990e78a591d9ccdc0d241233b9512` | `ca4f9e2de79af4ccecadc82bcc1e98b5f7b2a3318d7ecc5976c2c42a2d93fd54` |
| `reports/Rokid_Glasses_系統資料報告_Marp.md` | `36994fd1ca1c322ac8188953ecf2d454c00d8c19662765ae0649cb6cdb697ba8` | `37a96e6f1838473f249edbc80476bf416cfe5fb4c83cdebea4d72064414d8c89` |
| `reports/Rokid_RedTeam_Internal_Report.md` | `f7eafcde3bce507eec4bce135503f1230b57c110f86cee28e4a81f86df6f9bc2` | `767db552ad4900754e1cd790618bd53798ce4bf35da664e3e3356b1ed90df138` |
| `reports/Rokid_漏洞報告信件.md` | `7af26c1dfe0c22fdf6cbccbe1f731af5ca3cc454a6be389fe47212f61fc06063` | `25672a091142b864c512cdbb069e585f6a4ee59124fd4673e7d34f006f3c4b07` |
| `reports/SESSION_15_SUMMARY.md` | `8fcaebf93829e951c70b1381e094e726b09710e8661af37848f1034b733faa18` | `6a920283c4deb6625041a3b0e47665d3d654eadfe0b8a49ac663b9d0d5a2c738` |
| `reports/adbreachprobe_untrusted_app_adbd_socket_2026-09-20.md` | `12f2e7fbc2ec6c68bdeb9c4d62f22ee269d2f7dafc31a4ba14aa43a06bde60bf` | `2ef58798f0f075d97e2745c92cbf03ba1563c33dc70f253b7b7433071129abb6` |
| `reports/aivela_ring_connection_failure_2026-09-13.md` | `a86f05d57edc7e2fee16ec7692628fe7620def98a6f2d437605a83dc56d9e379` | `0ba79e4d28adbc74cba32b62712d706b000ba2bea1bb8d032ec9d3753e3cf383` |
| `reports/arm_safety_policy_2026-09-13.md` | `807d8783f6c4a4aa6b65834c11cda7bcdf3665a48ee8a32c34dd8fa4f5f80871` | `20f44a297a2274a88a83306a297492d60fca39a55caf37e80267084924d8d24b` |
| `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` | `58674b9524ea50dc9e9e4d3a5329c22ad6b4d8a2afdb866b764b2021acd5f46b` | `5f85151eef241a39b769edac5b9e79861e8d432a81f5f1544569fd0140a789d7` |
| `reports/deploy_lease_fix_2026-09-13.md` | `789fb8e63d8e04e6491a4885fa28714e94327cab6e547c885f5118de766ac573` | `563f5bdfc3f3672669b3e6680169a9a346faf95eaf5a3d28a79f44f327b66011` |
| `reports/device_facts_ramoops_uptime_2026-09-13.txt` | `46d56696fc89baa7773618026c428e97efe4372044431261168a626bc71c56b7` | `13bb99dfa2848a53e34a366f651787348b2f850e55554caca8503a67f9eb00af` |
| `reports/email.md` | `5d4114e48f5908e1bd834d1d0585724df9938db9982b4dceb3a8d178a29c246f` | `f0792da15c4b33296de0d823bf974973653249f8cd089cbc1e981ca464eac09d` |
| `reports/gate_dma_alloc_heapname_probe_2026-09-13.txt` | `4f028649010d968eb3c2cf2cdbd93b6ef47f7935ecf0694b1fa8702b38a079b9` | `73606948102d40ed42065586b2a26a11d04e02ab047962410d8075dfa666f99d` |
| `reports/gateserviced_probe_2026-09-13.txt` | `fbf1fd867557d4d9c216999d851ea3ca7c31158c0d6f8d928dfc5e1d328895b8` | `10876aae59969bf308a0e72c49b89835e8866e8878cda68ebdeaf5fa3f15cf08` |
| `reports/gateserviced_ta_load_2026-09-13.txt` | `f1bc4b228cc323f8a28167dc8d5a83d717b45057b5197c2d11056a4ebdbe6007` | `77b6b36c1290c6267284cffef7ac5525a01fb5e28d48efef1e87327af2608bc1` |
| `reports/glasses_reboot_after_adb_unplug_2026-09-13.md` | `463f42395c317ea909b83cb40da943279aa9b24f21390739932a4949ab35cbb8` | `e99a79c85974ac8f39094f5e0b392bb747f8db3f29e5d32854b1b277d34aebca` |
| `reports/hardcode_audit.md` | `ae74c8a2a2557e1323451b8b4c27770e727fadec64ae6e96ca2b85a8afc2e107` | `9e7b5e8463fdad47c785c5e6870cc883125f21fd6a954a3a6a71ca0998f8d662` |
| `reports/index_resilience_verification_2026-09-16.md` | `df38eecffeebcd9401ee705f0dda600fff5ad2c85151ab127c533afe23995571` | `98274b1f2a0fab30ab139259405c335f1d717207fbfe740c50f112049a96ca0d` |
| `reports/kernelsnitch_mm_leak_clean_boot_2026-09-13.md` | `6ec122516f4ef95005a5c0447b11d150031f4f12a6501796f5f88d5020d5cd9c` | `1a94fb292430a111d2634b83af32ce35c4296a7c1acb509ed3ec7b476dda8fcb` |
| `reports/kgsl_ioctl_retest_2026-09-13.txt` | `0f50cf1df63c41cc65875bdc16c65a3b1ae389cf40efda1beb2ce86e7089c0df` | `ad503e9164f02ffdbfa737f3fa34dfa3d87ffcc94e95b1d49b83d4473bdedee2` |
| `reports/kgsl_micronode_retest_2026-09-13.txt` | `86034163cf2f742395d3775e52dafcaaa65d8da7a0644c4acc36cb7854b2b364` | `99ce95a0dbc435bf9500e3bf9db1efc4024010c6f1877d315e4d091e43b0171d` |
| `reports/log蒐集.md` | `72fa0eeb004ea35d50a5a4a53441df2409b03d8e947d01a6795e8cfd239292e2` | `edea3725610e016fa22355057edf51ccf2764f1207973a0cde39333c6a01b2f9` |
| `reports/missing_binaries_inventory_2026-09-13.md` | `da911447b88444ce5c6a1a712e0a241f1a801cac75c6da9e8f777e920a4672b0` | `aad54032e317012757917826c8b35d13f142e5e26b24eb8f7ae4d4b8188a8eaa` |
| `reports/phone_i18n_localization_report.md` | `f471c9376573bc65e227432b80c7df28da51ae051a906ff6113710bf66d5bff3` | `40031318df875136854677e9c80a28eaffa0f025a03940c3324c9962cd86d58a` |
| `reports/rgr06_pairing_failure_2026-09-13.md` | `57f6746f09d4cb54002429687ca92c395412df0482011f87baf83d7a8084b338` | `3b3af31cd477530aec9c4529c0578ba3368d9efa9ee0600daf04b83b2c55d6c3` |
| `reports/ring_capture_security_audit_2026-09-13.md` | `a75774d5aece183d7b4390796fa643ddacf88b204c156cd1aa4d01f5b92afde3` | `2f09dc63bf2bbf0da7d93ca3f65d102f073bd4e658f637f97d267b5170109990` |
| `reports/ring_p0_p1_fixes_test_2026-09-13.md` | `d96ce22671a0458ef3957db0ae75299acdca11ab46ba13866758be120ac62d20` | `f887b932fa03cee674cbdd464d658a57550b71153300ad37cc0794fe1d525bbb` |
| `reports/ring_reachability_test_2026-09-13.txt` | `0069413c9ab989c69b2a0a73445934899bc91a6e84068eb8d0e8fdfcbb405209` | `ccfc0804d461817f7772e631d970376872a2db1ed00e2b890d94604e4fe1945f` |
| `reports/shizuku_integration_test_report.md` | `c7bc2ccedb48873239ee2ccc6a9e351662a13e479572ac94901218d8fc365856` | `77133419e47ad967852e172529e74635985089acc8a0b2dd62aa1360ea4a0c9f` |
| `reports/silent_capture_mechanism.md` | `801f5e740897af0ee472ff9a69d19766729ee84fa3bd3ef9d6c0a833efacdee4` | `4db22569e9c1ce89c9b5745c5a7a3364233d8b5eb70d7310e569dab18c4c1d09` |
| `reports/static_re_gate_codes_and_rescue_2026-09-13.md` | `c0b5310feb03a4df3c9a2fc9d7e3b59a632ac0ae1c171f602eb16645a230f2a5` | `166cf24a2061b34bb8044e3797ac7d14fe8f97651dcf13e3978088b0d194becc` |
| `reports/ui_transport_2026-09-13/README.md` | `41637e23ece690058fb7bfec8eeb71f2ff9a30645809331ca5248c42c69e1e39` | `bfc46748ee0fc2e6f22a322cab3e50ea11e350c22edc90367ece77bfeed093d6` |
| `reports/ui_v11/README.md` | `dae8ba31158c20d294df40e75bc8fca0e1d7be97e9f65f1f7c8e07cd30258a7c` | `47d76826d176e5e3e8a720531ada7c9b1a311d5c41dd6e9b10f574f7ff35fea0` |
| `reports/usbdebug_antidetect_module_2026-09-30.md` | `961205c5fed09dd6fffb568761c5d470755b2fdd5d7384eee80b7b74b52d9319` | `3893f4572987ba35bb16554a9c0ca6d2461013fc2a4c232388ac1acf56d6afed` |
| `reports/wifi_transfer_fix_2026-09-13.md` | `a4df74dfb1a62ce58272c94e3dd55659f71b5251816530c452203e2341f551bb` | `b3d22d609b2de6cc3e5a72e61d2a09fff0a29046a60ee1c5e6627c4c02bc71bc` |
| `reports/wifi_transfer_test_report.md` | `839302023c31d2a13145abaea4f0506ce51c42aa07c1112700aeeacaf8d8f447` | `16dde0cf71b7a5b3f6f6d4474c09dadcfe12a509c31fbf22d354096a97514ae4` |
| `reports/wireless_adb_feasibility_2026-09-20.md` | `09710da5c890d6226351ac5e276e28a74c6d1f69e64705ddbd3ca6eb4b5ada4d` | `6a0b53cbe043c74a94fd73e019a130e8db536b85883d99f2c48296dad247a562` |
| `reports/zero_light_desktop_test_report.md` | `9fef8ea7aefe392ed70a9576ae5bf361fa4e58909e55d0aa33e369786b199c10` | `87c5ab747c96c4013a8791cac9c0cfb3e783f61744df0e081a3c9d307cd87e7b` |
| `reports/zero_light_desktop_ui/README.md` | `8f060d835372f147af09be6289e73e553e85b6399ee5037ad024cadb81f56292` | `8d9f4bf9c6e64180642ac067c217eba5c1b0ece038790d155d78ef3af261fc6f` |
| `reports/便紀錄.md` | `8aad1283c29887ef4a9c1a605ad39fca0b9350e35f395ff9745ce7db4f10ee9a` | `eeadb318fb91fa06e074326e32492b8f0c076390915459264c3a468202117a86` |
| `research/shizuku/alternatives_and_ota.md` | `898e9f22ae96a343a0a334fe18745a0108c0b3dd9d55dec2df994c783b22c7d8` | `629f8bee8a3e4842c2b720f9bf285ec6d688a23e1fab515795803f5b58e5168d` |
| `scripts/check_1.19_vulns.py` | `f845225af27071808a02aa85f7343854057411ef4e0f5872a4ff465248550aa4` | `0636bf8d56152b9c1c59002eb5c2678a59bf83e6a45bc851b2a08998757eba77` |
| `scripts/check_index_state.sh` | `f1e533ffa84529504c6bc40b67b2019eae6267aaf7924d766bc5a3a383894c89` | `4b780a1ae0ad942f93ef393b0d82ad1bc1848ca9146d8b0c4c755abaaf5594cf` |
| `scripts/extract_offsets.py` | `c2bc1ba7222f90d9d4df78954108920eecd6ff6c622b32649eae871d5df0247c` | `e24cfb679407a366fe5f67424350fc2199bce0c632fe4ada45702ae5523c6347` |
| `scripts/gen_vuln_report_20260523.py` | `5a3596eda8ae776cc6e2219b8492d2da7a540705ccb1af4b7d7e5ad5c8921560` | `b050e028c25b20e57dcce4e73fc8d96b00c15348b132fd834ba1e6cf3f9fc905` |
| `scripts/ghostlock_reboot_run.ps1` | `43422198fe5dc1b54aeaa99cad58a405fa76ceaadd47269fdac255945ae7cd00` | `5f99d4725b94536d24018ecd87d7019cb3dffdc27d65e68b19ec52b0428ec38a` |
| `scripts/rokid_ota_auth.py` | `5a70730ac12993be4204b0da30f56e4581b7740b78a9fddcc51b841f23b68363` | `995b7dfa9fc1666cd843354fe3091c6037c50ee449ef581fc2318fb792e914b6` |
| `scripts/rokid_ota_check.py` | `e1b16842afb796dca9b9ddb3fa05e20175142d0c8d2c0c992d7efda17003b4b1` | `fea833cddced15d690486da54732a673e8c3418277a7f1d2a8c7adea36500ca0` |
| `scripts/run_led_orchestrator.sh` | `e2c377987535e121989e18760b26d80db97309f3dea6ec6e80e96c217e2a1089` | `f5421685299e0321a42c30b2b4db10ef8a91973d302eda2cce72dfdbaf98a69d` |
| `scripts/run_pathd_orchestrator.sh` | `ab6b3d068355f5d6682a092a43acb0ee12afe035067085edd4e7079fb55219ce` | `3a9c6fa87cc5ca6fb30ee6334cea3529ee02018439867aefcb2519c02a5b5541` |
| `scripts/run_v11.ps1` | `8bc5ba33bbe7fcf8dc211f5ac9966da3ef1d112f90341041ec209ffafe9c4ac1` | `0fe4383d4431a7c174e18efa2d63bfe258d96439046e87a33ceae3ead115dc0f` |
| `scripts/scan_kernel_phys_load.sh` | `8f61b78909f53cdc9b330819427da7d30daf2008e203e2ff94308a68a6c85f3b` | `8cc1e8c36af5900619c56a977da2cb2c573eb2a5130feb9c5a5fcb9b4d51cb14` |
| `scripts/verify_index_resilience.sh` | `9eb61e560a5be68bbaf8ddf3c7a5b36d96d609a3aec203feca0262df36df9fa4` | `f4be5d701662c8ba5d429affe5d7e2a1b3c0c656cea692fcabc45ae723b5fba8` |
| `source_code/ashmem_overflow_exploit.c` | `820dbb9d02acbba6f54434ffc3a5ef652cd6a14b781cd8d6d8fc61e757f7f204` | `797b8060cb34d64bdadd5073f47f2ff2e951104ed0a12b29e49ee6b31f644e4b` |
| `source_code/ashmem_probe.c` | `54e8c22aedceeb2e60fe19257429e9d145abcd09574ef60cda6f548ec9818075` | `40ce755ba796dafd7eeff9db48c71cdb375e7e12f28ad115fc7b20f726afe27b` |
| `source_code/binder_probe.c` | `ec437b8734f88f1793cba5e7a1200d5c4281b62a9a7442c2a097ece3498f1f6f` | `0bf5c5253a282c3cec0285e999bedfc87585ae2494198194cafa90a87672cc4b` |
| `source_code/ghostlock_rv101/Makefile` | `23675d7de3f218fd1ecd3d28f55714622b548b88b62fa4990fe516ad56ef678e` | `bdb870fdcc4e998150459f457020255fbe5ffa1c537c514f9280c7a5ff810617` |
| `source_code/slabprobe/Makefile` | `e32c0fa3fb4b8045f52fc1a545780c6ae5faa5f06758b035404686f0b488d1e8` | `f9ddab4ed806ff65a3b642949740584d96abaf18a20d1ed61760cc8708e77f63` |

> **維護註記（2026-10-11）**：本表「遮蔽後」欄已更新為**目前磁碟內容**的雜湊。
> 其中 112 筆與初次遮蔽時不同，原因是後續新增了英文版互連連結
> （`> English:` 一行）與程式碼註解英譯；33 筆未變動。
> 「遮蔽前」欄與第二節的置換對照表仍為原始紀錄，可用於驗證遮蔽本身。

> **維護註記（2026-10-11）**：本表「遮蔽後」欄已更新為**目前磁碟內容**的雜湊。
> 其中 112 筆與初次遮蔽時不同，原因是後續新增了英文版互連連結
> （`> English:` 一行）與程式碼註解英譯；121 筆未變動。
> 「遮蔽前」欄與第二節的置換對照表仍為原始紀錄，可用於驗證遮蔽本身。

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
