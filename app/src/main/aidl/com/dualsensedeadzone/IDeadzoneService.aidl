package com.dualsensedeadzone;

// Runs inside the Shizuku user service process (shell uid).
interface IDeadzoneService {
    // Called by the Shizuku server when the service is unbound. The transaction code is fixed by Shizuku.
    void destroy() = 16777114;

    // Loads the native library from the app's extracted lib dir. Returns an error message, or null on success.
    String init(String nativeLibraryDir) = 1;

    // Grabs the real DualSense and creates the virtual one. Returns an error message, or null on success.
    String start() = 2;
    void stop() = 3;
    boolean isRunning() = 4;

    // Flat config, see DeadzoneConfig.toArray().
    void setConfig(in float[] config) = 5;

    // See StickSnapshot.fromArray().
    int[] getSnapshot() = 6;
}
