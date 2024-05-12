"""Calculates the require resources to deploy a Llama2 model to an IC canister"""

# pylint: disable=invalid-name
import sys
import struct
from pathlib import Path
from typing import TextIO

ROOT_PATH = Path(__file__).parent.parent

# For 32 bit system
SIZE_OF_FLOAT = 4  # bytes
SIZE_OF_POINTER = 4  # bytes
SIZE_OF_BYTE_PIECES = 512  # bytes (static size)


def read_config_from_file(file_path: Path) -> dict[str, int]:
    """
    Reads the Config structure from a binary file and returns it as a dictionary.
    """
    with open(file_path, "rb") as f:
        # Read the data corresponding to the Config struct
        data: bytes = f.read(struct.calcsize("7i"))
        config_values = struct.unpack("7i", data)

        config: dict[str, int] = {
            "dim": config_values[0],
            "hidden_dim": config_values[1],
            "n_layers": config_values[2],
            "n_heads": config_values[3],
            "n_kv_heads": config_values[4],
            "vocab_size": abs(
                config_values[5]
            ),  # account for possible negative vocab_size
            "seq_len": config_values[6],
        }
    return config


def calculate_memory(config: dict[str, int]) -> dict[str, dict[str, float]]:
    """Calculate required memory for all the LLM components"""
    # Tokenizer
    vocab_memory = config["vocab_size"] * SIZE_OF_POINTER
    vocab_scores_memory = config["vocab_size"] * SIZE_OF_FLOAT

    # TransformerWeights
    head_size = config["dim"] / config["n_heads"]
    n_layers = config["n_layers"]

    token_embedding_table = config["vocab_size"] * config["dim"] * SIZE_OF_FLOAT
    rms_att_weight = n_layers * config["dim"] * SIZE_OF_FLOAT
    wq = n_layers * config["dim"] * (config["n_heads"] * head_size) * SIZE_OF_FLOAT
    wk = n_layers * config["dim"] * (config["n_kv_heads"] * head_size) * SIZE_OF_FLOAT
    wv = wk  # Same as wk
    wo = n_layers * (config["n_heads"] * head_size) * config["dim"] * SIZE_OF_FLOAT
    rms_ffn_weight = n_layers * config["dim"] * SIZE_OF_FLOAT
    w1 = n_layers * config["dim"] * config["hidden_dim"] * SIZE_OF_FLOAT
    w2 = n_layers * config["hidden_dim"] * config["dim"] * SIZE_OF_FLOAT
    w3 = w1  # Same as w1
    rms_final_weight = config["dim"] * SIZE_OF_FLOAT
    wcls = config["vocab_size"] * config["dim"] * SIZE_OF_FLOAT

    # RunState
    kv_dim = (config["dim"] * config["n_kv_heads"]) / config["n_heads"]
    x = config["dim"] * SIZE_OF_FLOAT
    xb = x  # Same as x
    xb2 = x  # Same as x
    hb = config["hidden_dim"] * SIZE_OF_FLOAT
    hb2 = hb  # Same as hb
    q = x  # Same as x
    k = kv_dim * SIZE_OF_FLOAT
    v = k  # Same as k
    att = config["n_heads"] * config["seq_len"] * SIZE_OF_FLOAT
    logits = config["vocab_size"] * SIZE_OF_FLOAT
    key_cache = n_layers * config["seq_len"] * kv_dim * SIZE_OF_FLOAT
    value_cache = key_cache  # Same as key_cache

    # Calculate total memory usage for Tokenizer, TransformerWeights and RunState
    total_tokenizer = vocab_memory + vocab_scores_memory + SIZE_OF_BYTE_PIECES

    total_transformer_weights = sum(
        [
            token_embedding_table,
            rms_att_weight,
            wq,
            wk,
            wv,
            wo,
            rms_ffn_weight,
            w1,
            w2,
            w3,
            rms_final_weight,
            wcls,
        ]
    )
    total_run_state = sum(
        [x, xb, xb2, hb, hb2, q, k, v, att, logits, key_cache, value_cache]
    )

    # Collate the results in a dictionary
    data: dict[str, dict[str, float]] = {
        "Tokenizer Memory (per model)": {
            "vocab_memory": vocab_memory / (1024 * 1024),
            "vocab_scores_memory": vocab_scores_memory / (1024 * 1024),
        },
        "TransformerWeights Memory (per model)": {
            "token_embedding_table": token_embedding_table / (1024 * 1024),
            "rms_att_weight": rms_att_weight / (1024 * 1024),
            "wq": wq / (1024 * 1024),
            "wk": wk / (1024 * 1024),
            "wv": wv / (1024 * 1024),
            "wo": wo / (1024 * 1024),
            "rms_ffn_weight": rms_ffn_weight / (1024 * 1024),
            "w1": w1 / (1024 * 1024),
            "w2": w2 / (1024 * 1024),
            "w3": w3 / (1024 * 1024),
            "rms_final_weight": rms_final_weight / (1024 * 1024),
            "wcls": wcls / (1024 * 1024),
        },
        "RunState Memory (per user)": {
            "x": x / (1024 * 1024),
            "xb": xb / (1024 * 1024),
            "xb2": xb2 / (1024 * 1024),
            "hb": hb / (1024 * 1024),
            "hb2": hb2 / (1024 * 1024),
            "q": q / (1024 * 1024),
            "k": k / (1024 * 1024),
            "v": v / (1024 * 1024),
            "att": att / (1024 * 1024),
            "logits": logits / (1024 * 1024),
            "key_cache": key_cache / (1024 * 1024),
            "value_cache": value_cache / (1024 * 1024),
        },
        "Total Memory": {
            "Total Tokenizer Memory (per model)": total_tokenizer / (1024 * 1024),
            "Total TransformerWeights Memory (per model)": total_transformer_weights
            / (1024 * 1024),
            "Total RunState Memory (per user)": total_run_state / (1024 * 1024),
            "Overall Total Memory": (total_transformer_weights + total_run_state)
            / (1024 * 1024),
        },
    }
    return data


def write_data(file: TextIO, title: str, data: dict[str, dict[str, float]]) -> None:
    """Writes it all to a Markdown file"""
    # Get the models for headers
    headers = ["Memory Type"] + [f"{model}<br>(MB)" for model in data.keys()]

    # Write the table name
    file.write(f"### {title}\n\n")

    # Write the headers
    file.write(" | ".join(headers) + "\n")
    file.write(" | ".join(["---"] * len(headers)) + "\n")

    # Assuming that all models have the same memory types,
    # using the first model to get the list of memory types
    memory_types = list(data[next(iter(data))].keys())

    totals = {model: 0.0 for model in data.keys()}

    for mtype in memory_types:
        row_data = [mtype] + [
            f"{model_data[mtype]:.2f}" for model_data in data.values()
        ]
        file.write(" | ".join(row_data) + "\n")

        # Accumulate totals for the first three tables
        if title in [
            "Tokenizer Memory (per model)",
            "TransformerWeights Memory (per model)",
            "RunState Memory (per user)",
        ]:
            for model, value in zip(
                data.keys(),
                [model_data[mtype] for model_data in data.values()],
            ):
                totals[model] += value

    if title in [
        "Tokenizer Memory (per model)",
        "TransformerWeights Memory (per model)",
        "RunState Memory (per user)",
    ]:
        # Add the totals to the table
        total_row = ["Total"] + [f"{totals[model]:.2f}" for model in data.keys()]
        file.write(" | ".join(total_row) + "\n")
    else:
        # Calculate max users for each model
        # Calculate number of users for each model and add it to the data
        number_of_users = {}
        for model, values in data.items():
            total_available_memory = 4 * 1024  # Available canister memory in MB
            total_tokenizer_memory = values["Total Tokenizer Memory (per model)"]
            total_transformer_weights_memory = values[
                "Total TransformerWeights Memory (per model)"
            ]
            total_runstate_memory = values["Total RunState Memory (per user)"]

            number_of_users[model] = int(
                (
                    total_available_memory
                    - total_tokenizer_memory
                    - total_transformer_weights_memory
                )
                / total_runstate_memory
            )

        # Write the markdown table for number of users
        file.write("\n\n")
        # Get the models for headers
        headers = ["Canister Metrics"] + [f"{model}<br>(MB)" for model in data.keys()]

        # Write the table name
        file.write("### Canister Metrics\n\n")

        # Write the headers
        file.write(" | ".join(headers) + "\n")
        file.write(" | ".join(["---"] * len(headers)) + "\n")

        row_data = ["Max number of concurrent users"] + [
            f"{number_of_users[model]}" for model in data.keys()
        ]
        file.write(" | ".join(row_data) + "\n")

    file.write("\n\n")


def main() -> int:
    """Reads the model.bin files and summarizes the resource requirements."""
    file_paths: dict[str, Path] = {
        "260K": ROOT_PATH / "stories260K/stories260K.bin",
        "15M": ROOT_PATH / "models/stories15Mtok4096.bin",
        "42M": ROOT_PATH / "models/stories42M.bin",
        "110M": ROOT_PATH / "models/stories110M.bin",
    }

    data = {}
    for key, file_path in file_paths.items():
        config: dict[str, int] = read_config_from_file(file_path)
        data[key] = calculate_memory(config)

    output_path = ROOT_PATH / "README_icpp_llama2_resource_requirements.md"
    with open(output_path, "w", encoding="utf-8") as file:
        file.write("# Canister resource requirements for llama2_c.")
        file.write("\n")
        file.write("\nDo not edit this file. It is created with the command: ")
        file.write("\n```bash")
        file.write("\npython -m scripts.icpp_llama2_sizer")
        file.write("\n```\n\n")
        for key in [
            "Tokenizer Memory (per model)",
            "TransformerWeights Memory (per model)",
            "RunState Memory (per user)",
            "Total Memory",
        ]:
            subset_data = {k: v[key] for k, v in data.items()}
            write_data(file, key, subset_data)

    return 0


if __name__ == "__main__":
    sys.exit(main())
