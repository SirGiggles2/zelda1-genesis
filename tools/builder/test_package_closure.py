"""Verify measured compiler dependency closure with real GCC, no game data."""
import subprocess
import tempfile
from pathlib import Path
from package_closure import compiler_inputs


def main():
    with tempfile.TemporaryDirectory(prefix="closure with spaces ") as temp:
        tree = Path(temp)
        out = tree / "build/debug_project/out"
        out.mkdir(parents=True)
        (tree / "used header.h").write_text("#define VALUE 7\n")
        (tree / "unused.h").write_text("#error should not be read\n")
        source = tree / "source.c"
        source.write_text('#include "used header.h"\nint result(void) {return VALUE;}\n')
        dep = out / "source.d"
        obj = out / "source.o"
        subprocess.run(["gcc", "-c", str(source), "-o", str(obj), "-MMD", "-MF", str(dep)], check=True)
        expected = {"source.c", "used header.h"}
        assert compiler_inputs(tree) == expected
        # Wine's Z: alias represents the same host files in GCC depfiles.
        dep.write_text(dep.read_text().replace(tree.as_posix(), "Z:" + tree.as_posix()))
        assert compiler_inputs(tree) == expected
        obj.unlink()
        assert compiler_inputs(tree) == set()
    print("package_closure dependency controls: PASS (real GCC, spaces, Wine alias, unused header, missing object)")


if __name__ == "__main__":
    main()
