package com.rokid.rc.phone

import android.content.Context
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat

/**
 * Per-app language switching (AppCompatDelegate.setApplicationLocales).
 * When unset it follows the system language; the SET_LOCALE command can override it (stored in SharedPreferences, kept across restarts).
 */
object LocaleHelper {
    private const val PREFS = "app_locale"
    private const val KEY = "lang"

    /** Called from Application.onCreate: apply the saved language. */
    fun apply(context: Context) {
        val lang = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null)
        AppCompatDelegate.setApplicationLocales(
            if (lang.isNullOrEmpty()) LocaleListCompat.getEmptyLocaleList()
            else LocaleListCompat.forLanguageTags(lang)
        )
    }

    /** Set the language (lang=null restores following the system). */
    fun set(context: Context, lang: String?) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().apply {
            if (lang.isNullOrEmpty()) remove(KEY) else putString(KEY, lang)
        }.apply()
        apply(context)
    }

    fun current(context: Context): String? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null)

    /**
     * Supported languages (locale tag → native name). The list order is the dialog order;
     * `null` = follow the system (the label comes from `R.string.lang_system`).
     * One-to-one with the 14 locales in `res/values-xx/strings.xml` (including the default zh-TW).
     */
    val LANGUAGES: List<Pair<String?, String>> = listOf(
        "zh-TW" to "繁體中文",
        "zh-CN" to "简体中文",
        "en" to "English",
        "ja" to "日本語",
        "ko" to "한국어",
        "fr" to "Français",
        "de" to "Deutsch",
        "es" to "Español",
        "pt" to "Português",
        "ru" to "Русский",
        "uk" to "Українська",
        "th" to "ไทย",
        "vi" to "Tiếng Việt",
        "ar" to "العربية",
    )

    /** Currently selected index (0 = follow the system, otherwise it corresponds to [LANGUAGES] + 1). */
    fun currentIndex(context: Context): Int {
        val cur = current(context)
        if (cur.isNullOrEmpty()) return 0
        val i = LANGUAGES.indexOfFirst { it.first.equals(cur, ignoreCase = true) }
        return if (i >= 0) i + 1 else 0
    }
}
