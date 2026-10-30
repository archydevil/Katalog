#!/usr/bin/env python3
"""Make Teca.app self-contained after macdeployqt (fork, 2026-10-30).

- copies the external programs the AI analysis runs (ffmpeg, whisper-cli,
  pdftotext, tesseract, exiv2...) into Contents/Helpers, and every library
  they need into Contents/Frameworks;
- copies ggml's compute backends next to whisper-cli: ggml looks for them in
  the folder of the program that runs;
- copies the models into Contents/Resources/models;
- points every library reference at @executable_path/../Frameworks, removes
  the search paths that lead outside the app, and checks that each reference
  resolves inside the app or the system;
- signs every piece ad hoc, from the inside out.

Usage: bundle_tools.py Teca.app --tools ffmpeg ... --backends DIR --models FILE...
"""
import argparse
import os
import shutil
import subprocess
import sys

SYSTEM = ("/System/", "/usr/lib/")
DYLIB_COMMANDS = ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
                  "LC_LAZY_LOAD_DYLIB", "LC_LOAD_UPWARD_DYLIB")
MACHO_MAGIC = (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def is_macho(path):
    if os.path.islink(path) or not os.path.isfile(path):
        return False
    with open(path, "rb") as f:
        return f.read(4) in MACHO_MAGIC


def load_commands(path):
    """The install name, the libraries and the search paths of a Mach-O file."""
    lines = run("otool", "-l", path).splitlines()
    ident, deps, rpaths = None, [], []
    for i, line in enumerate(lines):
        words = line.split()
        if len(words) != 2 or words[0] != "cmd":
            continue
        value = lines[i + 2].strip().split(" (offset")[0].split(None, 1)[1]
        if words[1] == "LC_ID_DYLIB":
            ident = value
        elif words[1] in DYLIB_COMMANDS:
            deps.append(value)
        elif words[1] == "LC_RPATH":
            rpaths.append(value)
    return ident, deps, rpaths


def expand(path, loader, executable_dir):
    if path.startswith("@loader_path"):
        return os.path.join(os.path.dirname(loader), path[len("@loader_path/"):])
    if path.startswith("@executable_path"):
        return os.path.join(executable_dir, path[len("@executable_path/"):])
    return path


def resolve(ref, loader, executable_dir, rpaths):
    """The file a library reference loads, or None."""
    if ref.startswith("@rpath/"):
        for r in rpaths:
            candidate = os.path.join(expand(r, loader, executable_dir), ref[len("@rpath/"):])
            if os.path.exists(candidate):
                return os.path.realpath(candidate)
        return None
    candidate = expand(ref, loader, executable_dir)
    return os.path.realpath(candidate) if os.path.exists(candidate) else None


def bundle_program(src, dest, frameworks, copied):
    """Copy a program or library and, through it, every library it needs."""
    queue = [(os.path.realpath(src), dest, [])]
    while queue:
        src, dest, chain = queue.pop()
        if dest in copied:
            continue
        copied.add(dest)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copyfile(src, dest)
        os.chmod(dest, 0o755)
        ident, deps, rpaths = load_commands(src)
        search = rpaths + chain
        args = []
        for ref in deps:
            if ref.startswith(SYSTEM):
                continue
            real = resolve(ref, src, os.path.dirname(src), search)
            if real is None:
                sys.exit(f"not found: {ref} (needed by {src})")
            name = os.path.basename(ref)
            args += ["-change", ref, "@executable_path/../Frameworks/" + name]
            target = os.path.join(frameworks, name)
            # A library macdeployqt already placed is the same Homebrew file
            if not os.path.exists(target):
                queue.append((real, target, [expand(r, src, os.path.dirname(src)) for r in search]))
        if ident is not None:
            args += ["-id", "@executable_path/../Frameworks/" + os.path.basename(dest)]
        for r in dict.fromkeys(rpaths):
            args += ["-delete_rpath", r]
        if args:
            run("install_name_tool", *args, dest)


def outside(path, app):
    return path.startswith("/") and not path.startswith(SYSTEM) and not os.path.realpath(path).startswith(app)


def clean(app, files):
    """Remove the search paths and install names that lead outside the app.

    The libraries macdeployqt placed may reach each other through a search
    path that led to Homebrew: inside the app they sit side by side, so the
    library's own folder becomes their search path. The programs in Helpers
    load them too, without the app's search paths."""
    frameworks = os.path.join(app, "Contents", "Frameworks")
    for f in files:
        ident, deps, rpaths = load_commands(f)
        args = []
        kept = []
        for r in dict.fromkeys(rpaths):
            if r.startswith("$ORIGIN") or outside(r, app):
                args += ["-delete_rpath", r]
            else:
                kept.append(r.rstrip("/"))
        if (os.path.dirname(f) == frameworks and any(d.startswith("@rpath/") for d in deps)
                and "@loader_path" not in kept and "@loader_path/." not in kept):
            args += ["-add_rpath", "@loader_path"]
        if ident is not None and outside(ident, app):
            args += ["-id", "@executable_path/../Frameworks/" + os.path.basename(f)]
        if args:
            run("install_name_tool", *args, f)


def verify(app, files):
    """Load each program as dyld does: every reference resolves inside the app
    or the system, through the image's own search paths and the program's."""
    contents = os.path.join(app, "Contents")
    helpers = os.path.join(contents, "Helpers")
    commands = {f: load_commands(f) for f in files}
    mains = [f for f in files if os.path.dirname(f) == os.path.join(contents, "MacOS")]
    backends = [f for f in files if os.path.dirname(f) == helpers and f.endswith(".so")]
    contexts = [(m, [m] + [f for f in files if not f.startswith(helpers + "/") and f not in mains]) for m in mains]
    for f in files:
        if os.path.dirname(f) == helpers and not f.endswith(".so"):
            contexts.append((f, [f] + (backends if os.path.basename(f) == "whisper-cli" else [])))

    problems = set()
    for f, (_, _, rpaths) in commands.items():
        for r in rpaths:
            if outside(r, app):
                problems.add(f"{os.path.relpath(f, app)} rpath {r}")
    for exe, roots in contexts:
        exe_dir = os.path.dirname(exe)
        exe_rpaths = [expand(r, exe, exe_dir) for r in commands[exe][2]]
        seen, queue = set(), list(roots)
        while queue:
            image = queue.pop()
            if image in seen:
                continue
            seen.add(image)
            _, deps, rpaths = commands.get(image) or load_commands(image)
            for ref in deps:
                if ref.startswith(SYSTEM):
                    continue
                real = resolve(ref, image, exe_dir, rpaths + exe_rpaths)
                if real is None or not real.startswith(app):
                    problems.add(f"{os.path.basename(exe)}: {os.path.relpath(image, app)} -> {ref}")
                elif real not in seen:
                    queue.append(real)
    return sorted(problems)


def sign(app, files):
    """Ad-hoc signatures, inside out: loose code, frameworks, then the app."""
    frameworks = set()
    for f in sorted(files, key=lambda p: -p.count("/")):
        if ".framework/" in f:
            frameworks.add(f[:f.index(".framework/") + len(".framework")])
            continue
        run("codesign", "--force", "--sign", "-", "--timestamp=none", f)
    for fw in sorted(frameworks):
        run("codesign", "--force", "--sign", "-", "--timestamp=none", fw)
    run("codesign", "--force", "--sign", "-", "--timestamp=none", app)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("app")
    parser.add_argument("--tools", nargs="*", default=[])
    parser.add_argument("--backends", help="folder of ggml's backends (libggml-*.so)")
    parser.add_argument("--models", nargs="*", default=[], help="model files or folders")
    args = parser.parse_args()

    app = os.path.realpath(args.app)
    contents = os.path.join(app, "Contents")
    frameworks = os.path.join(contents, "Frameworks")
    helpers = os.path.join(contents, "Helpers")
    copied = set()

    for tool in args.tools:
        src = shutil.which(tool, path="/opt/homebrew/bin:/usr/local/bin")
        if src is None:
            sys.exit(f"program not found: {tool}")
        bundle_program(src, os.path.join(helpers, tool), frameworks, copied)
        print(f"program: {tool}")
    if args.backends:
        for name in sorted(os.listdir(args.backends)):
            if name.startswith("libggml-") and name.endswith(".so"):
                bundle_program(os.path.join(args.backends, name), os.path.join(helpers, name), frameworks, copied)
                print(f"backend: {name}")

    models = os.path.join(contents, "Resources", "models")
    for m in args.models:
        target = os.path.join(models, os.path.basename(m.rstrip("/")))
        os.makedirs(models, exist_ok=True)
        if os.path.isdir(m):
            shutil.copytree(m, target, dirs_exist_ok=True)
        else:
            shutil.copyfile(m, target)
        print(f"model: {os.path.basename(m.rstrip('/'))}")

    files = [os.path.join(d, n) for d, _, names in os.walk(app) for n in names]
    files = [f for f in files if is_macho(f)]
    clean(app, files)
    problems = verify(app, files)
    if problems:
        print("\n".join(problems))
        sys.exit(f"{len(problems)} reference(s) lead outside the app")
    print(f"checked: {len(files)} binaries, every reference inside the app or the system")
    sign(app, files)
    print("signed (ad hoc)")


if __name__ == "__main__":
    main()
