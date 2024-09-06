"""
We use Binaryen.py to:
(1) Remove exports of globals
(2) Optimize the wasm module, to reduce the number of globals


Reference: binaryen:
    binaryen.py           : https://github.com/jonathanharg/binaryen.py/tree/main
    Binaryen C header file: https://github.com/WebAssembly/binaryen/blob/main/src/binaryen-c.h

    Reference the Binaryen header file to understand how to use Binaryen. 
    This package makes no significant changes to how Binaryen is used. 
    The majority of the work this module does is interoperating between 
    C and Python and creating more pythonic classes for Modules, Expressions and Functions.

    You can still call any missing functions that haven't been implemented by 
    the wrapper yet by calling them directly. To do this use 
    binaryen.lib.BinaryenFullFunctionName() and call the full function name 
    as described in the Binaryen header file.
"""
from pathlib import Path
import shutil
from icpp import icpp_toml

import binaryen
from binaryen import ffi, lib

def load_wasm(wasm_path: Path) -> binaryen.module.Module:
    """Load a WebAssembly binary into a binaryen module"""

    # Read the binary wasm into a bytes array
    wasm_bytes = open(wasm_path, "rb").read()

    # Convert the binary data to a C data type that Binaryen can understand
    wasm_ptr = ffi.new("char[]", wasm_bytes)
    wasm_size = len(wasm_bytes)

    # Use Binaryen's C API to read the module from the binary
    module_ref = lib.BinaryenModuleRead(wasm_ptr, wasm_size)

    # Wrap the module_ref in a Python object so you can use it with the rest of binaryen.py's API
    module = binaryen.Module()
    module.ref = module_ref

    return module

def remove_exports_of_globals(module: binaryen.module.Module) -> None:
    """Remove all exports of globals"""
    
    # Constant value for BinaryenExternalKindGlobal
    BinaryenExternalKindGlobal = 3

    # Iterate over the exports and collect the names of all global exports
    exports_to_remove = []
    for i in range(lib.BinaryenGetNumExports(module.ref)):
        export_ref = lib.BinaryenGetExportByIndex(module.ref, i)
        if lib.BinaryenExportGetKind(export_ref) == BinaryenExternalKindGlobal:
            export_name = ffi.string(lib.BinaryenExportGetName(export_ref)).decode('utf-8')
            exports_to_remove.append(export_name)

    # Remove the global exports
    for export_name in exports_to_remove:
        lib.BinaryenRemoveExport(module.ref, export_name.encode('utf-8'))

def optimize(module: binaryen.module.Module, shrink_level:int, optimize_level:int) -> None:
    """Optimize the wasm"""

    # From binaryen-c.h
    # Sets the shrink level to use. Applies to all modules, globally.
    # 0, 1, 2 correspond to -O0, -Os (default), -Oz.
    # BINARYEN_API void BinaryenSetShrinkLevel(int level);
    lib.BinaryenSetShrinkLevel(shrink_level)

    # From binaryen-c.h
    # Sets the optimization level to use. Applies to all modules, globally.
    # 0, 1, 2 correspond to -O0, -O1, -O2 (default), etc.
    # BINARYEN_API void BinaryenSetOptimizeLevel(int level);
    lib.BinaryenSetOptimizeLevel(optimize_level)

    # Optimize it
    module.optimize()

def write_wasm(module: binaryen.module.Module, wasm_path: Path) -> None:
    """Write the modified module back to a file"""
    with open(wasm_path, "wb") as f:
        f.write(module.emit_binary())


def main() -> None:
    """Run optimization passes on binary wasm file"""

    build_path = icpp_toml.icpp_toml_path.parent / "build"
    wasm_path = (build_path / f"{icpp_toml.build_wasm['canister']}.wasm").resolve()

    # save the original version
    wasm_path_orig = wasm_path.with_stem(wasm_path.stem + "_before_opt").resolve()
    shutil.copy(wasm_path, wasm_path_orig)

    # optimize the wasm
    module = load_wasm(wasm_path)
    
    num_exports_before = lib.BinaryenGetNumExports(module.ref)
    num_globals_before = lib.BinaryenGetNumGlobals(module.ref)

    remove_exports_of_globals(module)
    optimize(module, shrink_level=0, optimize_level=0)
    write_wasm(module, wasm_path)

    # summarize the optimization result
    num_exports_after = lib.BinaryenGetNumExports(module.ref)
    num_globals_after = lib.BinaryenGetNumGlobals(module.ref)

    print(f"Exports before optimization: {num_exports_before}")
    print(f"Exports after  optimization: {num_exports_after}")
    print(f"Globals before optimization: {num_globals_before}")
    print(f"Globals after  optimization: {num_globals_after}")

if __name__ == "__main__":
    # For debugging without running `icpp build-wasm`, 
    # (-) make sure to run this from the root folder, as:
    #     python -m scripts.optimize_wasm
    #     -> That way, import icpp_toml works correctly and
    #        all values will be set
    # (-) note that this overwrites `build/<canister_name>.wasm`
    #
    main()
