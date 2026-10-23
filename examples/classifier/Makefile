# The DAO-moderation classifier example: see docs/README-classifier-training.md.
#
#   make data      download the dataset from Hugging Face
#   make train     fine-tune Ettin-encoder-17M (f32) into build/model
#   make eval      score build/model on the held-out test set
#   make gguf      convert build/model to build/dao-moderation-f32.gguf

PYTHON ?= python
BASE ?= jhu-clsp/ettin-encoder-17m
DATASET := https://huggingface.co/datasets/onicai/dao-moderation-classifier/resolve/main
# A llama.cpp checkout with convert_hf_to_gguf.py: the repo's fork by default.
# From the release zip, which has no llama.cpp, pass LLAMA_CPP=<your checkout>.
LLAMA_CPP ?= ../../src/llama_cpp_onicai_fork

.PHONY: data
data:
	mkdir -p data
	curl -sSfL -o data/train.jsonl $(DATASET)/train.jsonl
	curl -sSfL -o data/test.jsonl $(DATASET)/test.jsonl
	wc -l data/train.jsonl data/test.jsonl

.PHONY: train
train:
	$(PYTHON) train.py --base $(BASE) --data data/train.jsonl --out build/model

.PHONY: eval
eval:
	$(PYTHON) eval.py build/model data/test.jsonl --misses

.PHONY: gguf
gguf:
	$(PYTHON) "$(LLAMA_CPP)/convert_hf_to_gguf.py" build/model --outtype f32 --outfile build/dao-moderation-f32.gguf
