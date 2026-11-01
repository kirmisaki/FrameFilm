package android.os

class Looper private constructor() {
    companion object {
        private val main = Looper()
        fun getMainLooper(): Looper = main
        fun myLooper(): Looper = main
    }
}

class Handler(@Suppress("UNUSED_PARAMETER") looper: Looper) {
    companion object {
        val delayed = ArrayDeque<Runnable>()
        fun runNext(): Boolean {
            val next = delayed.removeFirstOrNull() ?: return false
            next.run()
            return true
        }
        fun clear() = delayed.clear()
    }
    fun post(action: Runnable): Boolean { action.run(); return true }
    fun postDelayed(action: Runnable, @Suppress("UNUSED_PARAMETER") delayMillis: Long): Boolean {
        delayed.addLast(action); return true
    }
    fun removeCallbacks(action: Runnable) { delayed.remove(action) }
}
