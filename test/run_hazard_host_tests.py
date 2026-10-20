"""Run transport-independent hazard tests without PlatformIO or ESP32 hardware."""

import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    source = root / "src" / "Hazard Memory Network"
    with tempfile.TemporaryDirectory(prefix="autolink-tests-") as directory:
        binary = str(Path(directory) / "hazard_memory_test")
        subprocess.run(
            [
                os.environ.get("CXX", "clang++"),
                "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-I", str(source),
                str(root / "test" / "unit" / "hazard_memory_test.cpp"),
                str(root / "test" / "unit" / "hazard_storage_test.cpp"),
                str(root / "test" / "unit" / "hazard_lifecycle_test.cpp"),
                str(root / "test" / "unit" / "hazard_sync_test.cpp"),
                str(root / "test" / "unit" / "hazard_evidence_test.cpp"),
                str(root / "test" / "unit" / "hazard_confidence_test.cpp"),
                *[str(source / name) for name in (
                    "Hazard_Record.cpp", "Hazard_Repository.cpp",
                    "Hazard_Service.cpp", "Hazard_Console.cpp", "Hazard_Storage.cpp", "Hazard_Lifecycle.cpp",
                    "Hazard_Sync.cpp", "Hazard_Evidence.cpp", "Hazard_Groups.cpp", "Hazard_Confidence.cpp",
                )],
                "-o", binary,
            ],
            check=True,
        )
        subprocess.run([binary, directory], check=True)
        transport_binary = str(Path(directory) / "lora_transport_test")
        subprocess.run(
            [
                os.environ.get("CXX", "clang++"),
                "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-I", str(root / "test" / "host_stubs"),
                "-I", str(root / "src" / "Core"),
                str(root / "test" / "unit" / "lora_transport_test.cpp"),
                str(root / "src" / "Core" / "LoRa_Transport.cpp"),
                "-o", transport_binary,
            ],
            check=True,
        )
        subprocess.run([transport_binary], check=True)


if __name__ == "__main__":
    main()
