// User-interface translation.
//
// Code only ever names strings by stable English keys ("menu.save", "status.saved");
// the language changes what tr() returns, nothing else. Identifiers, file formats,
// operation names and widget IDs never depend on the language, so switching it at any
// moment, as often as you like, cannot change behaviour.
//
// Tables are JSON files <dir>/<domain>.<lang>.json: {"key": "text", ...}. English is
// always loaded as the fallback; a missing key falls back to English, then to the key.
// A translation whose printf conversions (%s %d %.1f ...) differ from the English text
// is rejected at load time, so a translation can never break a format call.
#ifndef MC_CORE_I18N_H
#define MC_CORE_I18N_H

#define I18N_LANG_COUNT 5
// Language codes (BCP 47): "en", "zh-Hans", "zh-Hant", "ja", "ko".
const char *i18n_lang_code(int i);
const char *i18n_lang_name(int i);   // in its own language: "English", "简体中文", ...
int i18n_lang_index(const char *code);  // -1 if unknown

// Adds a table domain (e.g. "editor") found in dir; loads it for English, the current
// language and any language already loaded. Returns the number of English keys added.
int i18n_add_domain(const char *dir, const char *domain);
// Switches the language (unknown codes fall back to English). Returns 0 on success.
int i18n_set_language(const char *code);
const char *i18n_language(void);
// Best match for the operating system's preferred languages.
const char *i18n_system_language(void);
// Rejected translations since the last load (format mismatches), for diagnostics.
int i18n_rejected(void);

// The text for key in the current language (never NULL). The pointer stays valid until
// i18n_shutdown, whatever languages are switched to in between.
const char *tr(const char *key);

void i18n_shutdown(void);

#endif
