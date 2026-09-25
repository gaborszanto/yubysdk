plugins {
    alias(libs.plugins.android.application)
}

val generatedDemoAssets = layout.buildDirectory.dir("generated/demoAssets")
val prepareDemoAssets = tasks.register<Copy>("prepareDemoAssets") {
    from(rootProject.layout.projectDirectory.file("../tropical-breeze.mp3"))
    into(generatedDemoAssets)
}

android {
    namespace = "com.example.yubydemo"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "com.example.yubydemo"
        minSdk = 27
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    buildTypes {
        release {
            optimization {
                enable = false
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures {
        viewBinding = true
    }
    sourceSets.getByName("main").assets.directories.add(generatedDemoAssets.get().asFile.absolutePath)
}

tasks.named("preBuild").configure {
    dependsOn(prepareDemoAssets)
}

dependencies {
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)
    implementation(libs.androidx.core.ktx)
    implementation(libs.material)
}
