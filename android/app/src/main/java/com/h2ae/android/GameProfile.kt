package com.h2ae.android

/** Preserve game settings and replace only the options managed by the launcher. */
internal object GameProfile {
    private val setting = Regex("^\\s*([^\\s=]+)(?:\\s*=\\s*|\\s+)(.*)$")
    fun render(lines: List<String>, overrides: Map<String, String>): String {
        val remaining = overrides.toMutableMap()
        val out = ArrayList<String>()
        for (line in lines) {
            val key = setting.find(line)?.groupValues?.get(1)
            val canonical = overrides.keys.firstOrNull { it.equals(key, true) }
            if (canonical == null) out.add(line)
            else remaining.remove(canonical)?.let { if (it.isNotEmpty()) out.add(format(canonical, it)) }   // an empty value removes the option
        }
        remaining.forEach { (key, value) -> if (value.isNotEmpty()) out.add(format(key, value)) }
        return out.joinToString("\r\n") + "\r\n"
    }
    private fun format(key: String, value: String) = if (key == "DefaultScene") "$key=$value" else "$key $value"
}
