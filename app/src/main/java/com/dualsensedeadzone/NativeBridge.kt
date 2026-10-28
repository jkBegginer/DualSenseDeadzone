package com.dualsensedeadzone

/** JNI entry points into libdsdz.so. Only usable inside the Shizuku user service process. */
object NativeBridge {
    external fun nativeStart(): String?
    external fun nativeStop()
    external fun nativeIsRunning(): Boolean
    external fun nativeSetConfig(values: FloatArray)
    external fun nativeGetSnapshot(): IntArray
}
