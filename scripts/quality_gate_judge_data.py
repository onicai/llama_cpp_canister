"""Prompts, parsing rules and the tiered answer set for the Judge quality gate.

Everything funnAI-specific lives here so scripts/quality_gate_judge.py holds only
the harness. The prompt builders are transcribed BYTE FOR BYTE from the
production Motoko - get one trailing space wrong and the measurement is of a
different prompt:

  judge system half : PoAIW/src/Challenger/src/Main.mo  judgePromptGenerationDoIt_
  judge answer half : PoAIW/src/Judge/src/Main.mo       judgeChallengeResponseDoIt_
  challenger        : PoAIW/src/Challenger/src/Main.mo  challengeGenerationDoIt_
  mAIner            : PoAIW/src/Challenger/src/Main.mo  mAInerPromptGenerationDoIt_

Watch for, in the Judge prompt:
  - lines ending in a SPACE before the newline ("question: ", "correctness: ",
    "<|im_end|> ")
  - "blank" lines that are a single space (" \n"), NOT empty
  - "Grade the answer between 1 and 5" has NO trailing period. The design
    notebook (PoAIW/scripts/3-judge.ipynb) has one; production does not.
  - the line before the closing <|im_end|> IS truly empty
"""

# The tiered answers below are verbatim test data and must not be reflowed.
# pylint: disable=line-too-long
import json
from typing import Any, Dict, List, Optional

# --------------------------------------------------------------------------
# Prompts
# --------------------------------------------------------------------------

# Assistant-turn openers. Production uses PLAIN for every role and every model.
# Qwen3 opens a <think> block unless one is prefilled closed and empty, and at a
# ~20 token budget it then spends the whole budget reasoning and never emits a
# bare digit. NOTHINK is that fix; running qwen3 with PLAIN is a deliberate
# control that should demonstrate the collapse.
ASSISTANT_PLAIN = "<|im_start|>assistant\n"
ASSISTANT_NOTHINK = "<|im_start|>assistant\n<think>\n\n</think>\n\n"


def judge_prompt(question: str, answer: str, assistant: str = ASSISTANT_PLAIN) -> str:
    """The exact prompt production sends to the Judge LLM."""
    return (
        "<|im_start|>system\n"
        "You grade answers based on its correctness to the question: \n"
        " \n"
        "- " + question + "\n"
        " \n"
        "Grade the answer between 1 and 5\n"
        "1 = completely wrong\n"
        "2 = mostly wrong\n"
        "3 = partially correct\n"
        "4 = mostly correct\n"
        "5 = completely correct\n"
        " \n"
        "<|im_end|> \n"
        "<|im_start|>user\n"
        "Grade this answer based on its correctness: \n"
        " \n"
        "- " + answer + "\n"
        " \n"
        "Respond with the grade only, nothing else.\n"
        "\n<|im_end|>\n" + assistant
    )


def challenger_prompt(
    topic: str, starts_with: str, assistant: str = ASSISTANT_PLAIN
) -> str:
    """The exact prompt production sends to generate a challenge question."""
    return (
        "<|im_start|>user\n"
        "Ask a question that can be answered with common knowledge. "
        "Do NOT give the answer. Ask me a question about "
        + topic
        + ", and start the question with "
        + starts_with
        + "."
        "\n<|im_end|>\n" + assistant
    )


def mainer_prompt(question: str, assistant: str = ASSISTANT_PLAIN) -> str:
    """The exact prompt production sends to answer a challenge."""
    return (
        "<|im_start|>user\n"
        "Answer the following question as brief as possible. This is the question: "
        + question
        + "\n<|im_end|>\n"
        + assistant
    )


# Production inference parameters, per role (PoAIW Motoko).
# The Judge is GREEDY and fixed-seed, so it is deterministic: repeats of an
# identical prompt add nothing, and a harsh score is systematic, not noise.
ROLE_PARAMS = {
    "judge": {"temp": 0.0, "seed": 42, "max_continue_loop_count": 1},
    "challenger": {"temp": 0.7, "seed": None, "max_continue_loop_count": 30},
    "mainer": {"temp": 0.8, "seed": None, "max_continue_loop_count": 3},
}

# Sampler defaults from the vendored fork (common/common.h). Production overrides
# none of them, so they must NOT be passed explicitly either.
FORK_SAMPLER_DEFAULTS = {
    "top_k": 40,
    "top_p": 0.95,
    "min_p": 0.05,
    "repeat_penalty": 1.0,
}

STOP_TOKENS = ["<|im_end|>", "<|im_start|>", "<|endoftext|>"]


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------


def parse_score_production(raw: str) -> int:
    """Motoko `Nat.fromText(generationOutput)` over the ENTIRE raw output.

    No trim, no split, no regex, no clamping (Judge/src/Main.mo:1162). So "3"
    parses, but "3\\n", " 3", "3." and "Grade: 3" are all null -> stored as 0.
    Motoko Nat.fromText accepts underscore separators and rejects a sign.
    """
    if not raw:
        return 0
    candidate = raw.replace("_", "")
    if not candidate.isdigit():
        return 0
    return int(candidate)


def filter_text(text: str) -> str:
    """The design notebook's filter (PoAIW/scripts/3-judge.ipynb), NOT production.

    Production applies this to neither the mAIner answer nor the Judge output.
    Measuring with and without it quantifies what that divergence costs.
    """
    text = text.split("\n")[0]
    text = text.replace('"', "").replace("'", "")
    text = text.strip()
    text = text.split(" ")[0]
    return text


def parse_score_notebook(raw: str) -> int:
    """The notebook's forgiving parse, for comparison. 0 when unparseable."""
    try:
        return int(filter_text(raw))
    except (ValueError, TypeError):
        return 0


# --------------------------------------------------------------------------
# Set A - tiered answers, the calibration instrument
# --------------------------------------------------------------------------
# Tiers mirror the Judge's own rubric:
#   5 completely correct   4 mostly correct   3 partially correct
#   2 mostly wrong         1 completely wrong
# Calibration is measured as rank agreement between `tier` and the score the
# Judge emits - not as exact equality, which would be too strong a demand of a
# 0.5B model.

CASES: List[Dict[str, Any]] = [
    {
        "case_id": "music-jazz-fusion",
        "topic": "music",
        # Verbatim from funnAI testing-network challenge 27, judged 2026-09-14.
        "question": "What is a key element that distinguishes traditional jazz from jazz fusion?",
        "note": (
            "The tier-4 answer is the REAL mAIner answer that scored 1 in production. "
            "It is vague but correct in direction: fusion is defined by electric "
            "instruments and rock/funk rhythms, so 'focus on traditional instruments "
            "and techniques' is right about traditional jazz without being precise."
        ),
        "answers": [
            {
                "tier": 5,
                "text": "Jazz fusion blends jazz improvisation with rock and funk, using electric instruments like electric guitar and synthesizers, while traditional jazz uses acoustic instruments and swing rhythms.",
            },
            {
                "tier": 4,
                "text": "Key element that distinguishes traditional jazz from jazz fusion is its focus on traditional instruments and techniques.",
                "real_production_answer": True,
            },
            {"tier": 3, "text": "Fusion is louder and uses electric instruments."},
            {
                "tier": 2,
                "text": "Traditional jazz is always played faster than fusion.",
            },
            {
                "tier": 1,
                "text": "Jazz fusion was invented in the 1600s by classical composers.",
            },
        ],
    },
    {
        "case_id": "crypto-private-key",
        "topic": "crypto",
        "question": "What is a private key used for in a cryptocurrency wallet?",
        "answers": [
            {
                "tier": 5,
                "text": "A private key is a secret number that proves ownership of funds and is used to sign transactions authorizing them to be spent.",
            },
            {"tier": 4, "text": "It signs transactions and proves you own the coins."},
            {"tier": 3, "text": "It is a secret code for your wallet."},
            {
                "tier": 2,
                "text": "It is the public address you give people so they can pay you.",
            },
            {
                "tier": 1,
                "text": "A private key is a type of hardware cable used to connect a wallet to a printer.",
            },
        ],
    },
    {
        "case_id": "nature-photosynthesis",
        "topic": "nature",
        "question": "What do plants produce during photosynthesis?",
        "answers": [
            {
                "tier": 5,
                "text": "Plants produce glucose and oxygen, using carbon dioxide, water and light energy.",
            },
            {"tier": 4, "text": "Sugar and oxygen."},
            {"tier": 3, "text": "Oxygen."},
            {"tier": 2, "text": "Carbon dioxide."},
            {
                "tier": 1,
                "text": "Plants produce iron and helium during photosynthesis.",
            },
        ],
    },
    {
        "case_id": "space-sun-star",
        "topic": "space",
        "question": "Why does the Sun appear larger than other stars from Earth?",
        "answers": [
            {
                "tier": 5,
                "text": "Because the Sun is vastly closer to Earth than any other star, so it subtends a much larger angle in the sky.",
            },
            {"tier": 4, "text": "It is much closer to us than the other stars."},
            {"tier": 3, "text": "Because of the distance."},
            {"tier": 2, "text": "Because the Sun is the biggest star in the universe."},
            {
                "tier": 1,
                "text": "Because the Sun is a planet that orbits close to the Moon.",
            },
        ],
    },
    {
        "case_id": "history-ww2-end",
        "topic": "history",
        "question": "In which year did the Second World War end?",
        "answers": [
            {"tier": 5, "text": "1945"},
            {"tier": 4, "text": "It ended in 1945."},
            {"tier": 3, "text": "In the mid 1940s."},
            {"tier": 2, "text": "1918"},
            {"tier": 1, "text": "1776"},
        ],
    },
    {
        "case_id": "science-water-boil",
        "topic": "science",
        "question": "At what temperature does water boil at sea level?",
        "answers": [
            {
                "tier": 5,
                "text": "100 degrees Celsius, or 212 degrees Fahrenheit, at standard atmospheric pressure.",
            },
            {"tier": 4, "text": "100 degrees Celsius."},
            {"tier": 3, "text": "Around 100 degrees."},
            {"tier": 2, "text": "50 degrees Celsius."},
            {"tier": 1, "text": "Water does not boil; it freezes when heated."},
        ],
    },
    {
        "case_id": "technology-http",
        "topic": "technology",
        "question": "What does HTTP stand for?",
        "answers": [
            {"tier": 5, "text": "HyperText Transfer Protocol"},
            {
                "tier": 4,
                "text": "Hypertext transfer protocol, the protocol used by the web.",
            },
            {"tier": 3, "text": "A protocol used by websites."},
            {"tier": 2, "text": "High Transfer Text Process"},
            {"tier": 1, "text": "Hot Tea Take Personally"},
        ],
    },
    {
        "case_id": "engineering-bridge",
        "topic": "engineering",
        "question": "Why are triangles commonly used in bridge and truss design?",
        "answers": [
            {
                "tier": 5,
                "text": "A triangle is geometrically rigid: its shape cannot change without changing the length of a side, so it distributes load without deforming.",
            },
            {
                "tier": 4,
                "text": "Because triangles are rigid and spread the load well.",
            },
            {"tier": 3, "text": "Because they are strong."},
            {
                "tier": 2,
                "text": "Because triangles are cheaper to manufacture than squares.",
            },
            {"tier": 1, "text": "Because triangles float on water."},
        ],
    },
    {
        "case_id": "math-prime",
        "topic": "math",
        "question": "What is a prime number?",
        "answers": [
            {
                "tier": 5,
                "text": "A whole number greater than 1 whose only positive divisors are 1 and itself.",
            },
            {"tier": 4, "text": "A number divisible only by 1 and itself."},
            {"tier": 3, "text": "A number that cannot be divided evenly."},
            {"tier": 2, "text": "Any odd number."},
            {
                "tier": 1,
                "text": "A prime number is a number that contains a decimal point.",
            },
        ],
    },
    {
        "case_id": "art-mona-lisa",
        "topic": "art",
        "question": "Who painted the Mona Lisa?",
        "answers": [
            {"tier": 5, "text": "Leonardo da Vinci"},
            {
                "tier": 4,
                "text": "It was painted by Leonardo da Vinci in the early 1500s.",
            },
            {"tier": 3, "text": "An Italian Renaissance painter."},
            {"tier": 2, "text": "Michelangelo"},
            {"tier": 1, "text": "Pablo Picasso painted it last year."},
        ],
    },
    {
        "case_id": "music-instrument-family",
        "topic": "music",
        "question": "Which family of instruments does the violin belong to?",
        "answers": [
            {
                "tier": 5,
                "text": "The string family; it is played with a bow across four strings.",
            },
            {"tier": 4, "text": "The strings."},
            {"tier": 3, "text": "It is an orchestral instrument."},
            {"tier": 2, "text": "The woodwind family."},
            {"tier": 1, "text": "The violin is a percussion instrument made of glass."},
        ],
    },
    {
        "case_id": "science-gravity",
        "topic": "science",
        "question": "What causes objects to fall towards the Earth?",
        "answers": [
            {
                "tier": 5,
                "text": "Gravity, the attractive force the Earth's mass exerts on other masses.",
            },
            {"tier": 4, "text": "The Earth's gravity pulls them down."},
            {"tier": 3, "text": "Gravity."},
            {"tier": 2, "text": "Air pressure pushes them down."},
            {"tier": 1, "text": "Objects fall because they are afraid of heights."},
        ],
    },
]


def set_a_pairs() -> List[Dict[str, Any]]:
    """Flatten CASES into (case_id, topic, question, answer, tier) rows."""
    rows = []
    for case in CASES:
        for ans in case["answers"]:
            rows.append(
                {
                    "set": "A",
                    "case_id": case["case_id"],
                    "topic": case["topic"],
                    "question": case["question"],
                    "answer": ans["text"],
                    "tier": ans["tier"],
                    "real_production_answer": ans.get("real_production_answer", False),
                }
            )
    return rows


def set_b_pairs(path: str, limit: Optional[int] = None) -> List[Dict[str, Any]]:
    """Real mAIner answers from PoAIW/scripts/3-judge.json.

    That notebook run never finished: of 80 challenges only 7 carry answers (all
    topic `crypto`), giving 767 real answers of which 222 have a `judge_score`.
    So it is a corpus of real answers, not a labelled benchmark - `tier` is None
    and `notebook_score` is the notebook Judge's own value where present.
    """
    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    rows = []
    for case in data:
        for ans in case.get("mainer_answers", []):
            rows.append(
                {
                    "set": "B",
                    "case_id": f"judgejson-{case['challenge_id']}",
                    "topic": case["challenge_topic"],
                    "question": case["challenge_question"],
                    "answer": ans["mainer_answer"],
                    "tier": None,
                    "notebook_score": ans.get("judge_score"),
                    "real_production_answer": True,
                }
            )
    return rows[:limit] if limit else rows
