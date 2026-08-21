#!/usr/bin/env python3
import shutil
import subprocess
import sysconfig
from pathlib import Path

SYSTEM_LIBRARIES = {
    "ld-linux-aarch64.so.1", "ld-linux-x86-64.so.2", "libc.so.6", "libdl.so.2",
    "libgcc_s.so.1", "libm.so.6", "libpthread.so.0", "librt.so.1",
}


def dependencies(path: Path) -> list[Path]:
    output = subprocess.check_output(["ldd", path], text=True)
    result: list[Path] = []
    for line in output.splitlines():
        words = line.strip().split()
        if "=>" in words:
            index = words.index("=>")
            if index + 1 < len(words) and words[index + 1].startswith("/"):
                result.append(Path(words[index + 1]))
        elif words and words[0].startswith("/"):
            result.append(Path(words[0]))
    return result


def copy_regular(source: Path, target: Path) -> None:
    shutil.copy2(source.resolve(), target)
    if target.is_symlink():
        raise RuntimeError(f"bundle target is a symbolic link: {target}")


def bundle(root: Path, extension: Path) -> None:
    libraries = root / "runtime" / "lib"
    libraries.mkdir(parents=True, exist_ok=True)
    version = sysconfig.get_config_var("VERSION")
    python_source = Path(sysconfig.get_config_var("LIBDIR")) / sysconfig.get_config_var("LDLIBRARY")
    python_real = libraries / f"libpython{version}.so.1.0"
    python_link = libraries / f"libpython{version}.so"
    copy_regular(python_source, python_real)
    copy_regular(python_source, python_link)
    stdlib = Path(sysconfig.get_paths()["stdlib"])
    shutil.copytree(
        stdlib, libraries / stdlib.name, dirs_exist_ok=True,
        ignore=shutil.ignore_patterns("__pycache__", "site-packages", "*.pyc"),
    )

    queue = [extension, python_real]
    copied: dict[str, Path] = {}
    while queue:
        current = queue.pop(0)
        for dependency in dependencies(current):
            if dependency.name in SYSTEM_LIBRARIES or dependency.name in copied:
                continue
            target = libraries / dependency.name
            copy_regular(dependency, target)
            copied[dependency.name] = target
            queue.append(target)

    subprocess.check_call(["patchelf", "--set-rpath", "$ORIGIN/../../runtime/lib", extension])
    for library in [python_real, python_link, *copied.values()]:
        subprocess.check_call(["patchelf", "--set-rpath", "$ORIGIN", library])


if __name__ == "__main__":
    bundle(Path(__import__("sys").argv[1]), Path(__import__("sys").argv[2]))
