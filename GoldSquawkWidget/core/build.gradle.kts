plugins {
    kotlin("jvm")
}

dependencies {
    // Compile-time only: on-device, Android provides its own org.json implementation
    // at runtime, and bundling this jar into the app APK would collide with it when
    // dex-merging. Tests run on a plain JVM (no Android runtime to fall back on), so
    // they pull in the real implementation separately below.
    compileOnly("org.json:json:20240303")

    testImplementation("org.json:json:20240303")
    testImplementation(platform("org.junit:junit-bom:5.10.2"))
    testImplementation("org.junit.jupiter:junit-jupiter")
}

tasks.test {
    useJUnitPlatform()
}
