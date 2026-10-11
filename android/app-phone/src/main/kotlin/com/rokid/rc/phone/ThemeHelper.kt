package com.rokid.rc.phone

import android.content.Context
import androidx.appcompat.app.AppCompatDelegate

/**
 * Theme mode: light/dark (two brand palettes, see values/colors.xml and values-night/colors.xml).
 *
 * - **Dark** by default (dark blue background + bright cyan, matching the launcher icon / glasses dashboard)
 * - After the user switches in "Settings → Appearance" it is stored in prefs and applied at app startup in
 *   `MainActivity.onCreate` (before setContentView); the switch itself takes effect immediately through `recreate()`.
 */
object ThemeHelper {

    private const val PREFS = "app_theme"
    private const val KEY_LIGHT = "light_mode"

    /** true = light mode. */
    fun isLight(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY_LIGHT, false)

    fun setLight(context: Context, light: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit().putBoolean(KEY_LIGHT, light).apply()
        apply(context)
    }

    /** Apply night mode from prefs (called before the Activity is created). */
    fun apply(context: Context) {
        AppCompatDelegate.setDefaultNightMode(
            if (isLight(context)) AppCompatDelegate.MODE_NIGHT_NO
            else AppCompatDelegate.MODE_NIGHT_YES
        )
    }
}
