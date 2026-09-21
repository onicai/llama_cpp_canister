"""Removes exported globals & runs the Binaryen optimizer (IC0505 fix).

The IC rejects a wasm module with more than 1000 DEFINED globals. A wasi-sdk
build of llama.cpp blows past that, so every global-kind export is removed and
Binaryen's optimizer drops the now-dead globals.

Thin wrapper around icpp-binaryen; the _before_opt backup keeps the wasm name
section for named backtraces under scripts/wasm_harness.py.
"""

from icpp import icpp_toml

from icpp_binaryen import fix_globals_limit


def main() -> None:
    """Run the globals-limit fix on the built wasm."""

    build_path = icpp_toml.icpp_toml_path.parent / "build"
    wasm_path = (build_path / f"{icpp_toml.build_wasm['canister']}.wasm").resolve()

    report = fix_globals_limit(wasm_path)  # writes llama_cpp_before_opt.wasm
    print(report.summary())


if __name__ == "__main__":
    # For debugging without running `icpp build-wasm`,
    # (-) make sure to run this from the root folder, as:
    #     python -m scripts.optimize_wasm
    #     -> That way, import icpp_toml works correctly and
    #        all values will be set
    # (-) note that this overwrites `build/<canister_name>.wasm`
    #
    main()
