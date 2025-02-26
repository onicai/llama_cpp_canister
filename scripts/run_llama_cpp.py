"""Helper function to run llama.cpp from the command line using subprocess"""

import subprocess


def run_llama_cpp(
    llama_cli_path: str,
    model: str,
    prompt: str,
    num_tokens: int,
    seed: int,
    temp: float,
    # top_k : float,
    # top_p : float,
    # min_p : float,
    # tfs : float,
    # typical : float,
    # mirostat : float,
    # mirostat_lr : float,
    # mirostat_ent : float,
    repeat_penalty: float,
    cache_type_k: str,
    # cache_type_v : str,
    # defrag_thold : float,
) -> None:
    """Helper function to run llama.cpp from the command line using subprocess"""
    command = [
        llama_cli_path,
        "-m",
        model,
        "--no-warmup",  # needed when running from CLI. Is default for llama_cpp_canister
        "-no-cnv",  # needed when running from CLI. Is default for llama_cpp_canister
        # "--simple-io",
        # "--no-display-prompt",  # only return generated text, no special characters
        "-sp",  # output special tokens
        "-n",
        f"{num_tokens}",
        "--seed",
        f"{seed}",
        "--temp",
        f"{temp}",
        # "--top-k",
        # f"{top_k}",
        # "--top-p",
        # f"{top_p}",
        # "--min-p",
        # f"{min_p}",
        # "--tfs",
        # f"{tfs}",
        # "--typical",
        # f"{typical}",
        # "--mirostat",
        # f"{mirostat}",
        # "--mirostat-lr",
        # f"{mirostat_lr}",
        # "--mirostat-ent",
        # f"{mirostat_ent}",
        "--repeat-penalty",
        f"{repeat_penalty}",
        "--cache-type-k",
        f"{cache_type_k}",
        # "--cache-type-v", # CPU only
        # f"{cache_type_v}",
        # "--defrag-thold", # No impact
        # f"{defrag_thold}",
        "-p",
        prompt,
    ]

    # Print the command on a single line for terminal use, preserving \n
    print(
        "\nCommand:\n",
        f"{llama_cli_path} -m {model} "
        f"--no-warmup -no-cnv -sp "
        f"-n {num_tokens} "
        f"--seed {seed} "
        f"--temp {temp} "
        f"--repeat-penalty {repeat_penalty} "
        f"--cache-type-k {cache_type_k} "
        f"-p '{prompt}'".replace("\n", "\\n"),
    )

    subprocess.run(command, text=True, check=True)
