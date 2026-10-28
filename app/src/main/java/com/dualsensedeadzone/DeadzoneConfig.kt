package com.dualsensedeadzone

import android.content.SharedPreferences

/** Fractions of full travel (0..1); curve is an exponent (1 = linear). */
data class StickConfig(
    val inner: Float = 0.05f,
    val outer: Float = 0.98f,
    val anti: Float = 0f,
    val curve: Float = 1f,
)

data class TriggerConfig(
    val inner: Float = 0f,
    val outer: Float = 1f,
)

data class DeadzoneConfig(
    val left: StickConfig = StickConfig(),
    val right: StickConfig = StickConfig(),
    val l2: TriggerConfig = TriggerConfig(),
    val r2: TriggerConfig = TriggerConfig(),
) {
    /** Layout must match deadzone_cfg_from_array() in deadzone.c. */
    fun toArray(): FloatArray = floatArrayOf(
        left.inner, left.outer, left.anti, left.curve,
        right.inner, right.outer, right.anti, right.curve,
        l2.inner, l2.outer,
        r2.inner, r2.outer,
    )

    fun save(prefs: SharedPreferences) {
        val editor = prefs.edit()
        toArray().forEachIndexed { i, v -> editor.putFloat("cfg_$i", v) }
        editor.apply()
    }

    companion object {
        fun load(prefs: SharedPreferences): DeadzoneConfig {
            val d = DeadzoneConfig().toArray()
            val v = FloatArray(d.size) { prefs.getFloat("cfg_$it", d[it]) }
            return DeadzoneConfig(
                left = StickConfig(v[0], v[1], v[2], v[3]),
                right = StickConfig(v[4], v[5], v[6], v[7]),
                l2 = TriggerConfig(v[8], v[9]),
                r2 = TriggerConfig(v[10], v[11]),
            )
        }
    }
}

/** Live values from the bridge, 0..255. */
data class StickSnapshot(
    val rawLx: Int = 128, val rawLy: Int = 128, val rawRx: Int = 128, val rawRy: Int = 128,
    val outLx: Int = 128, val outLy: Int = 128, val outRx: Int = 128, val outRy: Int = 128,
    val rawL2: Int = 0, val rawR2: Int = 0, val outL2: Int = 0, val outR2: Int = 0,
) {
    companion object {
        const val SIZE = 12

        /** Layout must match nativeGetSnapshot() in jni.c. */
        fun fromArray(a: IntArray): StickSnapshot =
            if (a.size < SIZE) StickSnapshot()
            else StickSnapshot(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11])
    }
}
