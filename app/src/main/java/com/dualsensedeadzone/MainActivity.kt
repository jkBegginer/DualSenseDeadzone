package com.dualsensedeadzone

import android.content.Context
import android.os.Bundle
import android.os.RemoteException
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import java.util.Locale

class MainActivity : ComponentActivity() {

    private lateinit var shizuku: ShizukuController
    private var status by mutableStateOf(ShizukuController.Status.NOT_RUNNING)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        shizuku = ShizukuController(applicationContext)
        shizuku.onStatusChanged = { runOnUiThread { status = it } }
        shizuku.register()

        val prefs = getSharedPreferences("deadzone", Context.MODE_PRIVATE)

        setContent {
            MaterialTheme(colorScheme = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme()) {
                MainScreen(
                    status = status,
                    initialConfig = remember { DeadzoneConfig.load(prefs) },
                    shizuku = shizuku,
                    onConfigChanged = { it.save(prefs) },
                )
            }
        }
    }

    override fun onDestroy() {
        shizuku.unregister()
        super.onDestroy()
    }
}

@Composable
private fun MainScreen(
    status: ShizukuController.Status,
    initialConfig: DeadzoneConfig,
    shizuku: ShizukuController,
    onConfigChanged: (DeadzoneConfig) -> Unit,
) {
    var config by remember { mutableStateOf(initialConfig) }
    var running by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    var snapshot by remember { mutableStateOf(StickSnapshot()) }

    fun pushConfig(c: DeadzoneConfig) {
        config = c
        onConfigChanged(c)
        try {
            shizuku.service?.setConfig(c.toArray())
        } catch (e: RemoteException) {
            error = e.message
        }
    }

    // Poll the bridge for live stick values and running state.
    LaunchedEffect(status) {
        while (status == ShizukuController.Status.CONNECTED) {
            try {
                val svc = shizuku.service ?: break
                running = svc.isRunning
                snapshot = StickSnapshot.fromArray(svc.snapshot)
            } catch (e: RemoteException) {
                error = e.message
                break
            }
            delay(33)
        }
        if (status != ShizukuController.Status.CONNECTED) running = false
    }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text("DualSense Deadzone", style = MaterialTheme.typography.headlineSmall)
            StatusCard(
                status = status,
                running = running,
                error = error,
                onConnect = { shizuku.connect() },
                onStart = {
                    val svc = shizuku.service ?: return@StatusCard
                    try {
                        svc.setConfig(config.toArray())
                        error = svc.start()
                    } catch (e: RemoteException) {
                        error = e.message
                    }
                },
                onStop = {
                    try {
                        shizuku.service?.stop()
                    } catch (e: RemoteException) {
                        error = e.message
                    }
                },
            )

            Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
                StickView(
                    "왼쪽", snapshot.rawLx, snapshot.rawLy, snapshot.outLx, snapshot.outLy, config.left,
                    Modifier.weight(1f),
                )
                StickView(
                    "오른쪽", snapshot.rawRx, snapshot.rawRy, snapshot.outRx, snapshot.outRy, config.right,
                    Modifier.weight(1f),
                )
            }

            StickSettings("왼쪽 스틱", config.left) { pushConfig(config.copy(left = it)) }
            StickSettings("오른쪽 스틱", config.right) { pushConfig(config.copy(right = it)) }
            TriggerSettings("L2", config.l2, snapshot.rawL2, snapshot.outL2) { pushConfig(config.copy(l2 = it)) }
            TriggerSettings("R2", config.r2, snapshot.rawR2, snapshot.outR2) { pushConfig(config.copy(r2 = it)) }

            OutlinedButton(onClick = { pushConfig(DeadzoneConfig()) }) { Text("기본값으로") }
            Spacer(Modifier.height(24.dp))
        }
    }
}

@Composable
private fun StatusCard(
    status: ShizukuController.Status,
    running: Boolean,
    error: String?,
    onConnect: () -> Unit,
    onStart: () -> Unit,
    onStop: () -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            val text = when (status) {
                ShizukuController.Status.NOT_RUNNING -> "Shizuku가 실행 중이 아닙니다"
                ShizukuController.Status.NO_PERMISSION -> "Shizuku 권한이 필요합니다"
                ShizukuController.Status.READY -> "Shizuku 준비됨"
                ShizukuController.Status.CONNECTED -> if (running) "데드존 적용 중" else "연결됨 (정지)"
            }
            Text(text, style = MaterialTheme.typography.titleMedium)
            error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            when (status) {
                ShizukuController.Status.NOT_RUNNING -> Unit
                ShizukuController.Status.NO_PERMISSION, ShizukuController.Status.READY ->
                    Button(onClick = onConnect) { Text("연결") }
                ShizukuController.Status.CONNECTED ->
                    if (running) OutlinedButton(onClick = onStop) { Text("정지") }
                    else Button(onClick = onStart) { Text("시작") }
            }
        }
    }
}

/** Gray dot = raw input, colored dot = output; rings mark inner/outer deadzone. */
@Composable
private fun StickView(
    label: String,
    rawX: Int, rawY: Int, outX: Int, outY: Int,
    cfg: StickConfig,
    modifier: Modifier = Modifier,
) {
    val outline = MaterialTheme.colorScheme.outline
    val accent = MaterialTheme.colorScheme.primary
    val zone = MaterialTheme.colorScheme.error.copy(alpha = 0.25f)
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, style = MaterialTheme.typography.labelLarge)
        Canvas(Modifier.size(140.dp)) {
            val c = Offset(size.width / 2, size.height / 2)
            val r = size.minDimension / 2 - 4f
            drawCircle(outline, r, c, style = Stroke(2f))
            drawCircle(zone, r * cfg.inner, c)
            drawCircle(outline.copy(alpha = 0.5f), r * cfg.outer, c, style = Stroke(1f))
            fun pos(x: Int, y: Int) = Offset(c.x + (x - 128) / 127f * r, c.y + (y - 128) / 127f * r)
            drawCircle(Color.Gray, 6f, pos(rawX, rawY))
            drawCircle(accent, 8f, pos(outX, outY))
        }
        Text("원본 $rawX,$rawY → $outX,$outY", style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun StickSettings(title: String, cfg: StickConfig, onChange: (StickConfig) -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            LabeledSlider("안쪽 데드존", cfg.inner, 0f..0.5f, percent = true) { onChange(cfg.copy(inner = it)) }
            LabeledSlider("바깥 데드존", cfg.outer, 0.5f..1f, percent = true) { onChange(cfg.copy(outer = it)) }
            LabeledSlider("안티 데드존", cfg.anti, 0f..0.5f, percent = true) { onChange(cfg.copy(anti = it)) }
            LabeledSlider("응답 커브", cfg.curve, 0.5f..3f, percent = false) { onChange(cfg.copy(curve = it)) }
        }
    }
}

@Composable
private fun TriggerSettings(title: String, cfg: TriggerConfig, raw: Int, out: Int, onChange: (TriggerConfig) -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            LinearProgressIndicator(progress = { raw / 255f }, modifier = Modifier.fillMaxWidth().padding(top = 8.dp))
            LinearProgressIndicator(progress = { out / 255f }, modifier = Modifier.fillMaxWidth().padding(top = 4.dp))
            LabeledSlider("시작 지점", cfg.inner, 0f..0.5f, percent = true) { onChange(cfg.copy(inner = it)) }
            LabeledSlider("최대 지점", cfg.outer, 0.5f..1f, percent = true) { onChange(cfg.copy(outer = it)) }
        }
    }
}

@Composable
private fun LabeledSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    percent: Boolean,
    onChange: (Float) -> Unit,
) {
    val shown = if (percent) "${(value * 100).toInt()}%" else String.format(Locale.US, "%.2f", value)
    Row(Modifier.fillMaxWidth().padding(top = 8.dp), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label)
        Text(shown)
    }
    Slider(value = value.coerceIn(range), onValueChange = onChange, valueRange = range)
}
