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
        private val delays = mutableMapOf<Runnable, Long>()
        fun runWithDelay(delayMillis: Long): Boolean {
            val next = delayed.firstOrNull { delays[it] == delayMillis } ?: return false
            delayed.remove(next)
            delays.remove(next)
            next.run()
            return true
        }
        fun runNext(): Boolean {
            val next = delayed.removeFirstOrNull() ?: return false
            next.run()
            return true
        }
        fun clear() { delayed.clear(); delays.clear() }
    }
    fun post(action: Runnable): Boolean { action.run(); return true }
    fun postDelayed(action: Runnable, @Suppress("UNUSED_PARAMETER") delayMillis: Long): Boolean {
        delayed.addLast(action); delays[action] = delayMillis; return true
    }
    fun removeCallbacks(action: Runnable) { delayed.remove(action); delays.remove(action) }
}
