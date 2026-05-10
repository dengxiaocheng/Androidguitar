#!/bin/sh
# Gradle wrapper - uses Android Studio's Gradle

GRADLE_HOME="D:/Android/SDK/Sdk"
# Find gradle
if [ -f "/c/Users/Administrator/.gradle/wrapper/dists/gradle-8.9-bin/"*"/gradle-8.9/bin/gradle" ]; then
    GRADLE="/c/Users/Administrator/.gradle/wrapper/dists/gradle-8.9-bin/"*"/gradle-8.9/bin/gradle"
fi

# Simple build script
echo "Building GuitarFXPro..."
cd "$(dirname "$0")"

# Try gradle directly
for g in "/c/Users/Administrator/.gradle/wrapper/dists/gradle-8.9-bin"/*/gradle-8.9/bin/gradle.bat; do
    if [ -f "$g" ]; then
        echo "Found gradle at: $g"
        "$g" assembleDebug 2>&1
        exit $?
    fi
done

echo "Gradle not found in wrapper cache. Downloading..."
echo "Check Android Studio's Gradle location"
