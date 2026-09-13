package com.espsquawk.goldsquawk

import android.app.Application

class GoldSquawkApp : Application() {
    override fun onCreate() {
        super.onCreate()
        NotificationHelper.createChannel(this)
    }
}
