#!/usr/bin/env python3
"""術語正規化：讓 scripts/i18n/out/*.xml 的新字串用語與各語系「既有」strings.xml 一致。

背景：新字串由翻譯產出時統一使用 `WiFi`／`Bluetooth`／`LED` 等拉丁寫法，但各語系既有
翻譯早已選定各自的慣用詞（例：ja 用「Wi-Fi」「メガネ」、de 用「WLAN」、ar 用「واي فاي」「بلوتوث」、
ko 用「블루투스」）。同一語系檔內混用會顯得不一致，故以本腳本統一。

本腳本可重複執行（idempotent）：先做詞彙替換，再對隱私指示燈相關 key 寫入各語系固定譯法。
執行順序：翻譯產出 → normalize_locale_terms.py → apply_phone_strings.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path("/home/kali/rokid_root")
OUT = ROOT / "scripts/i18n/out"

# locale tag（out 檔內的 @@ 標記）→ 該語系既有檔案的慣用詞
TERMS = {
    "values-fr":      {"wifi": "Wi-Fi"},
    "values-de":      {"wifi": "WLAN"},
    "values-es":      {"wifi": "WiFi"},
    "values-pt":      {"wifi": "Wi-Fi"},
    "values-ja":      {"wifi": "Wi-Fi", "bluetooth": "Bluetooth", "glasses": "メガネ"},
    "values-ko":      {"wifi": "Wi-Fi", "bluetooth": "블루투스"},
    "values-zh-rCN":  {"wifi": "WiFi", "bluetooth": "蓝牙"},
    "values-ru":      {"wifi": "Wi-Fi"},
    "values-uk":      {"wifi": "Wi-Fi"},
    "values-th":      {"wifi": "Wi-Fi", "bluetooth": "บลูทูธ"},
    "values-vi":      {"wifi": "Wi-Fi"},
    "values-ar":      {"wifi": "واي فاي", "bluetooth": "بلوتوث"},
}

# 隱私指示燈／LED 相關 key 的各語系固定譯法（對齊既有 sec_led／led_switch_label）
LED = {
    "values-zh-rCN": {
        "log_led_enabling": "开启隐私指示灯…", "log_led_disabling": "关闭隐私指示灯…",
        "log_led_on_ok": "✓ 指示灯：亮", "log_led_off_ok": "✓ 指示灯：熄",
        "log_led_fail": "✗ 指示灯设置失败 %1$s", "led_name": "%1$s灯",
    },
    "values-ja": {
        "log_led_enabling": "プライバシーインジケーターを点灯中…",
        "log_led_disabling": "プライバシーインジケーターを消灯中…",
        "log_led_on_ok": "✓ インジケーター：点灯", "log_led_off_ok": "✓ インジケーター：消灯",
        "log_led_fail": "✗ インジケーターの設定に失敗 %1$s", "led_name": "%1$sランプ",
    },
    "values-ko": {
        "log_led_enabling": "프라이버시 표시등 켜는 중…", "log_led_disabling": "프라이버시 표시등 끄는 중…",
        "log_led_on_ok": "✓ 표시등: 켜짐", "log_led_off_ok": "✓ 표시등: 꺼짐",
        "log_led_fail": "✗ 표시등 설정 실패 %1$s", "led_name": "%1$s LED",
    },
    "values-th": {
        "log_led_enabling": "กำลังเปิดไฟแสดงความเป็นส่วนตัว…",
        "log_led_disabling": "กำลังปิดไฟแสดงความเป็นส่วนตัว…",
        "log_led_on_ok": "✓ ไฟแสดงสถานะ: เปิด", "log_led_off_ok": "✓ ไฟแสดงสถานะ: ปิด",
        "log_led_fail": "✗ ตั้งค่าไฟแสดงสถานะไม่สำเร็จ: %1$s", "led_name": "ไฟ %1$s",
    },
    "values-ar": {
        "log_led_enabling": "يجري تشغيل مؤشر الخصوصية…", "log_led_disabling": "يجري إيقاف مؤشر الخصوصية…",
        "log_led_on_ok": "✓ المؤشر: مضيء", "log_led_off_ok": "✓ المؤشر: مطفأ",
        "log_led_fail": "✗ فشل ضبط المؤشر: %1$s", "led_name": "مؤشر %1$s",
    },
    "values-ru": {
        "log_led_enabling": "Включение индикатора конфиденциальности…",
        "log_led_disabling": "Выключение индикатора конфиденциальности…",
        "log_led_on_ok": "✓ Индикатор: вкл.", "log_led_off_ok": "✓ Индикатор: выкл.",
        "log_led_fail": "✗ Ошибка настройки индикатора: %1$s", "led_name": "Светодиод %1$s",
    },
    "values-uk": {
        "log_led_enabling": "Увімкнення індикатора приватності…",
        "log_led_disabling": "Вимкнення індикатора приватності…",
        "log_led_on_ok": "✓ Індикатор: увімк.", "log_led_off_ok": "✓ Індикатор: вимк.",
        "log_led_fail": "✗ Помилка налаштування індикатора: %1$s", "led_name": "Світлодіод %1$s",
    },
    "values-vi": {
        "log_led_enabling": "Đang bật đèn báo riêng tư…", "log_led_disabling": "Đang tắt đèn báo riêng tư…",
        "log_led_on_ok": "✓ Đèn báo: bật", "log_led_off_ok": "✓ Đèn báo: tắt",
        "log_led_fail": "✗ Đặt đèn báo thất bại %1$s", "led_name": "Đèn %1$s",
    },
    "values-fr": {
        "log_led_enabling": "Allumage du voyant de confidentialité…",
        "log_led_disabling": "Extinction du voyant de confidentialité…",
        "log_led_on_ok": "✓ Voyant : allumé", "log_led_off_ok": "✓ Voyant : éteint",
        "log_led_fail": "✗ Échec du réglage du voyant : %1$s", "led_name": "Voyant %1$s",
    },
    "values-de": {
        "log_led_enabling": "Datenschutz-Anzeige wird eingeschaltet…",
        "log_led_disabling": "Datenschutz-Anzeige wird ausgeschaltet…",
        "log_led_on_ok": "✓ Anzeige: an", "log_led_off_ok": "✓ Anzeige: aus",
        "log_led_fail": "✗ Anzeige-Einstellung fehlgeschlagen: %1$s", "led_name": "%1$s-LED",
    },
    "values-es": {
        "log_led_enabling": "Encendiendo el indicador de privacidad…",
        "log_led_disabling": "Apagando el indicador de privacidad…",
        "log_led_on_ok": "✓ Indicador: encendido", "log_led_off_ok": "✓ Indicador: apagado",
        "log_led_fail": "✗ Error al configurar el indicador: %1$s", "led_name": "LED %1$s",
    },
    "values-pt": {
        "log_led_enabling": "Ligando o indicador de privacidade…",
        "log_led_disabling": "Desligando o indicador de privacidade…",
        "log_led_on_ok": "✓ Indicador: ligado", "log_led_off_ok": "✓ Indicador: desligado",
        "log_led_fail": "✗ Falha na configuração do indicador: %1$s", "led_name": "LED %1$s",
    },
}

# 其他零星對齊（依既有檔案的用詞）
MISC = {
    "values-uk": {"Тихе фотографування": "Тиха зйомка"},          # silent_switch_label 用「Тиха зйомка」
    "values-ja": {"グラス": "メガネ"},                              # conn_card_title 用「メガネ」
}

STR_RE = re.compile(r'<string name="([^"]+)">(.*)</string>$')


def main() -> int:
    changed = 0
    for f in sorted(OUT.glob("*.xml")):
        tag, out_lines = None, []
        for line in f.read_text(encoding="utf-8").splitlines():
            s = line.strip()
            if s.startswith("@@"):
                tag = s[2:].strip()
                out_lines.append(line)
                continue
            m = STR_RE.match(s)
            if not m:
                out_lines.append(line)
                continue
            key, val = m.group(1), m.group(2)
            terms = TERMS.get(tag, {})
            if "wifi" in terms:
                val = val.replace("WiFi", terms["wifi"])
            if "bluetooth" in terms:
                val = val.replace("Bluetooth", terms["bluetooth"])
            for old, new in MISC.get(tag, {}).items():
                val = val.replace(old, new)
            if key in LED.get(tag, {}):
                val = LED[tag][key]
            new_line = f'<string name="{key}">{val}</string>'
            if new_line != s:
                changed += 1
                print(f"  {tag:14s} {key:22s} -> {val[:70]}")
            out_lines.append(new_line)
        f.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    print(f"共調整 {changed} 條")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
