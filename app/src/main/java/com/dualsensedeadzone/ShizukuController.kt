package com.dualsensedeadzone

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import android.util.Log
import rikka.shizuku.Shizuku

/** Tracks Shizuku availability/permission and binds [DeadzoneUserService]. */
class ShizukuController(private val context: Context) {

    enum class Status { NOT_RUNNING, NO_PERMISSION, READY, CONNECTED }

    var onStatusChanged: (Status) -> Unit = {}
    var service: IDeadzoneService? = null
        private set

    private val serviceArgs = Shizuku.UserServiceArgs(
        ComponentName(context.packageName, DeadzoneUserService::class.java.name)
    )
        .daemon(false)
        .processNameSuffix("bridge")
        .debuggable(BuildConfig.DEBUG)
        .version(BuildConfig.VERSION_CODE)

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            if (binder == null || !binder.pingBinder()) return
            val svc = IDeadzoneService.Stub.asInterface(binder)
            val error = svc.init(context.applicationInfo.nativeLibraryDir)
            if (error != null) Log.e(TAG, error)
            service = svc
            onStatusChanged(Status.CONNECTED)
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            service = null
            onStatusChanged(currentStatus())
        }
    }

    private val binderReceived = Shizuku.OnBinderReceivedListener { onStatusChanged(currentStatus()) }
    private val binderDead = Shizuku.OnBinderDeadListener {
        service = null
        onStatusChanged(Status.NOT_RUNNING)
    }
    private val permissionResult = Shizuku.OnRequestPermissionResultListener { _, result ->
        if (result == PackageManager.PERMISSION_GRANTED) bind() else onStatusChanged(Status.NO_PERMISSION)
    }

    fun register() {
        Shizuku.addBinderReceivedListenerSticky(binderReceived)
        Shizuku.addBinderDeadListener(binderDead)
        Shizuku.addRequestPermissionResultListener(permissionResult)
    }

    fun unregister() {
        Shizuku.removeBinderReceivedListener(binderReceived)
        Shizuku.removeBinderDeadListener(binderDead)
        Shizuku.removeRequestPermissionResultListener(permissionResult)
    }

    fun currentStatus(): Status = when {
        service != null -> Status.CONNECTED
        !Shizuku.pingBinder() -> Status.NOT_RUNNING
        Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED -> Status.NO_PERMISSION
        else -> Status.READY
    }

    /** Requests permission if needed, otherwise binds the user service. */
    fun connect() {
        when (currentStatus()) {
            Status.NOT_RUNNING, Status.CONNECTED -> Unit
            Status.NO_PERMISSION -> Shizuku.requestPermission(REQUEST_CODE)
            Status.READY -> bind()
        }
    }

    private fun bind() {
        Shizuku.bindUserService(serviceArgs, connection)
    }

    /** Stops the bridge and kills the service process. */
    fun disconnect() {
        if (Shizuku.pingBinder()) Shizuku.unbindUserService(serviceArgs, connection, true)
        service = null
        onStatusChanged(currentStatus())
    }

    private companion object {
        const val TAG = "ShizukuController"
        const val REQUEST_CODE = 1
    }
}
