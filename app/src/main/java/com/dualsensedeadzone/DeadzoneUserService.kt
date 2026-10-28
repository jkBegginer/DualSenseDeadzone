package com.dualsensedeadzone

import android.util.Log
import java.io.File
import kotlin.system.exitProcess

/**
 * Shizuku user service: runs as the shell uid (input + uhid groups), so it can grab
 * /dev/input/event* and write /dev/uhid. Instantiated by Shizuku via its no-arg constructor.
 */
class DeadzoneUserService : IDeadzoneService.Stub() {

    private var loaded = false

    override fun destroy() {
        Log.i(TAG, "destroy")
        if (loaded) NativeBridge.nativeStop()
        exitProcess(0)
    }

    override fun init(nativeLibraryDir: String): String? {
        if (loaded) return null
        // System.loadLibrary does not see the app's lib dir in this process, so load by path.
        val lib = File(nativeLibraryDir, "libdsdz.so")
        return try {
            System.load(lib.absolutePath)
            loaded = true
            null
        } catch (t: Throwable) {
            Log.e(TAG, "failed to load ${lib.absolutePath}", t)
            "Failed to load native library: ${t.message}"
        }
    }

    override fun start(): String? {
        if (!loaded) return "Native library not loaded"
        return NativeBridge.nativeStart()
    }

    override fun stop() {
        if (loaded) NativeBridge.nativeStop()
    }

    override fun isRunning(): Boolean = loaded && NativeBridge.nativeIsRunning()

    override fun setConfig(config: FloatArray) {
        if (loaded) NativeBridge.nativeSetConfig(config)
    }

    override fun getSnapshot(): IntArray =
        if (loaded) NativeBridge.nativeGetSnapshot() else IntArray(StickSnapshot.SIZE)

    private companion object {
        const val TAG = "DeadzoneUserService"
    }
}
