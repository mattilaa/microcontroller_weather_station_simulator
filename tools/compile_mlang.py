"""Compile MLang LLVM IR for the freestanding AArch64 firmware.

MLang selects a hosted target triple and emits exception frames by default.
Supply the non-throwing runtime in the same IR module so LLVM can eliminate
those frames. No hosted MLang runtime or heap is linked into the firmware.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output).resolve()
    ir = output.with_suffix(".ll")
    env = os.environ.copy()
    env["MLANG_MODULE_PATH"] = str(Path(__file__).resolve().parents[1] / "src")
    subprocess.run(
        [str(Path(args.compiler).resolve()), "--target-arch", "aarch64", "-O2", "-emit-llvm",
         str(Path(args.source).resolve()), "-o", str(ir)],
        cwd=output.parent, env=env, check=True,
    )
    text = ir.read_text()
    text = re.sub(r'^target (?:datalayout|triple) = .*\n', "", text, flags=re.M)
    # These functions never throw. Unexpected failure paths halt through the
    # firmware panic handler rather than linking a hosted exception runtime.
    definitions = {
        "__mlang_std_exceptions_push_frame": "define internal i64 @__mlang_std_exceptions_push_frame() { ret i64 0 }",
        "__mlang_std_exceptions_frame_env": "define internal ptr @__mlang_std_exceptions_frame_env(i64 %frame) { ret ptr null }",
        "_setjmp": "define internal i32 @_setjmp(ptr %env) { ret i32 0 }",
        "__mlang_std_exceptions_pop_frame": "define internal void @__mlang_std_exceptions_pop_frame(i64 %frame) { ret void }",
        "__mlang_std_exceptions_rethrow_current": "define internal void @__mlang_std_exceptions_rethrow_current() { call void @abort() unreachable }",
    }
    for symbol, definition in definitions.items():
        text = re.sub(r"^declare [^\n]*@" + re.escape(symbol) + r"\([^\n]*\)[^\n]*$",
                      definition, text, flags=re.M)
    # A source module may have no checked accesses, so its original IR can omit
    # abort even though the non-throwing rethrow shim above references it.
    if "@abort(" in text and not re.search(r"^(?:declare|define) [^@\n]*@abort\(", text, re.M):
        text += "\ndeclare void @abort()\n"
    # Null-pointer diagnostics use hosted stdio. The failure still calls
    # abort(), whose firmware implementation reports the panic over UART.
    text = re.sub(r"^@(stderr|__stderrp) = .*",
                  r"@\1 = internal global ptr null", text, flags=re.M)
    text = re.sub(r"^declare [^\n]*@fwrite\([^\n]*$",
                  "define internal i64 @fwrite(ptr %s, i64 %size, i64 %n, ptr %stream) { ret i64 0 }",
                  text, flags=re.M)
    if re.search(r"\bcall\b[^\n]*@(?:malloc|calloc|realloc)\(", text):
        raise RuntimeError("MLang firmware must not allocate heap memory")
    ir.write_text(text)
    subprocess.run(
        [args.clang, "--target=aarch64-none-elf", "-mcpu=cortex-a53",
         "-mgeneral-regs-only", "-mstrict-align", "-ffreestanding", "-fno-builtin",
         "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
         "-ffunction-sections", "-fdata-sections", "-Wno-override-module",
         "-Oz", "-c", str(ir), "-o", str(output)], check=True,
    )


if __name__ == "__main__":
    main()
