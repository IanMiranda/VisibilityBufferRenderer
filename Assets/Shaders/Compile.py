from pathlib import Path
import subprocess
import os

COMPILE_CMD = "slangc -target spirv -profile spirv_1_4 -g -matrix-layout-column-major -fvk-use-entrypoint-name -force-glsl-scalar-layout -lang glsl -o"
IGNORE_LIST = ["Lighting.slang", "Common.slang", "Math.slang", "Deferred.slang"]

print(f"Compiling command: {COMPILE_CMD}")
for path in sorted(Path('.').glob("*.slang")):
    if not path.is_file(): continue
    if path.name in IGNORE_LIST: continue
    
    bin_path = (Path(".") / "Bin" / path).with_suffix(".spv")
    cmd = f"{COMPILE_CMD} {bin_path} {path}"
    print(f"Compiling {path} -> {bin_path}...")
    subprocess.run(cmd.split())