"""Compile and run the real OTA session/coordinator on a host JVM.

Uses cached Kotlin artifacts from GRADLE_USER_HOME. No Gradle task, APK,
Android device, network access, or new dependency is involved.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
GRADLE_HOME = Path(os.environ.get("GRADLE_USER_HOME", Path.home() / ".gradle"))
CACHE = GRADLE_HOME / "caches" / "modules-2" / "files-2.1"
VERSION = "2.2.20"  # app/android/build.gradle.kts
SOURCES = [
    ROOT / "android/app/src/main/java/org/framefilm/ark/ArkSession.kt",
    ROOT / "android/app/src/main/java/org/framefilm/ark/DirectTransferCoordinator.kt",
    ROOT / "android/app/src/main/java/org/framefilm/ark/TransferSnapshot.kt",
    ROOT / "android/app/src/main/java/org/framefilm/ark/ArkFirmware.kt",
    ROOT / "android/app/src/main/java/org/framefilm/ark/FileTransferPath.kt",
    HERE / "stubs/android/content/Context.kt",
    HERE / "stubs/android/bluetooth/BluetoothDevice.kt",
    HERE / "stubs/android/os/Looper.kt",
    HERE / "stubs/org/framefilm/ark/TransportFakes.kt",
    HERE / "OtaRecoveryHostTest.kt",
]


def cached_file(group: str, artifact: str, version: str, suffix: str) -> Path:
    directory = CACHE / group / artifact / version
    matches = list(directory.rglob(f"{artifact}-{version}.{suffix}"))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one cached {artifact}:{version} {suffix} in {directory}")
    return matches[0]


def compiler_runtime() -> tuple[Path, list[Path]]:
    compiler = cached_file("org.jetbrains.kotlin", "kotlin-compiler-embeddable", VERSION, "jar")
    pom = cached_file("org.jetbrains.kotlin", "kotlin-compiler-embeddable", VERSION, "pom")
    namespace = {"m": "http://maven.apache.org/POM/4.0.0"}
    dependencies = []
    for dependency in ET.parse(pom).findall("m:dependencies/m:dependency", namespace):
        def field(name: str) -> str:
            return dependency.findtext(f"m:{name}", default="", namespaces=namespace)
        if field("scope") in ("", "compile", "runtime"):
            dependencies.append(cached_file(field("groupId"), field("artifactId"),
                                            field("version"), "jar"))
    return compiler, dependencies


def run() -> None:
    java = shutil.which("java")
    if not java:
        raise RuntimeError("Java executable was not found")
    if any(not source.is_file() for source in SOURCES):
        raise RuntimeError("A host test or product Kotlin source file is missing")
    compiler, dependencies = compiler_runtime()
    stdlib = next((item for item in dependencies if item.name == f"kotlin-stdlib-{VERSION}.jar"), None)
    if stdlib is None:
        raise RuntimeError("Compiler POM did not declare kotlin-stdlib")
    # Include cached annotations jars needed by Kotlin codegen without
    # guessing an annotations version that the compiler POM does not declare.
    annotations = sorted((CACHE / "org.jetbrains" / "annotations").rglob("annotations-*.jar"))
    if not annotations:
        raise RuntimeError("No cached org.jetbrains:annotations jar was found")
    compiler_cp = os.pathsep.join(map(str, [compiler, *dependencies, *annotations]))
    source_cp = os.pathsep.join(map(str, [stdlib, *annotations]))
    with tempfile.TemporaryDirectory(prefix="framefilm-ota-host-") as temporary:
        output = Path(temporary) / "classes"
        output.mkdir()
        subprocess.run(
            [java, "-cp", compiler_cp, "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler",
             "-no-stdlib", "-no-reflect", "-classpath", source_cp,
             "-d", str(output), *map(str, SOURCES)],
            check=True,
            cwd=ROOT,
        )
        subprocess.run(
            [java, "-cp", os.pathsep.join(map(str, [output, stdlib])),
             "org.framefilm.ark.OtaRecoveryHostTestKt"],
            check=True,
            cwd=ROOT,
        )


if __name__ == "__main__":
    try:
        run()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"OTA host regression failed: {error}", file=sys.stderr)
        raise SystemExit(1)
