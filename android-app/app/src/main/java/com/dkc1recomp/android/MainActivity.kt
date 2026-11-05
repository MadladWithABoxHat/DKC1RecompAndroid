
package com.dkc1recomp.android

import android.graphics.Color
import java.io.File
import android.widget.FrameLayout
import android.view.View
import android.net.Uri
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.Surface
import android.view.SurfaceHolder
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import android.util.Log

class MainActivity : AppCompatActivity() {

    private lateinit var statusText: TextView
    private lateinit var rootLayout: LinearLayout
    private lateinit var selectButton: Button

    @Volatile
    private var engineInitialized = false

    @Volatile
    private var lastBackPressMs = 0L

    @Volatile
    private var touchInputMask = 0

    @Volatile
    private var frameTimingResetRequested = true

    @Volatile
    private var renderFailed = false

    private var nativeLibraryLoaded = false
    private var glView: GLSurfaceView? = null

    private external fun nativeInit(
        romPath: String,
        saveRoot: String,
        viewWidth: Int,
        viewHeight: Int
    ): Boolean
    private external fun nativeFrame(input: Int): Boolean
    private external fun nativeGetFrameBuffer(): ByteBuffer?
    private external fun nativeShutdown()

    private external fun nativeSetPaused(paused: Boolean)

    private external fun nativeFlushSaves()

    private external fun nativeSetViewport(width: Int, height: Int): Int

    private val romPicker =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            uri?.let { loadRom(it) }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        rootLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 32, 24, 16)
        }

        statusText = TextView(this).apply {
            text = "DKC1 Recomp"
            textSize = 18f
        }

        selectButton = Button(this).apply {
            text = "Select Donkey Kong Country ROM"
            setOnClickListener {
                romPicker.launch(arrayOf("*/*"))
            }
        }

        rootLayout.addView(statusText)
        rootLayout.addView(selectButton)
        setContentView(rootLayout)

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                val now = SystemClock.elapsedRealtime()
                if (!engineInitialized || now - lastBackPressMs < 2000) {
                    finish()
                } else {
                    lastBackPressMs = now
                    Toast.makeText(
                        this@MainActivity,
                        "Press back again to exit",
                        Toast.LENGTH_SHORT
                    ).show()
                }
            }
        })

        try {
            System.loadLibrary("dkc1_android")
            nativeLibraryLoaded = true
            statusText.text = "Native library loaded\nSelect your ROM"

            val remembered = savedRomFile()
            if (remembered.isFile) startGame(remembered)
        } catch (e: UnsatisfiedLinkError) {
            statusText.text = "Library loading failed:\n${e.message}"
        }
    }

    private fun enterGameMode() {
        rootLayout.setPadding(0, 0, 0, 0)
        rootLayout.setBackgroundColor(Color.BLACK)
        window.setBackgroundDrawable(android.graphics.drawable.ColorDrawable(Color.BLACK))
        hideSystemBars()
    }

    private fun hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus && engineInitialized) hideSystemBars()
    }

    private fun requestSixtyHertz(holder: SurfaceHolder) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && holder.surface.isValid) {
            holder.surface.setFrameRate(
                60f,
                Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE
            )
        }
    }

    private fun savedRomFile(): File =
        File(File(filesDir, "rom").apply { mkdirs() }, "selected_game.sfc")

    private fun loadRom(uri: Uri) {
        if (!nativeLibraryLoaded || engineInitialized) return

        try {
            statusText.text = "Copying ROM..."

            val target = savedRomFile()
            val partial = File(target.parentFile, "selected_game.sfc.part")

            contentResolver.openInputStream(uri).use { input ->
                requireNotNull(input) { "Could not open selected file" }
                partial.outputStream().use { output ->
                    input.copyTo(output)
                }
            }

            if (!partial.renameTo(target)) {
                partial.copyTo(target, overwrite = true)
                partial.delete()
            }

            startGame(target)
        } catch (e: Exception) {
            statusText.text = "Error: ${e.message}"
        }
    }

    private fun startGame(romFile: File) {
        if (!nativeLibraryLoaded || engineInitialized) return

        try {
            val saveDirectory = File(filesDir, "saves")
            saveDirectory.mkdirs()

            statusText.text = "Initializing engine..."

            val metrics = resources.displayMetrics
            engineInitialized = nativeInit(
                romFile.absolutePath,
                saveDirectory.absolutePath,
                maxOf(metrics.widthPixels, metrics.heightPixels),
                minOf(metrics.widthPixels, metrics.heightPixels)
            )

            if (engineInitialized) {
                statusText.text = "Engine initialized\nStarting renderer..."

                statusText.visibility = View.GONE
                selectButton.visibility = View.GONE
                enterGameMode()

                val surface = GLSurfaceView(this).apply {
                    setEGLContextClientVersion(2)
                    preserveEGLContextOnPause = true
                    setRenderer(DkcRenderer())
                    renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
                    keepScreenOn = true
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(holder: SurfaceHolder) = requestSixtyHertz(holder)
                        override fun surfaceChanged(
                            holder: SurfaceHolder, format: Int, width: Int, height: Int
                        ) = requestSixtyHertz(holder)
                        override fun surfaceDestroyed(holder: SurfaceHolder) {}
                    })
                }

                glView = surface

                val gameContainer = FrameLayout(this)

                gameContainer.addView(
                    surface,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.MATCH_PARENT
                    )
                )

                val controls = TouchControlsView(
                    this,
                    onInputChanged = { input ->
                        touchInputMask = input
                    }
                )

                gameContainer.addView(
                    controls,
                    FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.MATCH_PARENT
                    )
                )

                rootLayout.addView(
                    gameContainer,
                    LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        0,
                        1f
                    )
                )

                statusText.text = "DKC running"
            } else {
                romFile.delete()
                statusText.text =
                    "That file is not a supported Donkey Kong Country ROM.\nSelect the correct ROM."
            }

        } catch (e: Exception) {
            statusText.text = "Error: ${e.message}"
        }
    }


    private inner class DkcRenderer : GLSurfaceView.Renderer {
        private var gameWidth = 256
        private val gameHeight = 224
        private val frameRate = 60.098811862
        private val maxFrameDeltaNs = 250_000_000L
        private val maxCatchUpTicks = 2

        private var texture = 0
        private var program = 0
        private var positionLocation = -1
        private var texCoordLocation = -1
        private var textureLocation = -1
        private var texSizeLocation = -1
        private var scaleLocation = -1

        private var scaleX = 1f
        private var scaleY = 1f
        private var textureStale = true

        private var lastFrameTimeNs = 0L
        private var accumulatorNs = 0.0
        private val frameStepNs = 1_000_000_000.0 / frameRate

        private val vertices = floatBuffer(
            floatArrayOf(
                -1f, -1f,
                1f, -1f,
                -1f,  1f,
                1f,  1f
            )
        )

        private val texCoords = floatBuffer(
            floatArrayOf(
                0f, 1f,
                1f, 1f,
                0f, 0f,
                1f, 0f
            )
        )

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            GLES20.glClearColor(0f, 0f, 0f, 1f)

            val vertexShader = """
            attribute vec4 aPosition;
            attribute vec2 aTexCoord;
            varying vec2 vTexCoord;

            void main() {
                gl_Position = aPosition;
                vTexCoord = aTexCoord;
            }
        """.trimIndent()

            val fragmentShader = """
            #ifdef GL_FRAGMENT_PRECISION_HIGH
            precision highp float;
            #else
            precision mediump float;
            #endif
            uniform sampler2D uTexture;
            uniform vec2 uTexSize;
            uniform vec2 uScale;
            varying vec2 vTexCoord;

            void main() {
                vec2 texel = vTexCoord * uTexSize;
                vec2 base = floor(texel);
                vec2 scale = max(uScale, vec2(1.0));
                vec2 region = 0.5 - 0.5 / scale;
                vec2 fromCentre = fract(texel) - 0.5;
                vec2 blended = (fromCentre - clamp(fromCentre, -region, region)) * scale + 0.5;
                vec4 pixel = texture2D(uTexture, (base + blended) / uTexSize);
                gl_FragColor = vec4(pixel.bgr, 1.0);
            }
        """.trimIndent()

            program = createProgram(vertexShader, fragmentShader)

            positionLocation = GLES20.glGetAttribLocation(program, "aPosition")
            texCoordLocation = GLES20.glGetAttribLocation(program, "aTexCoord")
            textureLocation = GLES20.glGetUniformLocation(program, "uTexture")
            texSizeLocation = GLES20.glGetUniformLocation(program, "uTexSize")
            scaleLocation = GLES20.glGetUniformLocation(program, "uScale")

            val textures = IntArray(1)
            GLES20.glGenTextures(1, textures, 0)
            texture = textures[0]

            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture)

            GLES20.glTexParameteri(
                GLES20.GL_TEXTURE_2D,
                GLES20.GL_TEXTURE_MIN_FILTER,
                GLES20.GL_LINEAR
            )
            GLES20.glTexParameteri(
                GLES20.GL_TEXTURE_2D,
                GLES20.GL_TEXTURE_MAG_FILTER,
                GLES20.GL_LINEAR
            )
            GLES20.glTexParameteri(
                GLES20.GL_TEXTURE_2D,
                GLES20.GL_TEXTURE_WRAP_S,
                GLES20.GL_CLAMP_TO_EDGE
            )
            GLES20.glTexParameteri(
                GLES20.GL_TEXTURE_2D,
                GLES20.GL_TEXTURE_WRAP_T,
                GLES20.GL_CLAMP_TO_EDGE
            )

            GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1)

            GLES20.glTexImage2D(
                GLES20.GL_TEXTURE_2D,
                0,
                GLES20.GL_RGBA,
                gameWidth,
                gameHeight,
                0,
                GLES20.GL_RGBA,
                GLES20.GL_UNSIGNED_BYTE,
                null
            )

            textureStale = true
            lastFrameTimeNs = 0L
            accumulatorNs = 0.0
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            if (width <= 0 || height <= 0) return

            val sourceWidth = nativeSetViewport(width, height)

            if (sourceWidth > 0 && sourceWidth != gameWidth) {
                gameWidth = sourceWidth
                textureStale = true

                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture)
                GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1)
                GLES20.glTexImage2D(
                    GLES20.GL_TEXTURE_2D,
                    0,
                    GLES20.GL_RGBA,
                    gameWidth,
                    gameHeight,
                    0,
                    GLES20.GL_RGBA,
                    GLES20.GL_UNSIGNED_BYTE,
                    null
                )
            }

            val pixelAspect = 7f / 6f
            val displayAspect = gameWidth * pixelAspect / gameHeight
            val columnWidth = height * pixelAspect / gameHeight
            val fitHeightWidth = height * displayAspect

            val viewportWidth: Int
            val viewportHeight: Int

            if (fitHeightWidth <= width + 2f * columnWidth) {

                viewportHeight = height
                viewportWidth = fitHeightWidth.toInt()
            } else {
                viewportWidth = width
                viewportHeight = (width / displayAspect).toInt()
            }

            GLES20.glViewport(
                (width - viewportWidth) / 2,
                (height - viewportHeight) / 2,
                viewportWidth,
                viewportHeight
            )

            scaleX = viewportWidth.toFloat() / gameWidth
            scaleY = viewportHeight.toFloat() / gameHeight
        }

        override fun onDrawFrame(gl: GL10?) {
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)

            if (!engineInitialized || renderFailed) return

            val now = System.nanoTime()

            if (frameTimingResetRequested || lastFrameTimeNs == 0L) {
                lastFrameTimeNs = now
                accumulatorNs = 0.0
                frameTimingResetRequested = false
            }

            val delta = (now - lastFrameTimeNs)
                .coerceIn(0L, maxFrameDeltaNs)

            lastFrameTimeNs = now
            accumulatorNs += delta

            var ticks = 0

            while (accumulatorNs >= frameStepNs &&
                ticks < maxCatchUpTicks
            ) {
                if (!nativeFrame(touchInputMask)) {
                    renderFailed = true
                    Log.e("DKC1", "nativeFrame failed")

                    runOnUiThread {
                        statusText.text = "Game runtime stopped. Check Logcat."
                    }
                    return
                }

                accumulatorNs -= frameStepNs
                ticks++
            }

            if (ticks == maxCatchUpTicks &&
                accumulatorNs >= frameStepNs
            ) {
                accumulatorNs %= frameStepNs
            }

            if (ticks > 0) textureStale = true

            GLES20.glUseProgram(program)

            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture)


            if (textureStale) {
                val nativePixels = nativeGetFrameBuffer() ?: return
                val pixels = nativePixels.duplicate().apply {
                    position(0)
                    limit(gameWidth * gameHeight * 4)
                }

                GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1)
                GLES20.glTexSubImage2D(
                    GLES20.GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    gameWidth,
                    gameHeight,
                    GLES20.GL_RGBA,
                    GLES20.GL_UNSIGNED_BYTE,
                    pixels
                )
                textureStale = false
            }

            GLES20.glUniform1i(textureLocation, 0)
            GLES20.glUniform2f(texSizeLocation, gameWidth.toFloat(), gameHeight.toFloat())
            GLES20.glUniform2f(scaleLocation, scaleX, scaleY)

            vertices.position(0)
            texCoords.position(0)

            GLES20.glEnableVertexAttribArray(positionLocation)
            GLES20.glVertexAttribPointer(
                positionLocation, 2, GLES20.GL_FLOAT,
                false, 0, vertices
            )

            GLES20.glEnableVertexAttribArray(texCoordLocation)
            GLES20.glVertexAttribPointer(
                texCoordLocation, 2, GLES20.GL_FLOAT,
                false, 0, texCoords
            )

            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)

            GLES20.glDisableVertexAttribArray(positionLocation)
            GLES20.glDisableVertexAttribArray(texCoordLocation)
        }

        private fun floatBuffer(values: FloatArray): FloatBuffer {
            return ByteBuffer.allocateDirect(values.size * 4)
                .order(ByteOrder.nativeOrder())
                .asFloatBuffer()
                .put(values)
                .apply { position(0) }
        }

        private fun compileShader(type: Int, source: String): Int {
            val shader = GLES20.glCreateShader(type)
            GLES20.glShaderSource(shader, source)
            GLES20.glCompileShader(shader)

            val compiled = IntArray(1)
            GLES20.glGetShaderiv(
                shader,
                GLES20.GL_COMPILE_STATUS,
                compiled,
                0
            )

            if (compiled[0] == 0) {
                val error = GLES20.glGetShaderInfoLog(shader)
                GLES20.glDeleteShader(shader)
                throw IllegalStateException("Shader compilation failed: $error")
            }

            return shader
        }

        private fun createProgram(vertex: String, fragment: String): Int {
            val vs = compileShader(GLES20.GL_VERTEX_SHADER, vertex)
            val fs = compileShader(GLES20.GL_FRAGMENT_SHADER, fragment)

            val result = GLES20.glCreateProgram()
            GLES20.glAttachShader(result, vs)
            GLES20.glAttachShader(result, fs)
            GLES20.glLinkProgram(result)

            val linked = IntArray(1)
            GLES20.glGetProgramiv(
                result,
                GLES20.GL_LINK_STATUS,
                linked,
                0
            )

            GLES20.glDeleteShader(vs)
            GLES20.glDeleteShader(fs)

            if (linked[0] == 0) {
                val error = GLES20.glGetProgramInfoLog(result)
                GLES20.glDeleteProgram(result)
                throw IllegalStateException("Program linking failed: $error")
            }

            return result
        }
    }


    override fun onPause() {
        if (engineInitialized) {
            touchInputMask = 0
            nativeSetPaused(true)
        }

        glView?.onPause()

        if (engineInitialized) {
            nativeFlushSaves()
        }

        super.onPause()
    }

    override fun onResume() {
        super.onResume()

        if (engineInitialized) {
            frameTimingResetRequested = true
            nativeSetPaused(false)
        }

        glView?.onResume()
    }

    override fun onDestroy() {
        glView?.onPause()

        if (engineInitialized) {
            nativeShutdown()
            engineInitialized = false
        }

        super.onDestroy()
    }

    companion object {
        init {
            System.loadLibrary("dkc1_android")
        }
    }
}
