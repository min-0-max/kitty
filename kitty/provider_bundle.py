#!/usr/bin/env python3
import os, re, shutil, subprocess, sysconfig
from pathlib import Path

def dependencies(path: Path) -> list[Path]:
    output = subprocess.check_output(["otool", "-L", path], text=True)
    result = []
    for line in output.splitlines()[1:]:
        name = line.strip().split(" (", 1)[0]
        if not name.startswith(("/", "@")) or name.startswith(("/System/", "/usr/lib/", "@")) or name == str(path): continue
        result.append(Path(name))
    return result

def bundle(root: Path, extension: Path) -> None:
    runtime = root / "runtime"; libraries = runtime / "lib"
    runtime.mkdir(parents=True, exist_ok=True); libraries.mkdir(parents=True, exist_ok=True)
    python = Path(sysconfig.get_config_var("LIBDIR")).parent / "Python"
    bundled_python = libraries / f"libpython{sysconfig.get_config_var('VERSION')}.dylib"
    shutil.copy2(python, bundled_python)
    stdlib = Path(sysconfig.get_paths()["stdlib"])
    shutil.copytree(stdlib, runtime / "lib" / stdlib.name, dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "site-packages", "*.pyc"))
    queue = [item for item in dependencies(extension) if item != python]; copied: dict[str, Path] = {}
    while queue:
        source = queue.pop(0)
        if source.name in copied: continue
        target = libraries / source.name; shutil.copy2(source, target); copied[source.name] = target
        queue.extend(item for item in dependencies(source) if item != python)
    subprocess.check_call(["install_name_tool", "-change", str(python), f"@loader_path/../../runtime/lib/{bundled_python.name}", extension])
    subprocess.check_call(["install_name_tool", "-id", f"@rpath/{bundled_python.name}", bundled_python])
    for source_name, target in copied.items():
        for dependency in dependencies(target):
            if dependency.name in copied:
                subprocess.check_call(["install_name_tool", "-change", str(dependency), f"@loader_path/{dependency.name}", target])
        subprocess.check_call(["install_name_tool", "-id", f"@loader_path/{source_name}", target])
    original = subprocess.check_output(["otool", "-L", extension], text=True)
    for source in [line.strip().split(" (", 1)[0] for line in original.splitlines()[1:]]:
        name = Path(source).name
        if name in copied:
            subprocess.check_call(["install_name_tool", "-change", source, f"@loader_path/../../runtime/lib/{name}", extension])
    for binary in [bundled_python, *copied.values(), extension]:
        subprocess.check_call(["codesign", "--force", "--sign", "-", binary])

if __name__ == "__main__": bundle(Path(__import__("sys").argv[1]), Path(__import__("sys").argv[2]))
